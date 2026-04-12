/*
 * usb.h - USB Host Controller Driver
 *
 * Supports UHCI (USB 1.1) and EHCI (USB 2.0) for keyboard input.
 * Most modern keyboards are USB HID devices.
 */

#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stddef.h>

/* USB speeds */
#define USB_SPEED_LOW      0
#define USB_SPEED_FULL     1
#define USB_SPEED_HIGH     2

/* USB transfer types */
#define USB_TRANSFER_CONTROL   0
#define USB_TRANSFER_ISO       1
#define USB_TRANSFER_BULK      2
#define USB_TRANSFER_INT       3

/* USB descriptor types */
#define USB_DESC_DEVICE        1
#define USB_DESC_CONFIG        2
#define USB_DESC_INTERFACE     4
#define USB_DESC_ENDPOINT      5
#define USB_DESC_HID           0x21
#define USB_DESC_HID_REPORT    0x22

/* USB Standard Requests */
#define USB_REQ_GET_STATUS     0x00
#define USB_REQ_CLEAR_FEATURE  0x01
#define USB_REQ_SET_FEATURE    0x03
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIG     0x08
#define USB_REQ_SET_CONFIG     0x09
#define USB_REQ_GET_INTERFACE  0x0A
#define USB_REQ_SET_INTERFACE  0x0B

/* HID class requests */
#define HID_REQ_GET_REPORT     0x01
#define HID_REQ_GET_IDLE       0x02
#define HID_REQ_GET_PROTOCOL   0x03
#define HID_REQ_SET_REPORT     0x09
#define HID_REQ_SET_IDLE       0x0A
#define HID_REQ_SET_PROTOCOL   0x0B

/* Endpoint directions */
#define USB_ENDPOINT_IN        0x80
#define USB_ENDPOINT_OUT       0x00

/* USB Device */
typedef struct usb_device {
    uint8_t  address;
    uint8_t  speed;
    uint8_t  config;
    uint8_t  interface;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    uint8_t  max_packet_size;
    uint8_t  endpoints;
    uint8_t  endpoint_in;
    uint8_t  endpoint_out;
} usb_device_t;

/* USB Request (Setup Packet) */
typedef struct {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed)) usb_request_t;

/* USB Device Descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint16_t usb_version;
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    uint8_t  max_packet_size0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t  manufacturer_idx;
    uint8_t  product_idx;
    uint8_t  serial_idx;
    uint8_t  num_configs;
} __attribute__((packed)) usb_device_desc_t;

/* USB Configuration Descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint16_t total_length;
    uint8_t  num_interfaces;
    uint8_t  config_value;
    uint8_t  config_idx;
    uint8_t  attributes;
    uint8_t  max_power;
} __attribute__((packed)) usb_config_desc_t;

/* USB Interface Descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint8_t  interface_num;
    uint8_t  alternate;
    uint8_t  num_endpoints;
    uint8_t  iface_class;
    uint8_t  iface_subclass;
    uint8_t  iface_protocol;
    uint8_t  iface_idx;
} __attribute__((packed)) usb_interface_desc_t;

/* USB Endpoint Descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint8_t  address;
    uint8_t  attributes;
    uint16_t max_packet_size;
    uint8_t  interval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* HID Descriptor */
typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint16_t hid_version;
    uint8_t  country_code;
    uint8_t  num_descriptors;
    uint8_t  report_type;
    uint16_t report_length;
} __attribute__((packed)) usb_hid_desc_t;

/* Initialize USB subsystem */
int usb_init(void);

/* Find all USB devices */
int usb_enumerate(void);

/* Find device by class (e.g., HID keyboard) */
usb_device_t* usb_find_device(uint8_t dev_class);

/* Control transfer */
int usb_control_transfer(usb_device_t* dev, usb_request_t* req, 
                          void* data, uint16_t length);

/* Interrupt transfer (for HID devices) */
int usb_interrupt_transfer(usb_device_t* dev, uint8_t endpoint,
                            void* data, uint16_t length);

/* Get device count */
int usb_get_device_count(void);

/* Get device by index */
usb_device_t* usb_get_device(int index);

#endif
