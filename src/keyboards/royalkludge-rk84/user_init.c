#include "kbdef.h"
#include "user_init.h"
#include "pwm.h"
#include "isp.h"
#include "delay.h"
#include <stdint.h>
#include <stdbool.h>

// =====================================================================
// RK84 user_init — recovery chord + full stock GPIO init + PWM init.
//
// Recovery contract (-R5):
// - user_init() runs BEFORE usb_init(); the Esc+Space chord check is
// the first thing here so a USB-init bug can never brick recovery.
// - The bootloader region (0xF000+) and the app reset-vector slot
// (0xEFFC) are NEVER touched by application code.
// - PWM starts at 0xC2 (scheduler on, RGB output select off); the
// first render phase enables outputs.
// =====================================================================

static bool recovery_chord_pressed_once(void);
static void user_gpio_init(void);
static void user_pwm_init(void);
static void rgb_clear_duty1(void);
static void rgb_clear_duty2(void);
static void rgb_blank(void);

void recovery_isp_check(void);

void user_init(void)
{
 recovery_isp_check();

 user_gpio_init();
 user_pwm_init();

 /*
 * PWM00CON is 0xC2: scheduler active, RGB output select disabled.
 * RGB render phases will enable outputs per-phase.
 */
 IEN1 |= _EPWM0;
}

// ---------------------------------------------------------------------
// Recovery chord ()
// ---------------------------------------------------------------------
static bool recovery_chord_pressed_once(void)
{
 bool esc;
 bool space;

 /* Rows as inputs with pull-ups. */
 P7CR &= (uint8_t)~RECOVERY_ROW_ESC_BIT;
 P5CR &= (uint8_t)~RECOVERY_ROW_SPACE_BIT;
 P7PCR |= RECOVERY_ROW_ESC_BIT;
 P5PCR |= RECOVERY_ROW_SPACE_BIT;

 /* Two columns as outputs, initially released HIGH. */
 P5CR |= RECOVERY_COL_ESC_BIT;
 P3CR |= RECOVERY_COL_SPACE_BIT;
 RECOVERY_COL_ESC = 1;
 RECOVERY_COL_SPACE = 1;

 /* Esc. */
 RECOVERY_COL_ESC = 0;
 delay_us(10);
 esc = !RECOVERY_ROW_ESC;
 RECOVERY_COL_ESC = 1;

 /* Space. */
 RECOVERY_COL_SPACE = 0;
 delay_us(10);
 space = !RECOVERY_ROW_SPACE;
 RECOVERY_COL_SPACE = 1;

 return esc && space;
}

void recovery_isp_check(void)
{
 /*
 * Require two positive samples 30 ms apart so a power-up transient
 * cannot enter ISP accidentally.
 */
 if (!recovery_chord_pressed_once()) {
 return;
 }

 delay_ms(30);

 if (recovery_chord_pressed_once()) {
 isp_jump();
 }
}

// ---------------------------------------------------------------------
// GPIO (stock GpioInit_Full the GPIO init values — radio pins in safe idle)
// ---------------------------------------------------------------------
static void user_gpio_init(void)
{
 /* Drive strength. */
 DRVCON = 0x05;
 P1DRV = 0x00;

 DRVCON = 0x45;
 P2DRV = 0x00;

 DRVCON = 0x85;
 P3DRV = 0x00;

 DRVCON = 0xC5;
 P5DRV = 0x00;

 /* Port direction. */
 P0CR = 0xBC;
 P1CR = 0x3F;
 P2CR = 0x3F;
 P3CR = 0x3F;
 P4CR = 0xFB;
 P5CR = 0x87;
 P6CR = 0xFF;
 P7CR = 0x50;

 /* Pull-ups. */
 P0PCR = 0x1C;
 P1PCR = 0x3F;
 P2PCR = 0x3F;
 P3PCR = 0x3F;
 P4PCR = 0x7B;
 P5PCR = 0xFF;
 P6PCR = 0xFF;
 P7PCR = 0xCF;

 /* Port data (columns released HIGH, radio pins idle). */
 P0 = 0x20;
 P1 = 0x3F;
 P2 = 0x3F;
 P3 = 0x3F;
 P4 = 0x80;
 P5 = 0x07;
 P6 = 0x00;
 P7 = 0x10;
}

// ---------------------------------------------------------------------
// PWM (stock: all groups 0x0A00, DUTY1/2 zero, blank, 0xC2 start)
// ---------------------------------------------------------------------
static void user_pwm_init(void)
{
 PWM0PERDH = 0x0A;
 PWM0PERDL = 0x00;

 PWM1PERDH = 0x0A;
 PWM1PERDL = 0x00;

 PWM2PERDH = 0x0A;
 PWM2PERDL = 0x00;

 PWM4PERDH = 0x0A;
 PWM4PERDL = 0x00;

 rgb_clear_duty1();
 rgb_clear_duty2();
 rgb_blank();

 PWM00CON = 0xC2;
}

// ---------------------------------------------------------------------
// RGB helpers (shared with indicators.c; derived)
// ---------------------------------------------------------------------
static void rgb_clear_duty1(void)
{
 /* PWM00-05 */
 PWM00DUTY1H = 0; PWM00DUTY1L = 0;
 PWM01DUTY1H = 0; PWM01DUTY1L = 0;
 PWM02DUTY1H = 0; PWM02DUTY1L = 0;
 PWM03DUTY1H = 0; PWM03DUTY1L = 0;
 PWM04DUTY1H = 0; PWM04DUTY1L = 0;
 PWM05DUTY1H = 0; PWM05DUTY1L = 0;

 /* PWM10-15 */
 PWM10DUTY1H = 0; PWM10DUTY1L = 0;
 PWM11DUTY1H = 0; PWM11DUTY1L = 0;
 PWM12DUTY1H = 0; PWM12DUTY1L = 0;
 PWM13DUTY1H = 0; PWM13DUTY1L = 0;
 PWM14DUTY1H = 0; PWM14DUTY1L = 0;
 PWM15DUTY1H = 0; PWM15DUTY1L = 0;

 /* PWM20-25 */
 PWM20DUTY1H = 0; PWM20DUTY1L = 0;
 PWM21DUTY1H = 0; PWM21DUTY1L = 0;
 PWM22DUTY1H = 0; PWM22DUTY1L = 0;
 PWM23DUTY1H = 0; PWM23DUTY1L = 0;
 PWM24DUTY1H = 0; PWM24DUTY1L = 0;
 PWM25DUTY1H = 0; PWM25DUTY1L = 0;

 /* PWM40-42 (direct SFR) */
 PWM40DUTY1H = 0; PWM40DUTY1L = 0;
 PWM41DUTY1H = 0; PWM41DUTY1L = 0;
 PWM42DUTY1H = 0; PWM42DUTY1L = 0;
}

static void rgb_clear_duty2(void)
{
 PWM00DUTY2H = 0; PWM00DUTY2L = 0;
 PWM01DUTY2H = 0; PWM01DUTY2L = 0;
 PWM02DUTY2H = 0; PWM02DUTY2L = 0;
 PWM03DUTY2H = 0; PWM03DUTY2L = 0;
 PWM04DUTY2H = 0; PWM04DUTY2L = 0;
 PWM05DUTY2H = 0; PWM05DUTY2L = 0;

 PWM10DUTY2H = 0; PWM10DUTY2L = 0;
 PWM11DUTY2H = 0; PWM11DUTY2L = 0;
 PWM12DUTY2H = 0; PWM12DUTY2L = 0;
 PWM13DUTY2H = 0; PWM13DUTY2L = 0;
 PWM14DUTY2H = 0; PWM14DUTY2L = 0;
 PWM15DUTY2H = 0; PWM15DUTY2L = 0;

 PWM20DUTY2H = 0; PWM20DUTY2L = 0;
 PWM21DUTY2H = 0; PWM21DUTY2L = 0;
 PWM22DUTY2H = 0; PWM22DUTY2L = 0;
 PWM23DUTY2H = 0; PWM23DUTY2L = 0;
 PWM24DUTY2H = 0; PWM24DUTY2L = 0;
 PWM25DUTY2H = 0; PWM25DUTY2L = 0;

 PWM40DUTY2H = 0; PWM40DUTY2L = 0;
 PWM41DUTY2H = 0; PWM41DUTY2L = 0;
 PWM42DUTY2H = 0; PWM42DUTY2L = 0;
}

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

 /* Shared PWM/matrix port groups LOW (pre-scan raises them). */
 P1 &= (uint8_t)0xC0;
 P2 &= (uint8_t)0xC0;
 P3 &= (uint8_t)0xC0;
 P5 &= (uint8_t)0xF8;
}
