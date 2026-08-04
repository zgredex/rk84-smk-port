#pragma once

#include "sh68f90a.h"
#include "report.h"
#include <stdint.h>

/* Board-configurable USB identity (meson cc_args; defaults = SMK). */
#ifndef USB_BCD_DEVICE
#define USB_BCD_DEVICE 0x0000
#endif
#ifndef USB_EP1_MPS
#define USB_EP1_MPS 16
#endif
#ifndef USB_EP2_MPS
#define USB_EP2_MPS 64
#endif
#ifndef USB_SERIAL_INDEX
#define USB_SERIAL_INDEX USB_STRING_SERIAL_NUMBER
#endif
#ifndef USB_STRING_MANUFACTURER_VALUE
#define USB_STRING_MANUFACTURER_VALUE "contact@carlossless.io"
#endif
#ifndef USB_STRING_PRODUCT_VALUE
#define USB_STRING_PRODUCT_VALUE "SMK Keyboard"
#endif
#ifndef USB_STRING_SERIAL_VALUE
#define USB_STRING_SERIAL_VALUE "0001"
#endif

enum {
    USB_PROTOCOL_BOOT   = 0,
    USB_PROTOCOL_REPORT = 1,
};

void    usb_init();
void    usb_send_report(__xdata report_keyboard_t *report);
void    usb_send_nkro(__xdata report_nkro_t *report);
void    usb_send_extra(__xdata report_extra_t *report);
uint8_t usb_device_state_get_protocol();

#if DEBUG == 1
bool usb_is_configured();
#endif // DEBUG

void usb_interrupt_handler() __interrupt(_INT_USB);
