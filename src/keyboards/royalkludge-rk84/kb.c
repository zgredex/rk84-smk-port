#include <stdint.h>
#include <stdbool.h>
#include "kbdef.h"
#include "report.h"
#include "usb.h"
#include "keyboard.h"

// =====================================================================
// RK84 kb.c — board layer: report routing, lock LEDs, brightness,
// USB suspend/resume and host-gated remote wake.
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
// USB suspend / resume and host-gated remote wake (wired only).
//
// Suspend strategy: the matrix scan KEEPS RUNNING while suspended so
// key presses can be detected (required for remote wake). Power is
// saved by suppressing report transmission — nothing is sent to the
// host while suspended. The first press with remote wake enabled
// asserts the USB resume signal (USBCON._WKUP) exactly once.
//
// Resume (host-driven): force a full report re-send (poison duplicate
// suppression) so a key held across suspend is re-delivered, then
// re-enable report transmission.
//
// The suspend hook deliberately does NOT stop EPWM0: with the scan
// stopped, no key press could ever be observed and remote wake would
// be dead. Stopping the scan is only correct for deep sleep (Stage 5
// wireless territory), not for USB remote-wake operation.
// =====================================================================
#if RK84_USB_FULL
#include "sh68f90a.h"

static __bit rk84_usb_suspended;
static __bit rk84_usb_wake_signalled;
static __bit rk84_usb_pending_wake;

void rk84_usb_suspend_hook(void)
{
    /* Stay in the low-activity state: scan continues (key detection),
     * reports suppressed. Remote wake may fire on the first press. */
    rk84_usb_suspended      = 1;
    rk84_usb_wake_signalled = 0;
    rk84_usb_pending_wake   = 0;
}

void rk84_usb_resume_hook(void)
{
    /* Host-driven resume (RESMIF): force the next report to transmit
     * even if it matches the last sent one, so a key held across
     * suspend is re-delivered. Re-enable report transmission. */
    report_force_resend();
    rk84_usb_suspended = 0;
}

/* Report path: while suspended, drop the report but remember a key is
 * pressed so kb_update_switches() can issue remote wake. */
static void rk84_usb_gate_report(const uint8_t *report, uint8_t len)
{
    uint8_t i;
    bool any_pressed = false;

    if (!rk84_usb_suspended) {
        return;
    }
    for (i = 0; i < len; ++i) {
        if (report[i] != 0) {
            any_pressed = true;
            break;
        }
    }
    if (any_pressed) {
        rk84_usb_pending_wake = 1;
    }
}
#endif /* RK84_USB_FULL — end of suspend state + hooks */

// =====================================================================
// Report routing (ALL stages). USB_full additionally gates reports
// while suspended (see above).
// =====================================================================
void kb_send_report(__xdata report_keyboard_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        rk84_usb_gate_report(report->raw, sizeof(report_keyboard_t));
        return;
    }
#endif
    usb_send_report(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        rk84_usb_gate_report(report->raw, sizeof(report_nkro_t));
        return;
    }
#endif
    usb_send_nkro(report);
}

void kb_send_extra(__xdata report_extra_t *report)
{
#if RK84_USB_FULL
    if (rk84_usb_suspended) {
        return;  /* no Consumer/System traffic while suspended */
    }
#endif
    usb_send_extra(report);
}

// =====================================================================
// Remote wake (USB_full only)
// =====================================================================
#if RK84_USB_FULL
/* Called from kb_update_switches() every main-loop pass. Issues the
 * USB resume signal on the first key press observed while suspended,
 * only if the host enabled remote wakeup. Signalled exactly once. */
void rk84_usb_wake_on_key(void)
{
    if (!rk84_usb_suspended || rk84_usb_wake_signalled) {
        return;
    }
    if (!rk84_usb_pending_wake) {
        return;
    }
    if (!usb_remote_wakeup_enabled()) {
        /* Host did not enable remote wake: no resume signal. The key
         * press stays gated until the host resumes the bus itself. */
        return;
    }
    /* First press while suspended + host enabled -> assert resume. */
    USBCON |= _WKUP;
    rk84_usb_wake_signalled = 1;
}

/* Board-level switch polling: called from the main loop. The matrix
 * scan keeps detecting presses while suspended; the report gate above
 * records them and this function turns the first one into a wake. */
void kb_update_switches(void)
{
    rk84_usb_wake_on_key();
}
#endif /* RK84_USB_FULL */
