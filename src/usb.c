#include "../include/usb.h"

static usb_device_t devices[16];
static int device_count = 0;

int usb_init(void) {
    device_count = 0;
    return 0;
}

int usb_enumerate(void) {
    return 0;
}

usb_device_t* usb_find_device(uint8_t dev_class) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].dev_class == dev_class) {
            return &devices[i];
        }
    }
    return 0;
}

int usb_control_transfer(usb_device_t* dev, usb_request_t* req, void* data, uint16_t length) {
    (void)dev; (void)req; (void)data; (void)length;
    return -1;
}

int usb_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint16_t length) {
    (void)dev; (void)endpoint; (void)data; (void)length;
    return -1;
}

int usb_get_device_count(void) {
    return device_count;
}

usb_device_t* usb_get_device(int index) {
    if (index >= 0 && index < device_count) {
        return &devices[index];
    }
    return 0;
}
