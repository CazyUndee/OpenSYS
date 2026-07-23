/*
 * net.c - RTL8139 Ethernet Driver
 *
 * Basic network card driver.
 */

#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "net_drv.h"
#include "pci.h"
#include "io.h"
#include "vga.h"
#include "pmm.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

/* Register offsets from I/O base */
#define RTL_MAC       0x00
#define RTL_MAR       0x08
#define RTL_TXSTATUS0 0x10
#define RTL_TXADDR0   0x20
#define RTL_RXBUF     0x30
#define RTL_COMMAND   0x37
#define RTL_CAPR      0x38
#define RTL_IMR       0x3C
#define RTL_ISR       0x3E
#define RTL_TCR       0x40
#define RTL_RCR       0x44
#define RTL_CONFIG1   0x52

#define RTL_CMD_RESET 0x10
#define RTL_CMD_RX    0x0E
#define RTL_CMD_TX    0x04

#define RTL_RCR_WRAP  (1U << 7)
#define RTL_RCR_ACCEPT_ALL  ((1U << 4) | (1U << 3))

#define RTL_IMR_ALL   (0xFFFF)

static uint16_t rtl_io_base = 0;
static uint8_t* rtl_rx_ring = NULL;
static phys_addr_t rtl_rx_phys = 0;
static uint8_t  rtl_tx_idx = 0;
static uint8_t* rtl_tx_virt[4] = {0};
static phys_addr_t rtl_tx_phys[4] = {0};

static mac_addr_t local_mac;

static inline uint8_t  inb_r(uint16_t p) { return inb(p); }
static inline void   outb_r(uint16_t p, uint8_t v) { outb(p, v); }
static inline uint16_t inw_r(uint16_t p) { return inw(p); }
static inline void   outw_r(uint16_t p, uint16_t v) { outw(p, v); }
static inline uint32_t inl_r(uint16_t p) { return inl(p); }
static inline void   outl_r(uint16_t p, uint32_t v) { outl(p, v); }

int net_drv_init(void) {
    int bus, dev, func;
    int found = 0;

    terminal_writestring("[NET] Probing for RTL8139...\n");
    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (func = 0; func < 8; func++) {
                uint16_t vendor = pci_read_word(bus, dev, func, PCI_VENDOR_ID);
                uint16_t device = pci_read_word(bus, dev, func, PCI_DEVICE_ID);
                if (vendor == RTL8139_VENDOR && device == RTL8139_DEVICE) {
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }

    if (!found) {
        terminal_writestring("[NET] RTL8139 not found\n");
        return -1;
    }

    uint16_t cmd = pci_read_word(bus, dev, func, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER;
    pci_write_dword(bus, dev, func, PCI_COMMAND, cmd);

    rtl_io_base = pci_get_io_bar(bus, dev, func, 0);
    if (!rtl_io_base) {
        /* Some QEMU configurations might map it as MMIO even though
           RTL8139 prefers I/O. Try BAR1. */
        uint32_t bar1 = pci_read_dword(bus, dev, func, PCI_BAR1);
        if (bar1 & 1) rtl_io_base = (uint16_t)(bar1 & 0xFFFC);
        else {
            terminal_writestring("[NET] RTL8139 has no valid I/O BAR\n");
            return -1;
        }
    }

    terminal_writestring("[NET] RTL8139 at I/O 0x");
    terminal_put_hex((uint64_t)rtl_io_base);
    terminal_writestring("\n");

    /* Reset */
    outb_r(RTL_COMMAND + rtl_io_base, RTL_CMD_RESET);
    while (inb_r(RTL_COMMAND + rtl_io_base) & RTL_CMD_RESET);

    /* Read MAC */
    for (int i = 0; i < 6; i++) {
        local_mac.addr[i] = inb_r(rtl_io_base + RTL_MAC + i);
    }

    /* Allocate RX ring */
    rtl_rx_phys = pmm_alloc_page();
    if (!rtl_rx_phys) return -1;
    rtl_rx_ring = (uint8_t*)((uint64_t)rtl_rx_phys);

    /* Allocate TX buffers */
    for (int i = 0; i < 4; i++) {
        rtl_tx_phys[i] = pmm_alloc_page();
        if (!rtl_tx_phys[i]) return -1;
        rtl_tx_virt[i] = (uint8_t*)((uint64_t)rtl_tx_phys[i]);
    }

    outl_r(rtl_io_base + RTL_RXBUF, (uint32_t)rtl_rx_phys);
    outw_r(rtl_io_base + RTL_IMR, RTL_IMR_ALL);
    outl_r(rtl_io_base + RTL_TCR, 0x03000000);
    outl_r(rtl_io_base + RTL_RCR, RTL_RCR_ACCEPT_ALL | RTL_RCR_WRAP | 0x0F);
    outb_r(rtl_io_base + RTL_COMMAND, RTL_CMD_RX | RTL_CMD_TX);

    terminal_writestring("[NET] RTL8139 initialized, MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) terminal_putchar(':');
        terminal_put_hex((uint64_t)local_mac.addr[i]);
    }
    terminal_writestring("\n");
    return 0;
}

void net_drv_get_mac(mac_addr_t* mac) {
    for (int i = 0; i < 6; i++) mac->addr[i] = local_mac.addr[i];
}

int net_drv_send(const void* data, uint16_t len) {
    if (!rtl_io_base) return -1;
    if (len > 1536) len = 1536;
    while ((inl_r(rtl_io_base + RTL_TXSTATUS0 + (rtl_tx_idx * 4)) & 0x8000) == 0) {
        __asm__ volatile("pause");
    }
    volatile uint8_t* tx = (volatile uint8_t*)((uint64_t)rtl_tx_virt[rtl_tx_idx]);
    for (int i = 0; i < len; i++) tx[i] = ((const uint8_t*)data)[i];
    outl_r(rtl_io_base + RTL_TXADDR0 + (rtl_tx_idx * 4), (uint32_t)rtl_tx_phys[rtl_tx_idx]);
    outl_r(rtl_io_base + RTL_TXSTATUS0 + (rtl_tx_idx * 4), len);
    rtl_tx_idx = (rtl_tx_idx + 1) & 3;
    return len;
}

int net_drv_recv(void* buf, uint16_t len) {
    if (!rtl_io_base || !buf || len == 0) return 0;

#define RTL_RX_BUF_SIZE 8192 /* 8KB ring + 16-byte header packets */

    /* Check ISR for RX_OK (bit 0) */
    uint16_t isr = inw_r(rtl_io_base + RTL_ISR);
    if (!(isr & 1)) return 0; /* No packet available */

    /* Read packet header from RX ring at current CAPR */
    uint16_t capr = inw_r(rtl_io_base + RTL_CAPR) & 0x3FFC; /* Mask to 8KB, align to 4 */
    uint32_t* rx_hdr = (uint32_t*)(rtl_rx_ring + capr);
    uint32_t rx_status  = rx_hdr[0]; /* Status word */
    uint32_t rx_len_raw = rx_hdr[1]; /* Length (includes 4-byte CRC) */

    if (!(rx_status & 1)) return 0; /* Not a valid packet */

    uint16_t pkt_len = rx_len_raw & 0x3FFF; /* Bits 13:0 = length */
    if (pkt_len < 4) return 0; /* Too short */

    /* Data starts at capr + 4 (after 4-byte status word) */
    uint16_t data_offset = capr + 4;
    uint16_t copy_len = pkt_len - 4; /* Exclude CRC */
    if (copy_len > len) copy_len = len;

    /* Copy packet data, handling ring buffer wrap-around */
    uint8_t* dst = (uint8_t*)buf;
    uint16_t remaining = copy_len;
    uint16_t src_offset = data_offset;

    while (remaining > 0) {
        uint16_t chunk = remaining;
        if (src_offset + chunk > RTL_RX_BUF_SIZE) {
            chunk = RTL_RX_BUF_SIZE - src_offset;
        }
        for (uint16_t i = 0; i < chunk; i++) {
            dst[i] = rtl_rx_ring[src_offset + i];
        }
        dst += chunk;
        src_offset = (src_offset + chunk) % RTL_RX_BUF_SIZE;
        remaining -= chunk;
    }

    /* Update CAPR: advance past this packet (packet = 4 header + pkt_len - 4 + 4 CRC) */
    uint16_t next_capr = capr + pkt_len + 4;
    if (next_capr >= RTL_RX_BUF_SIZE) next_capr -= RTL_RX_BUF_SIZE;
    outw_r(rtl_io_base + RTL_CAPR, next_capr);

    /* Clear RX_OK bit in ISR */
    outw_r(rtl_io_base + RTL_ISR, isr | 1);

    return copy_len;
#undef RTL_RX_BUF_SIZE
}

/* Stack init wrapper */
int net_init(void) {
    return net_drv_init();
}
