/*
 * usb_host.c - USB Host Controller (UHCI) Driver
 *
 * Implements UHCI (Universal Host Controller Interface) for USB 1.1.
 * Supports low-speed (1.5 Mbps) and full-speed (12 Mbps) devices.
 * Primary use: HID keyboard input via interrupt transfers.
 */

#include "usb.h"
#include "uhci.h"
#include "pci.h"
#include "io.h"
#include "pmm.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* UHCI Controller State                                               */
/* ------------------------------------------------------------------ */

#define MAX_DEVICES      16
#define FRAME_LIST_SIZE  1024          /* 1024 entries × 4 bytes = 4 KB */
#define TD_POOL_SIZE     128           /* Pre-allocated TDs */
#define QH_POOL_SIZE     32            /* Pre-allocated QHs */
#define POLL_TIMEOUT     50000         /* ~50 ms at ~1 MHz loop */

/* I/O register access macros */
#define UHCI_REG8(reg)    ((uint16_t)(uhci_io_base + (reg)))
#define UHCI_REG16(reg)   ((uint16_t)(uhci_io_base + (reg)))

static uint16_t uhci_io_base = 0;      /* I/O base from PCI BAR0 */
static int      uhci_found   = 0;      /* Non-zero if UHCI initialized */

/* Frame list (physical + virtual addresses) */
static uint32_t*   frame_list      = NULL;    /* Virtual address */
static phys_addr_t frame_list_phys = 0;        /* Physical address */

/* TD pool -- allocated as one page, carved into 32-byte TDs */
#define TD_SIZE  32
static uint8_t*   td_pool      = NULL;
static phys_addr_t td_pool_phys = 0;
static uint8_t    td_used[TD_POOL_SIZE];       /* 0 = free, 1 = in-use */

/* QH pool -- allocated as one page, carved into 16-byte QHs */
#define QH_SIZE  16
static uint8_t*   qh_pool      = NULL;
static phys_addr_t qh_pool_phys = 0;
static uint8_t    qh_used[QH_POOL_SIZE];

/* Async schedule: a dummy QH that all async QHs chain after */
static int dummy_qh_index = -1;

/* Device list */
static usb_device_t devices[MAX_DEVICES];
static int device_count = 0;

/* ------------------------------------------------------------------ */
/* Helper: convert physical address to identity-mapped virtual address  */
/* ------------------------------------------------------------------ */
static inline void* phys_to_virt(phys_addr_t phys) {
    return (void*)(uint64_t)phys;
}

/* ------------------------------------------------------------------ */
/* TD / QH pool management                                             */
/* ------------------------------------------------------------------ */

static int td_alloc(void) {
    for (int i = 0; i < TD_POOL_SIZE; i++) {
        if (!td_used[i]) {
            td_used[i] = 1;
            return i;
        }
    }
    terminal_writestring("[USB] TD pool exhausted!\n");
    return -1;
}

static void td_free(int index) {
    if (index >= 0 && index < TD_POOL_SIZE) {
        td_used[index] = 0;
    }
}

static uhci_td_t* td_virt(int index) {
    return (uhci_td_t*)(td_pool + (index * TD_SIZE));
}

static phys_addr_t td_phys(int index) {
    return td_pool_phys + (index * TD_SIZE);
}

static int qh_alloc(void) {
    for (int i = 0; i < QH_POOL_SIZE; i++) {
        if (!qh_used[i]) {
            qh_used[i] = 1;
            return i;
        }
    }
    terminal_writestring("[USB] QH pool exhausted!\n");
    return -1;
}

static void qh_free(int index) {
    if (index >= 0 && index < QH_POOL_SIZE) {
        qh_used[index] = 0;
    }
}

static uhci_qh_t* qh_virt(int index) {
    return (uhci_qh_t*)(qh_pool + (index * QH_SIZE));
}

static phys_addr_t qh_phys(int index) {
    return qh_pool_phys + (index * QH_SIZE);
}

/* ------------------------------------------------------------------ */
/* Helper: wait for a bit to clear in UHCI status register             */
/* ------------------------------------------------------------------ */
static int wait_for_bit_clear(uint16_t reg, uint16_t mask, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; i++) {
        if (!(inw(UHCI_REG16(reg)) & mask))
            return 0;
        __asm__ volatile("pause");
    }
    return -1;  /* timeout */
}

/* ------------------------------------------------------------------ */
/* Helper: wait for a bit to set in UHCI status register               */
/* ------------------------------------------------------------------ */
static int wait_for_bit_set(uint16_t reg, uint16_t mask, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; i++) {
        if (inw(UHCI_REG16(reg)) & mask)
            return 0;
        __asm__ volatile("pause");
    }
    return -1;  /* timeout */
}

/* ------------------------------------------------------------------ */
/* Helper: poll TD for completion (clear ACTIVE + no errors)           */
/* ------------------------------------------------------------------ */
static int poll_td_complete(uhci_td_t* td, uint32_t timeout) {
    for (uint32_t i = 0; i < timeout; i++) {
        uint32_t status = td->status;
        if (!(status & UHCI_TD_ACTIVE)) {
            /* Transfer completed -- check for errors */
            if (status & (UHCI_TD_STALLED | UHCI_TD_BABBLE |
                          UHCI_TD_TIMEOUT | UHCI_TD_BITSTUFF)) {
                return -1;  /* error */
            }
            return (int)((status >> 21) & 0x7FF);  /* actual length */
        }
        __asm__ volatile("pause");
    }
    return -2;  /* timeout still active */
}

/* ------------------------------------------------------------------ */
/* usb_init: find UHCI on PCI, reset, allocate frame list              */
/* ------------------------------------------------------------------ */
int usb_init(void) {
    uint8_t bus, dev, func;

    terminal_writestring("[USB] Scanning PCI for UHCI controller...\n");

    if (pci_find_device(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB,
                        PCI_PROGIF_UHCI, &bus, &dev, &func) != 0) {
        terminal_writestring("[USB] No UHCI controller found\n");
        uhci_found = 0;
        return -1;
    }

    terminal_writestring("[USB] UHCI found at PCI ");
    terminal_put_hex(bus); terminal_writestring(":");
    terminal_put_hex(dev); terminal_writestring(".");
    terminal_put_hex(func); terminal_writestring("\n");

    /* Enable bus mastering and I/O space */
    uint16_t cmd = pci_read_word(bus, dev, func, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER;
    pci_write_dword(bus, dev, func, PCI_COMMAND, cmd);

    /* Get I/O base from BAR0 */
    uhci_io_base = pci_get_io_bar(bus, dev, func, 0);
    if (uhci_io_base == 0) {
        terminal_writestring("[USB] BAR0 is not an I/O BAR\n");
        uhci_found = 0;
        return -1;
    }

    terminal_writestring("[USB] I/O base: 0x");
    terminal_put_hex(uhci_io_base);
    terminal_writestring("\n");

    /* ---- Reset controller ---- */
    outw(UHCI_REG16(UHCI_USBCMD), UHCI_CMD_HCRESET);
    /* Wait for reset to complete (HCRESET self-clears) */
    wait_for_bit_clear(UHCI_USBCMD, UHCI_CMD_HCRESET, 10000);

    /* After reset, controller should be halted -- verify HCH bit */
    if (wait_for_bit_set(UHCI_USBSTS, UHCI_STS_HCH, 10000) != 0) {
        terminal_writestring("[USB] Controller not halted after reset\n");
    }

    /* ---- Allocate frame list ---- */
    phys_addr_t fl_phys = pmm_alloc_page();
    if (!fl_phys) {
        terminal_writestring("[USB] Failed to allocate frame list page\n");
        uhci_found = 0;
        return -1;
    }
    frame_list_phys = fl_phys;
    frame_list = (uint32_t*)phys_to_virt(fl_phys);

    /* All entries terminate (no schedule yet) */
    for (int i = 0; i < FRAME_LIST_SIZE; i++) {
        frame_list[i] = 0x00000001;  /* Terminate = QH with T=1 */
    }

    /* ---- Allocate TD pool page ---- */
    phys_addr_t td_phys_page = pmm_alloc_page();
    if (!td_phys_page) {
        terminal_writestring("[USB] Failed to allocate TD pool\n");
        pmm_free_page(frame_list_phys);
        uhci_found = 0;
        return -1;
    }
    td_pool_phys = td_phys_page;
    td_pool = (uint8_t*)phys_to_virt(td_phys_page);
    for (int i = 0; i < TD_POOL_SIZE; i++) td_used[i] = 0;

    /* ---- Allocate QH pool page ---- */
    phys_addr_t qh_phys_page = pmm_alloc_page();
    if (!qh_phys_page) {
        terminal_writestring("[USB] Failed to allocate QH pool\n");
        pmm_free_page(frame_list_phys);
        pmm_free_page(td_pool_phys);
        uhci_found = 0;
        return -1;
    }
    qh_pool_phys = qh_phys_page;
    qh_pool = (uint8_t*)phys_to_virt(qh_phys_page);
    for (int i = 0; i < QH_POOL_SIZE; i++) qh_used[i] = 0;

    /* ---- Create dummy QH for async schedule head ---- */
    dummy_qh_index = qh_alloc();
    uhci_qh_t* dummy = qh_virt(dummy_qh_index);
    dummy->link    = 0x00000001;  /* Terminate (T=1) -- end of async list */
    dummy->element = 0x00000001;  /* Terminate -- no TDs yet */
    /* Frame list entry 0 points to dummy QH */
    frame_list[0] = (uint32_t)qh_phys(dummy_qh_index) | 0x0002;  /* QH type */

    /* ---- Set frame list base address ---- */
    outl(UHCI_REG16(UHCI_FRBASEADD), (uint32_t)frame_list_phys);

    /* ---- Set frame number to 0 ---- */
    outw(UHCI_REG16(UHCI_FRNUM), 0);

    /* ---- Set SOF timing (default: 64) ---- */
    outb(UHCI_REG8(UHCI_SOFMOD), 64);

    /* ---- Run controller: set RS bit + Configure Flag ---- */
    outw(UHCI_REG16(UHCI_USBCMD), UHCI_CMD_RS | UHCI_CMD_CF);

    /* Wait for HCH to clear (controller is now running) */
    wait_for_bit_clear(UHCI_USBSTS, UHCI_STS_HCH, 10000);

    terminal_writestring("[USB] UHCI controller running\n");
    uhci_found = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* usb_enumerate: scan UHCI ports, reset, enumerate devices            */
/* ------------------------------------------------------------------ */
int usb_enumerate(void) {
    if (!uhci_found) return -1;

    terminal_writestring("[USB] Enumerating devices...\n");

    device_count = 0;

    /* Check two UHCI ports */
    for (int port = 1; port <= 2; port++) {
        uint16_t port_reg = (port == 1) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t port_sc = inw(UHCI_REG16(port_reg));

        if (!(port_sc & UHCI_PORT_CCS)) {
            terminal_writestring("[USB] Port ");
            terminal_put_dec(port);
            terminal_writestring(": no device\n");
            continue;
        }

        terminal_writestring("[USB] Port ");
        terminal_put_dec(port);
        terminal_writestring(": device detected\n");

        /* ---- Port Reset ---- */
        outw(UHCI_REG16(port_reg), UHCI_PORT_PR);
        /* Wait ~50 ms */
        for (volatile int w = 0; w < 500000; w++) __asm__ volatile("pause");
        outw(UHCI_REG16(port_reg), 0);
        /* Wait for reset recovery */
        for (volatile int w = 0; w < 200000; w++) __asm__ volatile("pause");

        /* Re-read port status after reset */
        port_sc = inw(UHCI_REG16(port_reg));

        /* Determine speed */
        int speed = USB_SPEED_FULL;
        if (port_sc & UHCI_PORT_LSDA) {
            speed = USB_SPEED_LOW;
            terminal_writestring("[USB] Low-speed device\n");
        } else {
            terminal_writestring("[USB] Full-speed device\n");
        }

        /* ---- Enumerate ---- */
        int dev_addr = 1;  /* Simple: first device gets address 1 */

        /* Assign address via SET_ADDRESS control transfer */
        usb_request_t set_addr_req;
        set_addr_req.request_type = 0x00;
        set_addr_req.request      = USB_REQ_SET_ADDRESS;
        set_addr_req.value        = dev_addr;
        set_addr_req.index        = 0;
        set_addr_req.length       = 0;

        /* Build device stub to pass to control_transfer (address 0 for now) */
        usb_device_t stub_dev;
        stub_dev.address        = 0;       /* Default address before SET_ADDRESS */
        stub_dev.speed          = speed;
        stub_dev.max_packet_size = 64;     /* Will be updated after GET_DESCRIPTOR */
        stub_dev.endpoint_in    = 0x81;    /* EP1 IN (typical for HID) */
        stub_dev.endpoint_out   = 0x01;    /* EP1 OUT */

        /* Send SET_ADDRESS (to address 0) */
        /* Need a control transfer manually since device address changes */
        /* Build QH + 2 TDs: SETUP (8 bytes) + IN status (0 bytes) */

        /* Allocate TDs and QH */
        int td_setup_idx = td_alloc();
        int td_status_idx = td_alloc();
        int qh_idx = qh_alloc();
        if (td_setup_idx < 0 || td_status_idx < 0 || qh_idx < 0) {
            terminal_writestring("[USB] Out of TD/QH resources for SET_ADDRESS\n");
            continue;
        }

        /* Setup packet buffer (phys must be accessible by UHCI) */
        phys_addr_t setup_phys = pmm_alloc_page();
        if (!setup_phys) {
            td_free(td_setup_idx);
            td_free(td_status_idx);
            qh_free(qh_idx);
            continue;
        }
        uint8_t* setup_buf = (uint8_t*)phys_to_virt(setup_phys);
        setup_buf[0] = set_addr_req.request_type;
        setup_buf[1] = set_addr_req.request;
        setup_buf[2] = (uint8_t)(set_addr_req.value & 0xFF);
        setup_buf[3] = (uint8_t)((set_addr_req.value >> 8) & 0xFF);
        setup_buf[4] = (uint8_t)(set_addr_req.index & 0xFF);
        setup_buf[5] = (uint8_t)((set_addr_req.index >> 8) & 0xFF);
        setup_buf[6] = (uint8_t)(set_addr_req.length & 0xFF);
        setup_buf[7] = (uint8_t)((set_addr_req.length >> 8) & 0xFF);

        /* SETUP TD */
        uhci_td_t* td_setup = td_virt(td_setup_idx);
        td_setup->link    = (uint32_t)td_phys(td_status_idx);  /* point to status */
        td_setup->status  = UHCI_TD_ACTIVE | (0x7FF << 21);    /* max len = 2047 */
        td_setup->token   = UHCI_TD_PID_SETUP | (dev_addr << 8) | (0 << 15);  /* EP0 */
        td_setup->buffer  = (uint32_t)setup_phys;

        /* IN status TD (0 bytes from device) */
        uhci_td_t* td_status = td_virt(td_status_idx);
        td_status->link    = 0x00000001;  /* Terminate */
        td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_status->token   = UHCI_TD_PID_IN | UHCI_TD_IOC | (dev_addr << 8) | (0 << 15);
        td_status->buffer  = (uint32_t)setup_phys;  /* dummy */

        /* QH */
        uhci_qh_t* qh = qh_virt(qh_idx);
        qh->link    = (uint32_t)qh_phys(dummy_qh_index) | 0x0002;  /* chain to dummy */
        qh->element = (uint32_t)td_phys(td_setup_idx);  /* first TD */

        /* Insert into async schedule: frame list 0 -> dummy QH -> our QH */
        /* Actually, splice after dummy: dummy->link points to our QH */
        uhci_qh_t* dummy_qh = qh_virt(dummy_qh_index);
        /* Save old link, set new link */
        uint32_t old_link = dummy_qh->link;
        dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

        /* Wait for completion */
        int result = poll_td_complete(td_status, 500000);
        if (result < 0) {
            terminal_writestring("[USB] SET_ADDRESS failed\n");
        } else {
            terminal_writestring("[USB] SET_ADDRESS to ");
            terminal_put_dec(dev_addr);
            terminal_writestring(" OK\n");
        }

        /* Restore dummy QH link */
        dummy_qh->link = old_link;
        qh->link = 0x00000001;  /* terminate */

        pmm_free_page(setup_phys);
        td_free(td_setup_idx);
        td_free(td_status_idx);
        qh_free(qh_idx);
        /* Wait for schedule to drain */
        for (volatile int w = 0; w < 10000; w++) __asm__ volatile("pause");

        /* ---- GET_DESCRIPTOR (Device) to read max packet size ---- */
        usb_device_desc_t dev_desc;
        usb_request_t get_desc_req;
        get_desc_req.request_type = 0x80;  /* Device-to-host, standard, device */
        get_desc_req.request      = USB_REQ_GET_DESCRIPTOR;
        get_desc_req.value        = (USB_DESC_DEVICE << 8);  /* descriptor type + index 0 */
        get_desc_req.index        = 0;
        get_desc_req.length       = sizeof(usb_device_desc_t);  /* 18 bytes */

        /* Use stub_dev with the new address */
        stub_dev.address = dev_addr;
        (void)stub_dev;

        /* Manually build control transfer (same pattern as above) */
        td_setup_idx = td_alloc();
        int td_in_idx = td_alloc();
        td_status_idx = td_alloc();
        qh_idx = qh_alloc();
        if (td_setup_idx < 0 || td_in_idx < 0 || td_status_idx < 0 || qh_idx < 0) {
            terminal_writestring("[USB] Out of resources for GET_DESCRIPTOR\n");
            continue;
        }

        phys_addr_t data_phys = pmm_alloc_page();
        if (!data_phys) {
            td_free(td_setup_idx); td_free(td_in_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }
        uint8_t* data_buf = (uint8_t*)phys_to_virt(data_phys);

        /* Setup packet */
        setup_phys = pmm_alloc_page();
        if (!setup_phys) {
            pmm_free_page(data_phys);
            td_free(td_setup_idx); td_free(td_in_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }
        setup_buf = (uint8_t*)phys_to_virt(setup_phys);
        setup_buf[0] = get_desc_req.request_type;
        setup_buf[1] = get_desc_req.request;
        setup_buf[2] = (uint8_t)(get_desc_req.value & 0xFF);
        setup_buf[3] = (uint8_t)((get_desc_req.value >> 8) & 0xFF);
        setup_buf[4] = (uint8_t)(get_desc_req.index & 0xFF);
        setup_buf[5] = (uint8_t)((get_desc_req.index >> 8) & 0xFF);
        setup_buf[6] = (uint8_t)(get_desc_req.length & 0xFF);
        setup_buf[7] = (uint8_t)((get_desc_req.length >> 8) & 0xFF);

        /* Clear data buffer */
        for (int i = 0; i < 64; i++) data_buf[i] = 0;

        /* SETUP TD */
        td_setup = td_virt(td_setup_idx);
        td_setup->link    = (uint32_t)td_phys(td_in_idx);
        td_setup->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_setup->token   = UHCI_TD_PID_SETUP | (dev_addr << 8) | (0 << 15);
        td_setup->buffer  = (uint32_t)setup_phys;

        /* IN TD */
        uhci_td_t* td_in = td_virt(td_in_idx);
        td_in->link    = (uint32_t)td_phys(td_status_idx);
        td_in->status  = UHCI_TD_ACTIVE | (sizeof(usb_device_desc_t) << 21);
        td_in->token   = UHCI_TD_PID_IN | (dev_addr << 8) | (0 << 15);
        td_in->buffer  = (uint32_t)data_phys;

        /* OUT status TD (0 bytes to device) */
        td_status = td_virt(td_status_idx);
        td_status->link    = 0x00000001;  /* Terminate */
        td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_status->token   = UHCI_TD_PID_OUT | UHCI_TD_IOC | (dev_addr << 8) | (0 << 15);
        td_status->buffer  = (uint32_t)data_phys;

        /* QH */
        qh = qh_virt(qh_idx);
        qh->link    = 0x00000001;  /* terminate */
        qh->element = (uint32_t)td_phys(td_setup_idx);

        /* Schedule */
        dummy_qh = qh_virt(dummy_qh_index);
        old_link = dummy_qh->link;
        dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

        result = poll_td_complete(td_status, 500000);
        if (result < 0) {
            terminal_writestring("[USB] GET_DESCRIPTOR (device) failed\n");
            dummy_qh->link = old_link;
            pmm_free_page(data_phys);
            pmm_free_page(setup_phys);
            td_free(td_setup_idx); td_free(td_in_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }

        /* Parse device descriptor */
        usb_device_desc_t* desc = (usb_device_desc_t*)data_buf;
        uint8_t max_pkt = desc->max_packet_size0;
        terminal_writestring("[USB] VID: 0x");
        terminal_put_hex(desc->vendor_id);
        terminal_writestring(" PID: 0x");
        terminal_put_hex(desc->product_id);
        terminal_writestring(" MaxPacket: ");
        terminal_put_dec(max_pkt);
        terminal_writestring("\n");

        /* Restore dummy */
        dummy_qh->link = old_link;
        pmm_free_page(data_phys);
        pmm_free_page(setup_phys);
        td_free(td_setup_idx); td_free(td_in_idx);
        td_free(td_status_idx); qh_free(qh_idx);
        for (volatile int w = 0; w < 10000; w++) __asm__ volatile("pause");

        /* ---- SET_CONFIGURATION (value=1) ---- */
        usb_request_t set_cfg_req;
        set_cfg_req.request_type = 0x00;  /* Host-to-device, standard, device */
        set_cfg_req.request      = USB_REQ_SET_CONFIG;
        set_cfg_req.value        = 1;
        set_cfg_req.index        = 0;
        set_cfg_req.length       = 0;

        td_setup_idx = td_alloc();
        td_status_idx = td_alloc();
        qh_idx = qh_alloc();
        if (td_setup_idx < 0 || td_status_idx < 0 || qh_idx < 0) continue;

        setup_phys = pmm_alloc_page();
        if (!setup_phys) {
            td_free(td_setup_idx); td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }
        setup_buf = (uint8_t*)phys_to_virt(setup_phys);
        setup_buf[0] = set_cfg_req.request_type;
        setup_buf[1] = set_cfg_req.request;
        setup_buf[2] = (uint8_t)(set_cfg_req.value & 0xFF);
        setup_buf[3] = (uint8_t)((set_cfg_req.value >> 8) & 0xFF);
        setup_buf[4] = (uint8_t)(set_cfg_req.index & 0xFF);
        setup_buf[5] = (uint8_t)((set_cfg_req.index >> 8) & 0xFF);
        setup_buf[6] = (uint8_t)(set_cfg_req.length & 0xFF);
        setup_buf[7] = (uint8_t)((set_cfg_req.length >> 8) & 0xFF);

        td_setup = td_virt(td_setup_idx);
        td_setup->link    = (uint32_t)td_phys(td_status_idx);
        td_setup->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_setup->token   = UHCI_TD_PID_SETUP | (dev_addr << 8) | (0 << 15);
        td_setup->buffer  = (uint32_t)setup_phys;

        td_status = td_virt(td_status_idx);
        td_status->link    = 0x00000001;
        td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_status->token   = UHCI_TD_PID_IN | UHCI_TD_IOC | (dev_addr << 8) | (0 << 15);
        td_status->buffer  = (uint32_t)setup_phys;

        qh = qh_virt(qh_idx);
        qh->link    = 0x00000001;
        qh->element = (uint32_t)td_phys(td_setup_idx);

        dummy_qh = qh_virt(dummy_qh_index);
        old_link = dummy_qh->link;
        dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

        result = poll_td_complete(td_status, 500000);
        dummy_qh->link = old_link;
        pmm_free_page(setup_phys);
        td_free(td_setup_idx); td_free(td_status_idx); qh_free(qh_idx);
        for (volatile int w = 0; w < 10000; w++) __asm__ volatile("pause");

        if (result < 0) {
            terminal_writestring("[USB] SET_CONFIGURATION failed\n");
            continue;
        }

        /* ---- Read configuration descriptor to find HID interface ---- */
        /* First read just the config descriptor header (9 bytes) for total length */
        uint8_t cfg_hdr[9];
        usb_request_t get_cfg_req;
        get_cfg_req.request_type = 0x80;
        get_cfg_req.request      = USB_REQ_GET_DESCRIPTOR;
        get_cfg_req.value        = (USB_DESC_CONFIG << 8) | 0;  /* config index 0 */
        get_cfg_req.index        = 0;
        get_cfg_req.length       = 9;

        td_setup_idx = td_alloc();
        td_in_idx = td_alloc();
        td_status_idx = td_alloc();
        qh_idx = qh_alloc();
        if (td_setup_idx < 0 || td_in_idx < 0 || td_status_idx < 0 || qh_idx < 0) continue;

        data_phys = pmm_alloc_page();
        setup_phys = pmm_alloc_page();
        if (!data_phys || !setup_phys) {
            pmm_free_page(data_phys); pmm_free_page(setup_phys);
            td_free(td_setup_idx); td_free(td_in_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }
        setup_buf = (uint8_t*)phys_to_virt(setup_phys);
        data_buf = (uint8_t*)phys_to_virt(data_phys);

        setup_buf[0] = get_cfg_req.request_type;
        setup_buf[1] = get_cfg_req.request;
        setup_buf[2] = (uint8_t)(get_cfg_req.value & 0xFF);
        setup_buf[3] = (uint8_t)((get_cfg_req.value >> 8) & 0xFF);
        setup_buf[4] = (uint8_t)(get_cfg_req.index & 0xFF);
        setup_buf[5] = (uint8_t)((get_cfg_req.index >> 8) & 0xFF);
        setup_buf[6] = (uint8_t)(get_cfg_req.length & 0xFF);
        setup_buf[7] = (uint8_t)((get_cfg_req.length >> 8) & 0xFF);
        for (int i = 0; i < 64; i++) data_buf[i] = 0;

        td_setup = td_virt(td_setup_idx);
        td_setup->link    = (uint32_t)td_phys(td_in_idx);
        td_setup->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_setup->token   = UHCI_TD_PID_SETUP | (dev_addr << 8) | (0 << 15);
        td_setup->buffer  = (uint32_t)setup_phys;

        td_in = td_virt(td_in_idx);
        td_in->link    = (uint32_t)td_phys(td_status_idx);
        td_in->status  = UHCI_TD_ACTIVE | (9 << 21);
        td_in->token   = UHCI_TD_PID_IN | (dev_addr << 8) | (0 << 15);
        td_in->buffer  = (uint32_t)data_phys;

        td_status = td_virt(td_status_idx);
        td_status->link    = 0x00000001;
        td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_status->token   = UHCI_TD_PID_OUT | UHCI_TD_IOC | (dev_addr << 8) | (0 << 15);
        td_status->buffer  = (uint32_t)data_phys;

        qh = qh_virt(qh_idx);
        qh->link    = 0x00000001;
        qh->element = (uint32_t)td_phys(td_setup_idx);

        dummy_qh = qh_virt(dummy_qh_index);
        old_link = dummy_qh->link;
        dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

        result = poll_td_complete(td_status, 500000);
        dummy_qh->link = old_link;

        uint16_t total_cfg_len = 9;
        if (result >= 0) {
            usb_config_desc_t* cfg_desc = (usb_config_desc_t*)data_buf;
            total_cfg_len = cfg_desc->total_length;
            terminal_writestring("[USB] Config descriptor total length: ");
            terminal_put_dec(total_cfg_len);
            terminal_writestring("\n");
        }

        pmm_free_page(data_phys);
        pmm_free_page(setup_phys);
        td_free(td_setup_idx); td_free(td_in_idx);
        td_free(td_status_idx); qh_free(qh_idx);
        for (volatile int w = 0; w < 10000; w++) __asm__ volatile("pause");

        /* Now read full config descriptor */
        get_cfg_req.length = total_cfg_len;

        td_setup_idx = td_alloc();
        td_in_idx = td_alloc();
        td_status_idx = td_alloc();
        qh_idx = qh_alloc();
        if (td_setup_idx < 0 || td_in_idx < 0 || td_status_idx < 0 || qh_idx < 0) continue;

        data_phys = pmm_alloc_page();
        setup_phys = pmm_alloc_page();
        if (!data_phys || !setup_phys) {
            pmm_free_page(data_phys); pmm_free_page(setup_phys);
            td_free(td_setup_idx); td_free(td_in_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            continue;
        }
        setup_buf = (uint8_t*)phys_to_virt(setup_phys);
        data_buf = (uint8_t*)phys_to_virt(data_phys);

        setup_buf[0] = get_cfg_req.request_type;
        setup_buf[1] = get_cfg_req.request;
        setup_buf[2] = (uint8_t)(get_cfg_req.value & 0xFF);
        setup_buf[3] = (uint8_t)((get_cfg_req.value >> 8) & 0xFF);
        setup_buf[4] = (uint8_t)(get_cfg_req.index & 0xFF);
        setup_buf[5] = (uint8_t)((get_cfg_req.index >> 8) & 0xFF);
        setup_buf[6] = (uint8_t)(get_cfg_req.length & 0xFF);
        setup_buf[7] = (uint8_t)((get_cfg_req.length >> 8) & 0xFF);
        for (int i = 0; i < 512; i++) data_buf[i] = 0;

        td_setup = td_virt(td_setup_idx);
        td_setup->link    = (uint32_t)td_phys(td_in_idx);
        td_setup->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_setup->token   = UHCI_TD_PID_SETUP | (dev_addr << 8) | (0 << 15);
        td_setup->buffer  = (uint32_t)setup_phys;

        td_in = td_virt(td_in_idx);
        td_in->link    = (uint32_t)td_phys(td_status_idx);
        uint32_t td_in_len = total_cfg_len;
        if (td_in_len > 2047) td_in_len = 2047;
        td_in->status  = UHCI_TD_ACTIVE | (td_in_len << 21);
        td_in->token   = UHCI_TD_PID_IN | (dev_addr << 8) | (0 << 15);
        td_in->buffer  = (uint32_t)data_phys;

        td_status = td_virt(td_status_idx);
        td_status->link    = 0x00000001;
        td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
        td_status->token   = UHCI_TD_PID_OUT | UHCI_TD_IOC | (dev_addr << 8) | (0 << 15);
        td_status->buffer  = (uint32_t)data_phys;

        qh = qh_virt(qh_idx);
        qh->link    = 0x00000001;
        qh->element = (uint32_t)td_phys(td_setup_idx);

        dummy_qh = qh_virt(dummy_qh_index);
        old_link = dummy_qh->link;
        dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

        result = poll_td_complete(td_status, 500000);
        dummy_qh->link = old_link;

/* Parse config descriptor to find HID interface & endpoints */
uint8_t dev_class = 0;
uint8_t dev_subclass = 0;
uint8_t dev_protocol = 0;
uint8_t ep_in_addr = 0x81; /* default EP1 IN */
uint8_t ep_out_addr = 0x01; /* default EP1 OUT */

        if (result >= 0) {
            uint8_t* ptr = data_buf;
            uint16_t offset = 0;
            while (offset < total_cfg_len) {
                uint8_t desc_len = ptr[offset];
                uint8_t desc_type = ptr[offset + 1];
                if (desc_len == 0) break;

      if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
        usb_interface_desc_t* iface = (usb_interface_desc_t*)(ptr + offset);
        dev_class = iface->iface_class;
        dev_subclass = iface->iface_subclass;
        dev_protocol = iface->iface_protocol;
        terminal_writestring("[USB] Interface class: 0x");
        terminal_put_hex(dev_class);
        terminal_writestring(" subclass: 0x");
        terminal_put_hex(dev_subclass);
        terminal_writestring(" protocol: 0x");
        terminal_put_hex(dev_protocol);
        terminal_writestring("\n");
      }

                if (desc_type == USB_DESC_ENDPOINT && desc_len >= 7) {
                    usb_endpoint_desc_t* ep = (usb_endpoint_desc_t*)(ptr + offset);
                    if (ep->address & 0x80) {
                        ep_in_addr = ep->address;
                        terminal_writestring("[USB] IN endpoint: 0x");
                        terminal_put_hex(ep_in_addr);
                        terminal_writestring("\n");
                    } else {
                        ep_out_addr = ep->address;
                        terminal_writestring("[USB] OUT endpoint: 0x");
                        terminal_put_hex(ep_out_addr);
                        terminal_writestring("\n");
                    }
                }

                offset += desc_len;
            }
        }

        pmm_free_page(data_phys);
        pmm_free_page(setup_phys);
        td_free(td_setup_idx); td_free(td_in_idx);
        td_free(td_status_idx); qh_free(qh_idx);

        /* ---- Add device to list ---- */
        if (device_count < MAX_DEVICES) {
            devices[device_count].address        = dev_addr;
            devices[device_count].speed          = speed;
            devices[device_count].config         = 1;
            devices[device_count].interface      = 0;
            devices[device_count].vendor_id      = desc->vendor_id;
            devices[device_count].product_id     = desc->product_id;
  devices[device_count].dev_class = dev_class;
  devices[device_count].dev_subclass = dev_subclass;
  devices[device_count].dev_protocol = dev_protocol;
            devices[device_count].max_packet_size = max_pkt;
            devices[device_count].endpoints      = 2;
            devices[device_count].endpoint_in    = ep_in_addr;
            devices[device_count].endpoint_out   = ep_out_addr;
            device_count++;
            terminal_writestring("[USB] Device enumerated OK\n");
        }
    }

    terminal_writestring("[USB] Enumeration complete: ");
    terminal_put_dec(device_count);
    terminal_writestring(" device(s)\n");
    return device_count;
}

/* ------------------------------------------------------------------ */
/* usb_find_device: find device by class                               */
/* ------------------------------------------------------------------ */
usb_device_t* usb_find_device(uint8_t dev_class) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].dev_class == dev_class) {
            return &devices[i];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* usb_control_transfer: perform a control transfer on endpoint 0      */
/* ------------------------------------------------------------------ */
int usb_control_transfer(usb_device_t* dev, usb_request_t* req,
                          void* data, uint16_t length) {
    if (!uhci_found || !dev || !req) return -1;
    if (dev->address == 0) return -1;  /* default address, not enumerated */

    int result = -1;

    /* Allocate TDs and QH */
    int td_setup_idx = td_alloc();
    int td_data_idx  = td_alloc();
    int td_status_idx = td_alloc();
    int qh_idx = qh_alloc();
    if (td_setup_idx < 0 || td_data_idx < 0 || td_status_idx < 0 || qh_idx < 0) {
        td_free(td_setup_idx); td_free(td_data_idx);
        td_free(td_status_idx); qh_free(qh_idx);
        return -1;
    }

    /* Setup packet buffer */
    phys_addr_t setup_phys = pmm_alloc_page();
    if (!setup_phys) {
        td_free(td_setup_idx); td_free(td_data_idx);
        td_free(td_status_idx); qh_free(qh_idx);
        return -1;
    }
    uint8_t* setup_buf = (uint8_t*)phys_to_virt(setup_phys);
    setup_buf[0] = req->request_type;
    setup_buf[1] = req->request;
    setup_buf[2] = (uint8_t)(req->value & 0xFF);
    setup_buf[3] = (uint8_t)((req->value >> 8) & 0xFF);
    setup_buf[4] = (uint8_t)(req->index & 0xFF);
    setup_buf[5] = (uint8_t)((req->index >> 8) & 0xFF);
    setup_buf[6] = (uint8_t)(req->length & 0xFF);
    setup_buf[7] = (uint8_t)((req->length >> 8) & 0xFF);

    /* Data buffer */
    phys_addr_t data_phys = 0;
    if (data != NULL && length > 0) {
        data_phys = pmm_alloc_page();
        if (!data_phys) {
            pmm_free_page(setup_phys);
            td_free(td_setup_idx); td_free(td_data_idx);
            td_free(td_status_idx); qh_free(qh_idx);
            return -1;
        }
        uint8_t* buf = (uint8_t*)phys_to_virt(data_phys);
        for (int i = 0; i < length; i++) buf[i] = ((uint8_t*)data)[i];
    }

    /* Determine data direction */
    int dir_in = (req->request_type & 0x80) != 0;
    uint16_t data_toggle = 1;  /* DATA1 for data stage after SETUP */

    uint8_t addr = dev->address;

    /* SETUP TD (always OUT, DATA0) */
    uhci_td_t* td_setup = td_virt(td_setup_idx);
    td_setup->link   = (uint32_t)td_phys(td_data_idx);
    td_setup->status = UHCI_TD_ACTIVE | (0x7FF << 21);
    td_setup->token  = UHCI_TD_PID_SETUP | (addr << 8) | (0 << 15);  /* EP0, DATA0 */
    td_setup->buffer = (uint32_t)setup_phys;

    /* Data stage TD (IN or OUT depending on direction) */
    uhci_td_t* td_data = td_virt(td_data_idx);
    td_data->link   = (uint32_t)td_phys(td_status_idx);
    td_data->buffer = data_phys;

    if (dir_in) {
        td_data->status = UHCI_TD_ACTIVE | (length << 21);
        td_data->token  = UHCI_TD_PID_IN | (addr << 8) | (data_toggle << 15);  /* EP0, DATA1 */
        /* If length is 0, status handshake is reversed */
    } else {
        td_data->status = UHCI_TD_ACTIVE | (length << 21);
        td_data->token  = UHCI_TD_PID_OUT | (addr << 8) | (data_toggle << 15);
    }

    /* Status stage TD (opposite direction) */
    uhci_td_t* td_status = td_virt(td_status_idx);
    td_status->link    = 0x00000001;  /* Terminate */
    td_status->status  = UHCI_TD_ACTIVE | (0x7FF << 21);
    td_status->buffer  = data_phys ? data_phys : setup_phys;

    if (dir_in || length == 0) {
        /* OUT status (host sends 0-byte packet to device) */
        td_status->token = UHCI_TD_PID_OUT | UHCI_TD_IOC | (addr << 8) | (1 << 15);
    } else {
        /* IN status (device sends 0-byte packet) */
        td_status->token = UHCI_TD_PID_IN | UHCI_TD_IOC | (addr << 8) | (1 << 15);
    }

    /* QH */
    uhci_qh_t* qh = qh_virt(qh_idx);
    qh->link    = 0x00000001;  /* terminate (will be spliced) */
    qh->element = (uint32_t)td_phys(td_setup_idx);

    /* Splice into async schedule after dummy QH */
    uhci_qh_t* dummy_qh = qh_virt(dummy_qh_index);
    uint32_t saved_link = dummy_qh->link;
    dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

    /* Wait for status TD to complete */
    int poll_result = poll_td_complete(td_status, POLL_TIMEOUT * 2);

    /* Restore async schedule */
    dummy_qh->link = saved_link;
    qh->link = 0x00000001;

    if (poll_result >= 0) {
        /* Copy data back if IN transfer */
        if (dir_in && data_phys && data) {
            uint8_t* src = (uint8_t*)phys_to_virt(data_phys);
            for (int i = 0; i < length; i++) {
                ((uint8_t*)data)[i] = src[i];
            }
        }
        result = length;
    } else if (poll_result == -2) {
        /* Timeout - don't print, just fail */
        result = -1;
    } else {
        result = -1;
    }

    /* Cleanup */
    pmm_free_page(setup_phys);
    if (data_phys) pmm_free_page(data_phys);
    td_free(td_setup_idx);
    td_free(td_data_idx);
    td_free(td_status_idx);
    qh_free(qh_idx);

    return result;
}

/* ------------------------------------------------------------------ */
/* usb_interrupt_transfer: perform interrupt IN transfer                */
/* ------------------------------------------------------------------ */
int usb_interrupt_transfer(usb_device_t* dev, uint8_t endpoint,
                            void* data, uint16_t length) {
    if (!uhci_found || !dev || !data) return -1;
    if (length == 0 || length > 64) return -1;

    int result = -1;

    /* Allocate one TD */
    int td_idx = td_alloc();
    int qh_idx = qh_alloc();
    if (td_idx < 0 || qh_idx < 0) {
        td_free(td_idx); qh_free(qh_idx);
        return -1;
    }

    /* Data buffer must be DMA-able */
    phys_addr_t data_phys = pmm_alloc_page();
    if (!data_phys) {
        td_free(td_idx); qh_free(qh_idx);
        return -1;
    }
    uint8_t* buf = (uint8_t*)phys_to_virt(data_phys);

    int is_in = (endpoint & 0x80) != 0;

    if (is_in) {
        /* Clear buffer for IN data */
        for (int i = 0; i < length; i++) buf[i] = 0;
    } else {
        /* Copy data for OUT */
        for (int i = 0; i < length; i++) buf[i] = ((uint8_t*)data)[i];
    }

    /* Build TD */
    uhci_td_t* td = td_virt(td_idx);
    td->link    = 0x00000001;  /* Terminate (single TD) */
    td->status  = UHCI_TD_ACTIVE | (length << 21);
    td->buffer  = (uint32_t)data_phys;

    /* Low-speed devices need LS bit */
    uint32_t token_flags = UHCI_TD_IOC;  /* Interrupt on Complete */
    token_flags |= (dev->address << 8);
    token_flags |= ((endpoint & 0x0F) << 15);  /* endpoint number in bit 15-19 */

    if (is_in) {
        token_flags |= UHCI_TD_PID_IN;
    } else {
        token_flags |= UHCI_TD_PID_OUT;
    }

    if (dev->speed == USB_SPEED_LOW) {
        token_flags |= UHCI_TD_LS;
    }

    td->token = token_flags;

    /* Build QH */
    uhci_qh_t* qh = qh_virt(qh_idx);
    qh->link    = 0x00000001;  /* terminate */
    qh->element = (uint32_t)td_phys(td_idx);

    /* Add to async schedule after dummy QH (simple polling approach) */
    uhci_qh_t* dummy_qh = qh_virt(dummy_qh_index);
    uint32_t saved_link = dummy_qh->link;
    dummy_qh->link = (uint32_t)qh_phys(qh_idx) | 0x0002;

    /* Poll for completion */
    int poll_result = poll_td_complete(td, POLL_TIMEOUT);

    /* Restore schedule */
    dummy_qh->link = saved_link;
    qh->link = 0x00000001;

    if (poll_result > 0) {
        /* Copy data back */
        for (int i = 0; i < poll_result && i < length; i++) {
            ((uint8_t*)data)[i] = buf[i];
        }
        result = poll_result;
    } else if (poll_result == 0) {
        /* NAK -- no data available, not an error for interrupt transfers */
        result = 0;
    } else {
        result = -1;
    }

    /* Cleanup */
    pmm_free_page(data_phys);
    td_free(td_idx);
    qh_free(qh_idx);

    return result;
}

/* ------------------------------------------------------------------ */
/* usb_get_device_count / usb_get_device (stubs kept working)          */
/* ------------------------------------------------------------------ */
int usb_get_device_count(void) {
    return device_count;
}

usb_device_t* usb_get_device(int index) {
    if (index >= 0 && index < device_count) {
        return &devices[index];
    }
    return 0;
}
