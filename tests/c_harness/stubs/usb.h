/* Host-harness shadow of src/platform/sh68f90a/usb.h — the real one
 * includes the SDCC sh68f90a.h register stack. The report.c path only
 * needs the protocol enum and the report-send prototypes (which the
 * harness captures via host.h, not usb.h). */
#ifndef HARNESS_USB_H
#define HARNESS_USB_H

#include "report.h"

#include <stdint.h>
#include <stdbool.h>

/* USB_PROTOCOL_* enum lives in harness_stubs.h (shared). */

void    usb_init(void);
void    usb_interrupt_handler(void);
void    usb_send_report(__xdata report_keyboard_t *report);
void    usb_send_nkro(__xdata report_nkro_t *report);
void    usb_send_extra(__xdata report_extra_t *report);
uint8_t usb_device_state_get_protocol(void);

#endif
