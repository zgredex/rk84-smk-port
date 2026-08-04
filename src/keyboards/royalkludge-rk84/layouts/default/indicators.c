#include "kbdef.h"
#include "keyboard.h"
#include "indicators.h"
#include "delay.h"
#include <stdint.h>
#include <stdbool.h>

// =====================================================================
// RK84 indicators — 19-phase static RGB renderer ( derived).
//
// Schedule per PWM interrupt:
// rgb_phase 0 : matrix scan only, PWM00CON = 0xC2 (outputs off)
// rgb_phase 1-18: matrix scan, then write 21 DUTY2 pairs, enable PWM
// groups, raise ONE sink (row/component)
//
// Phase -> (row, component): (phase-1)/3 = row, (phase-1)%3 = component.
// row base = row * 21; PWM channel = logical - base (0-20).
//
// Duty = source * brightness(0-5) * 2, max 2550 < period 2560.
//
// The physical R/G/B order of component 0/1/2 is underived — component
// 0 defaults to full so the first flash identifies the color (then the
// array names can be corrected).
// =====================================================================

#if RK84_RGB_ENABLE
/* RGB framebuffer: 3 planes x 126 LEDs. Only allocated when RGB is
 * enabled — the usb stage claims "no framebuffer" and must not spend
 * 378 bytes of XRAM on one. */
static __xdata uint8_t rgb_plane[3][RGB_ROWS * RGB_COLS];
#endif
static __xdata uint8_t rgb_phase;
static __xdata uint8_t rgb_brightness;

#if RK84_RGB_ENABLE
static void rgb_fill_static(uint8_t c0, uint8_t c1, uint8_t c2);
static void rgb_blank(void);
static void rgb_pwm_enable(void);
static void rgb_sink_enable(uint8_t phase);
static void rgb_write_duty2(uint8_t col, uint16_t duty);
#endif
static uint16_t rgb_duty(uint8_t source);

// ---------------------------------------------------------------------
// Start — called once from main() before the main loop
// ---------------------------------------------------------------------
void indicators_start(void)
{
#if RK84_RECOVERY_ONLY
    /*
     * Recovery-only image:
     * - do not enable PWM0;
     * - do not enable the matrix scanner;
     * - leave all RGB sinks and shared outputs in the safe GPIO
     *   state installed by user_gpio_init().
     */
    return;
#else
    rgb_phase      = 0;
    rgb_brightness = 1;

#if RK84_RGB_ENABLE
    /*
     * Safe RGB bring-up: everything off except ONE logical LED
     * (row 0, column 0) at low intensity, cycling the three
     * components so the physical R/G/B order is confirmed before
     * any full-grid lighting. Duty = 64 * 1 * 2 = 128
     * (5% of the 2560-count period). Each component ~1.2 s,
     * looping forever so the sequence is observable at any time.
     *
     * ORDER MATTERS: the PWM scheduler (started below) must be
     * running BEFORE the plane writes, because the per-phase render
     * (indicators_update_step) only executes while the scheduler
     * ticks. Writing the plane first and cycling before the start
     * produces no light.
     */
#else
    /* Matrix-only image: no RGB outputs or framebuffer. */
#endif

    /*
     * Start the PWM scheduler: it drives the matrix scan (one column
     * per interrupt). RGB render phases are handled by
     * indicators_update_step() only when RK84_RGB_ENABLE.
     */
    PWM00CON = 0xC2;
    IEN1 |= _EPWM0;

#if RK84_RGB_ENABLE
    /* Component-order cycle, NOW that the scheduler renders.
     * LOOPS forever so the sequence is observable at any time
     * (a one-shot probe finishes before the user looks). */
    while (1) {
        rgb_fill_static(0, 0, 0);
        rgb_plane[0][0] = 64;      /* component 0 */
        delay_ms(4000);
        rgb_fill_static(0, 0, 0);
        rgb_plane[1][0] = 64;      /* component 1 */
        delay_ms(4000);
        rgb_fill_static(0, 0, 0);
        rgb_plane[2][0] = 64;      /* component 2 */
        delay_ms(4000);
    }
#endif
#endif /* RK84_RECOVERY_ONLY */
}

// ---------------------------------------------------------------------
// Per-interrupt hooks
// ---------------------------------------------------------------------
void indicators_pre_update(void)
{
#if RK84_RECOVERY_ONLY
    return;
#elif RK84_RGB_ENABLE
    /*
     * Blank sinks + disable PWM outputs so duty changes are not
     * visible mid-update.
     */
    rgb_blank();
#else
    /* Matrix-only: no RGB outputs to blank. */
#endif
}

bool indicators_update_step(
    keyboard_state_t *keyboard,
    uint8_t matrix_col
)
{
    (void)keyboard;
    (void)matrix_col;

#if RK84_RECOVERY_ONLY
    return false;
#else
    if (rgb_phase == 0) {
        PWM00CON = 0xC2;
    } else {
#if RK84_RGB_ENABLE
        uint8_t row       = (uint8_t)((rgb_phase - 1) / 3);
        uint8_t component = (uint8_t)((rgb_phase - 1) % 3);
        uint8_t base      = (uint8_t)(row * RGB_COLS);

        for (uint8_t col = 0; col < RGB_COLS; col++) {
            uint8_t source = rgb_plane[component][base + col];
            rgb_write_duty2(col, rgb_duty(source));
        }

        rgb_pwm_enable();
        rgb_sink_enable(rgb_phase);
#else
        /* Matrix-only image: no RGB output. */
        PWM00CON = 0xC2;
#endif
    }

    rgb_phase++;
    if (rgb_phase >= RGB_PHASES) {
        rgb_phase = 0;
    }

    return false;
#endif /* RK84_RECOVERY_ONLY */
}

void indicators_post_update(void)
{
#if RK84_RECOVERY_ONLY
    return;
#else
    /* Clear the PWM interrupt flag (bit 5 of PWM00CON). */
    PWM00CON &= (uint8_t)~0x20;
#endif
}

// ---------------------------------------------------------------------
// Brightness (stock range 0-5; no persistence in Milestone 1)
// ---------------------------------------------------------------------
void indicators_brightness_up(void)
{
 if (rgb_brightness < RGB_BRIGHTNESS_MAX) {
 rgb_brightness++;
 }
}

void indicators_brightness_down(void)
{
 if (rgb_brightness > 0) {
 rgb_brightness--;
 }
}

// ---------------------------------------------------------------------
// RGB renderer helpers (only compiled when RGB is enabled)
// ---------------------------------------------------------------------
#if RK84_RGB_ENABLE
static void rgb_fill_static(uint8_t c0, uint8_t c1, uint8_t c2)
{
 for (uint8_t i = 0; i < RGB_ROWS * RGB_COLS; i++) {
 rgb_plane[0][i] = c0;
 rgb_plane[1][i] = c1;
 rgb_plane[2][i] = c2;
 }
}

// ---------------------------------------------------------------------
// Duty math: duty = source * brightness * 2 (max 2550)
// ---------------------------------------------------------------------
static uint16_t rgb_duty(uint8_t source)
{
 return ((uint16_t)source * rgb_brightness) << 1;
}

// ---------------------------------------------------------------------
// DUTY2 writer — 21 renderer columns 
// ---------------------------------------------------------------------
static void rgb_write_duty2(uint8_t col, uint16_t duty)
{
 uint8_t high = (uint8_t)(duty >> 8);
 uint8_t low = (uint8_t)duty;

 switch (col) {
 case 0: PWM40DUTY2H = high; PWM40DUTY2L = low; break;
 case 1: PWM41DUTY2H = high; PWM41DUTY2L = low; break;
 case 2: PWM42DUTY2H = high; PWM42DUTY2L = low; break;
 case 3: PWM05DUTY2H = high; PWM05DUTY2L = low; break;
 case 4: PWM04DUTY2H = high; PWM04DUTY2L = low; break;
 case 5: PWM03DUTY2H = high; PWM03DUTY2L = low; break;
 case 6: PWM02DUTY2H = high; PWM02DUTY2L = low; break;
 case 7: PWM01DUTY2H = high; PWM01DUTY2L = low; break;
 case 8: PWM00DUTY2H = high; PWM00DUTY2L = low; break;
 case 9: PWM15DUTY2H = high; PWM15DUTY2L = low; break;
 case 10: PWM14DUTY2H = high; PWM14DUTY2L = low; break;
 case 11: PWM13DUTY2H = high; PWM13DUTY2L = low; break;
 case 12: PWM12DUTY2H = high; PWM12DUTY2L = low; break;
 case 13: PWM11DUTY2H = high; PWM11DUTY2L = low; break;
 case 14: PWM10DUTY2H = high; PWM10DUTY2L = low; break;
 case 15: PWM25DUTY2H = high; PWM25DUTY2L = low; break;
 case 16: PWM24DUTY2H = high; PWM24DUTY2L = low; break;
 case 17: PWM23DUTY2H = high; PWM23DUTY2L = low; break;
 case 18: PWM22DUTY2H = high; PWM22DUTY2L = low; break;
 case 19: PWM21DUTY2H = high; PWM21DUTY2L = low; break;
 case 20: PWM20DUTY2H = high; PWM20DUTY2L = low; break;
 }
}

// ---------------------------------------------------------------------
// PWM group enable
// ---------------------------------------------------------------------
static void rgb_pwm_enable(void)
{
 PWM00CON = 0xCA;
 PWM01CON = 0x08;
 PWM02CON = 0x08;
 PWM03CON = 0x08;
 PWM04CON = 0x08;
 PWM05CON = 0x08;

 PWM10CON = 0x8A;
 PWM11CON = 0x08;
 PWM12CON = 0x08;
 PWM13CON = 0x08;
 PWM14CON = 0x08;
 PWM15CON = 0x08;

 PWM20CON = 0x8A;
 PWM21CON = 0x08;
 PWM22CON = 0x08;
 PWM23CON = 0x08;
 PWM24CON = 0x08;
 PWM25CON = 0x08;

 PWM40CON = 0x8A;
 PWM41CON = 0x08;
 PWM42CON = 0x08;
}

// ---------------------------------------------------------------------
// Sink select — phase 1-18 -> one pin
// ---------------------------------------------------------------------
static void rgb_sink_enable(uint8_t phase)
{
 switch (phase) {
 case 1: P4_1 = 1; break;
 case 2: P6_0 = 1; break;
 case 3: P4_0 = 1; break;
 case 4: P6_1 = 1; break;
 case 5: P0_4 = 1; break;
 case 6: P0_3 = 1; break;
 case 7: P6_2 = 1; break;
 case 8: P6_7 = 1; break;
 case 9: P6_6 = 1; break;
 case 10: P6_3 = 1; break;
 case 11: P0_2 = 1; break;
 case 12: P5_7 = 1; break;
 case 13: P6_4 = 1; break;
 case 14: P4_5 = 1; break;
 case 15: P4_6 = 1; break;
 case 16: P6_5 = 1; break;
 case 17: P4_3 = 1; break;
 case 18: P4_4 = 1; break;
 }
}

// ---------------------------------------------------------------------
// Blank — sinks off + PWM outputs disabled (stock the blank routine)
// ---------------------------------------------------------------------
static void rgb_blank(void)
{
 /* All sinks off. */
 P0 &= (uint8_t)~0x1C; /* P0.2 P0.3 P0.4 */
 P4 &= (uint8_t)~0x7B; /* P4.0 P4.1 P4.3 P4.4 P4.5 P4.6 */
 P5 &= (uint8_t)~0x80; /* P5.7 */
 P6 = 0x00; /* P6.0-P6.7 */

 /* Disable PWM group outputs. */
 PWM00CON = 0x02;
 PWM01CON = 0x00;
 PWM02CON = 0x00;
 PWM03CON = 0x00;
 PWM04CON = 0x00;
 PWM05CON = 0x00;

 PWM10CON = 0x02;
 PWM11CON = 0x00;
 PWM12CON = 0x00;
 PWM13CON = 0x00;
 PWM14CON = 0x00;
 PWM15CON = 0x00;

 PWM20CON = 0x02;
 PWM21CON = 0x00;
 PWM22CON = 0x00;
 PWM23CON = 0x00;
 PWM24CON = 0x00;
 PWM25CON = 0x00;

 PWM40CON = 0x02;
 PWM41CON = 0x00;
 PWM42CON = 0x00;

 /* Shared PWM/matrix port groups LOW (matrix pre-scan raises). */
 P1 &= (uint8_t)0xC0;
 P2 &= (uint8_t)0xC0;
 P3 &= (uint8_t)0xC0;
 P5 &= (uint8_t)0xF8;
}
#endif /* RK84_RGB_ENABLE */
