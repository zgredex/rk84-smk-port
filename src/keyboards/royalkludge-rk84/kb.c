#include <stdint.h>
#include <stdbool.h>
#include "kbdef.h"
#include "report.h"
#include "usb.h"
#include "host.h"
#include "keyboard.h"

// =====================================================================
// RK84 kb.c — board layer: report routing, lock LEDs, brightness,
// USB suspend/resume, remote wake, reset/config resynchronization.
//
// Stages:
//   recovery: no reports, no RGB, no scan (nothing here is used).
//   matrix:   reports via USB; RGB brightness is a stored no-op.
//   usb:      full wired keyboard — reports, host lock LEDs,
//             suspend/resume, remote wake. No RGB, no radio.
//   rgb:      matrix + static RGB; brightness drives the renderer.
//
// Stock Fn layer:
//   Fn+Up   -> keyboard RGB brightness up
//   Fn+Down -> keyboard RGB brightness down
// When RGB is disabled these update a stored value only and never
// drive any LED output (deliberate no-op for the usb stage).
//
// Suspend strategy: the matrix scan KEEPS RUNNING while suspended so
// key presses can be detected (required for remote wake). Power is
// saved by suppressing report transmission. The first physical key
// press with remote wake enabled asserts USBCON._WKUP exactly once.
//
// ISR contract: the USB hooks set single-bit flags ONLY. All report
// state mutation and USB transmission happen in the main loop via
// rk84_usb_mainloop_poll() (called from kb_update()).
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
// USB suspend / resume / reset / config + remote wake (wired only).
// =====================================================================
#if RK84_USB_FULL
#include "sh68f90a.h"

static __bit rk84_usb_suspended;
static __bit rk84_usb_wake_signalled;
static __bit rk84_usb_pending_wake;    /* physical key-down while suspended */
static __bit rk84_usb_resync_needed;   /* resume/reset/config: resend state */

/* ---- ISR hooks: flags only ---- */

void rk84_usb_suspend_hook(void)
{
    rk84_usb_suspended      = 1;
    rk84_usb_wake_signalled = 0;
    rk84_usb_pending_wake   = 0;
}

void rk84_usb_resume_hook(void)
{
    rk84_usb_suspended    = 0;
    rk84_usb_resync_needed = 1;
}

void rk84_usb_reset_hook(void)
{
    /* Bus reset / unplug / host reset: the host lost all report state.
     * Clear suspend so reports are not dropped; schedule a resend. */
    rk84_usb_suspended      = 0;
    rk84_usb_pending_wake   = 0;
    rk84_usb_wake_signalled = 0;
    rk84_usb_resync_needed  = 1;
}

void rk84_usb_config_hook(void)
{
    /* SET_CONFIGURATION(1): device (re)configured — resend full state. */
    rk84_usb_suspended      = 0;
    rk84_usb_resync_needed  = 1;
}
#endif /* RK84_USB_FULL — end of suspend-state block (hooks above) */

/* ---- report path (ALL stages) ---- */

/* While suspended (usb stage only), drop the report. Physical key-down
 * is recorded via rk84_usb_key_press() (matrix transitions), not by
 * scanning report bytes (an empty NKRO packet starts with report ID 6
 * and would look like a pressed key). */
void kb_send_report(__xdata report_keyboard_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        return;
    }
#endif
    usb_send_report(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        return;
    }
#endif
    usb_send_nkro(report);
}

/* Returns true while reports are dropped (USB suspended). host.c uses
 * this to keep its last-sent cache stale so extra reports resend. */
#if RK84_USB_FULL
bool rk84_usb_is_suspended(void)
{
    return rk84_usb_suspended;
}
#endif

void kb_send_extra(__xdata report_extra_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        return;   /* dropped; host.c keeps last_sent stale */
    }
#endif
    usb_send_extra(report);
}

/* ---- physical key transitions (from matrix.c via kb_process_record
 *      on the press edge) ---- */
#if RK84_USB_FULL
void rk84_usb_key_press(void)
{
    if (rk84_usb_suspended) {
        rk84_usb_pending_wake = 1;
    }
}

/* ---- main-loop poll (called from kb_update() every pass) ---- */

void rk84_usb_mainloop_poll(void)
{
    if (rk84_usb_resync_needed) {
        rk84_usb_resync_needed = 0;
        /* Force + send the full keyboard state (a key held across
         * suspend/reset produces no new matrix transition, so the
         * normal transition path would never transmit it). */
        report_force_resend();
        send_keyboard_report();
        /* Resend System/Consumer so a release dropped while suspended
         * is not lost on the host. */
        host_extra_resync();
    }

    /* Remote wake: first physical press while suspended + host enabled
     * -> assert the resume signal exactly once. */
    if (rk84_usb_suspended && !rk84_usb_wake_signalled &&
        rk84_usb_pending_wake && usb_remote_wakeup_enabled()) {
        USBCON |= _WKUP;
        rk84_usb_wake_signalled = 1;
    }
}

void kb_update(void)
{
    rk84_usb_mainloop_poll();
}
#endif /* RK84_USB_FULL */
