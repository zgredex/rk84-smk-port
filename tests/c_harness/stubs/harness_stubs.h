/* Host-harness stubs for SDCC/SMK framework headers that report.c
 * includes but does not use in the RK84_STOCK_REPORTS path.
 *
 * `__xdata` is defined as empty so the REAL report.h / report.c
 * compile unchanged on the host compiler. */
#ifndef HARNESS_STUBS_H
#define HARNESS_STUBS_H

/* SDCC memory-space keywords are no-ops on the host. */
#ifndef __SDCC
#define __xdata
#define __idata
#define __code
#define __bit unsigned char
#define __interrupt(x)
#define __reentrant
#endif

#include <stdint.h>

/* --- layout.h --- */
extern const uint16_t keymaps[][6][16];

/* --- kb.h --- */
#include <stdbool.h>
bool kb_process_record(uint16_t keycode, bool key_pressed);

/* --- keyboard.h (shadowed by stubs/keyboard.h) --- */

/* --- usb.h (shadowed by stubs/usb.h; enum shared here) --- */
enum {
    USB_PROTOCOL_BOOT   = 0,
    USB_PROTOCOL_REPORT = 1,
};
uint8_t usb_device_state_get_protocol(void);

/* --- debug.h --- */
#ifndef DEBUG
#define DEBUG 0
#endif
/* The host's stdio.h already declares a real dprintf() function, and
 * SMK's report.c only uses the dprintf() MACRO in non-RK84_STOCK_REPORTS
 * paths that are compiled out here — so no macro is defined. */

#endif
