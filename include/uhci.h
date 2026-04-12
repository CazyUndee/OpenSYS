/*
 * uhci.h - UHCI (Universal Host Controller Interface) Driver
 * 
 * UHCI is the USB 1.1 host controller standard used by Intel.
 * Handles low-speed (1.5 Mbps) and full-speed (12 Mbps) devices.
 */

#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

/* UHCI I/O port offsets from base */
#define UHCI_USBCMD     0x00    /* Command Register */
#define UHCI_USBSTS     0x02    /* Status Register */
#define UHCI_USBINTR    0x04    /* Interrupt Enable Register */
#define UHCI_FRNUM      0x06    /* Frame Number Register */
#define UHCI_FRBASEADD  0x08    /* Frame List Base Address */
#define UHCI_SOFMOD     0x0C    /* Start of Frame Modify Register */
#define UHCI_PORTSC1    0x10    /* Port 1 Status/Control */
#define UHCI_PORTSC2    0x12    /* Port 2 Status/Control */

/* USBCMD bits */
#define UHCI_CMD_RS         0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET   0x0002  /* Host Controller Reset */
#define UHCI_CMD_GRESET    0x0004  /* Global Reset */
#define UHCI_CMD_EGSM      0x0008  /* Global Suspend Mode */
#define UHCI_CMD_FGR       0x0010  /* Force Global Resume */
#define UHCI_CMD_SWDBG     0x0020  /* Software Debug */
#define UHCI_CMD_CF        0x0040  /* Configure Flag */
#define UHCI_CMD_MAXP      0x0080  /* Max Packet (0=64, 1=32) */

/* USBSTS bits */
#define UHCI_STS_USBINT    0x0001  /* USB Interrupt */
#define UHCI_STS_ERROR     0x0002  /* USB Error Interrupt */
#define UHCI_STS_RESUME    0x0004  /* Resume Detect */
#define UHCI_STS_HSE       0x0008  /* Host System Error */
#define UHCI_STS_HCPE      0x0010  /* Host Controller Process Error */
#define UHCI_STS_HCH       0x0020  /* Host Controller Halted */

/* PORTSC bits */
#define UHCI_PORT_CCS      0x0001  /* Current Connect Status */
#define UHCI_PORT_CSC      0x0002  /* Connect Status Change */
#define UHCI_PORT_PE       0x0004  /* Port Enable */
#define UHCI_PORT_PEC      0x0008  /* Port Enable Change */
#define UHCI_PORT_DPLUS    0x0010  /* D+ Line Status */
#define UHCI_PORT_DMINUS   0x0020  /* D- Line Status */
#define UHCI_PORT_LSDA     0x0100  /* Low Speed Device Attached */
#define UHCI_PORT_PR       0x0200  /* Port Reset */
#define UHCI_PORT_SUSP     0x1000  /* Suspend */
#define UHCI_PORT_RD       0x2000  /* Resume Detect */

/* Transfer Descriptor (TD) */
typedef struct uhci_td {
    uint32_t link;         /* Link pointer */
    uint32_t status;       /* Status */
    uint32_t token;        /* Token */
    uint32_t buffer;       /* Buffer pointer */
} __attribute__((packed)) uhci_td_t;

/* Queue Head (QH) */
typedef struct uhci_qh {
    uint32_t link;         /* Link pointer */
    uint32_t element;      /* Queue element pointer */
} __attribute__((packed)) uhci_qh_t;

/* TD Status bits */
#define UHCI_TD_ACTIVE      0x80000000
#define UHCI_TD_STALLED     0x40000000
#define UHCI_TD_DATA_BUF    0x20000000
#define UHCI_TD_NAK         0x10000000
#define UHCI_TD_BABBLE      0x08000000
#define UHCI_TD_TIMEOUT     0x04000000
#define UHCI_TD_BITSTUFF    0x02000000
#define UHCI_TD_SPD         0x01000000
#define UHCI_TD_ACTLEN(x)   (((x) >> 21) & 0x7FF)

/* TD Token bits */
#define UHCI_TD_PID_OUT     0x00080000
#define UHCI_TD_PID_IN      0x00480000
#define UHCI_TD_PID_SETUP   0x00280000
#define UHCI_TD_IOC         0x01000000
#define UHCI_TD_IOS         0x02000000
#define UHCI_TD_LS          0x04000000

/* Initialize UHCI controller */
int uhci_init(uint16_t io_base);

/* Check if device connected */
int uhci_port_connected(int port);

/* Reset port */
int uhci_reset_port(int port);

/* Submit transfer */
int uhci_submit_td(uhci_td_t* td);

/* Create TD */
uhci_td_t* uhci_create_td(void);

/* Free TD */
void uhci_free_td(uhci_td_t* td);

#endif
