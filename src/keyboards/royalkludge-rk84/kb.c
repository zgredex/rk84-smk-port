#include <stdint.h>
#include <stdbool.h>
#include "report.h"
#include "usb.h"
#include "keyboard.h"
#include "kbdef.h"

// =====================================================================
// RK84 kb.c — board layer: report routing, lock LEDs, brightness.
//
// Stages:
//   recovery: no reports, no RGB, no scan (nothing here is used).
//   matrix:   reports via USB; RGB brightness is a stored no-op
//             (RGB outputs are never driven).
//   usb:      full wired keyboard — reports, host lock LEDs,
//             suspend/resume state. No RGB, no radio.
//   rgb:      matrix + static RGB; brightness drives the renderer.
//
// Stock Fn layer:
//   Fn+Up   -> keyboard RGB brightness up
//   Fn+Down -> keyboard RGB brightness down
// When RGB is disabled these update a stored value only and never
// drive any LED output (deliberate no-op for the usb stage).
// =====================================================================

extern void indicators_brightness_up(void);
extern void indicators_brightness_down(void);

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

// =====================================================================
// Host lock-LED state (EP0 SET_REPORT -> keyboard_state.led_state)
// =====================================================================
#if RK84_USB_FULL
uint8_t rk84_usb_led_state(void)
{
    return keyboard_state.led_state;
}

bool rk84_usb_num_lock(void)
{
    return (keyboard_state.led_state & 0x01) != 0;
}

bool rk84_usb_caps_lock(void)
{
    return (keyboard_state.led_state & 0x02) != 0;
}

bool rk84_usb_scroll_lock(void)
{
    return (keyboard_state.led_state & 0x04) != 0;
}
#endif /* RK84_USB_FULL */

// =====================================================================
// USB suspend / resume (wired only).
//
// Suspend: stop the PWM-driven matrix scan (EPWM0 off) to cut power.
// Resume:  restart the scan and force a full state re-send so held
//          keys do not produce stale reports after resume.
// The hooks are weak in usb.c; these strong definitions override them.
// =====================================================================
#if RK84_USB_FULL
#include "sh68f90a.h"

void rk84_usb_suspend_hook(void)
{
    /* Stop the scan scheduler (PWM0 interrupt drives one matrix
     * column per tick). All GPIO stays at idle. */
    IEN1 &= (uint8_t)~_EPWM0;
}

void rk84_usb_resume_hook(void)
{
    /* Restart the scheduler. The matrix task re-reads the full
     * switch state on the next loop pass, so the next report is the
     * complete current state (no stale held-key gaps). */
    IEN1 |= _EPWM0;
}
#endif /* RK84_USB_FULL */
