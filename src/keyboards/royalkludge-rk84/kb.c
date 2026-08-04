#include <stdint.h>
#include <stdbool.h>
#include "report.h"
#include "usb.h"
#include "kbdef.h"

// =====================================================================
// RK84 kb.c — RGB brightness controls only (Milestone 1: static RGB).
//
// Stock Fn layer:
//   Fn+Up   -> keyboard RGB brightness up
//   Fn+Down -> keyboard RGB brightness down
// Milestone 1 has no effects, so speed controls are intentionally
// omitted (no-op if the keycode is ever used).
// =====================================================================

extern void indicators_brightness_up();
extern void indicators_brightness_down();

bool kb_process_record(uint16_t keycode, bool key_pressed)
{
 switch (keycode) {
 case RGB_BRI_UP:
 if (key_pressed) {
 indicators_brightness_up();
 }
 return false;
 case RGB_BRI_DN:
 if (key_pressed) {
 indicators_brightness_down();
 }
 return false;
 default:
 /* All other keycodes pass through to the report layer. */
 return true;
 }
}

void kb_send_report(__xdata report_keyboard_t *report)
{
 usb_send_report(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
 usb_send_nkro(report);
}

void kb_send_extra(__xdata report_extra_t *report)
{
 usb_send_extra(report);
}
