#include "kbdef.h"
#include "user_matrix.h"
#include "delay.h"
#include <stdint.h>

// =====================================================================
// RK84 matrix — one column per PWM interrupt.
//
// Stock per-column sequence:
// release all columns HIGH
// selected column LOW
// DelayUnits(2) ~= 3 us
// sample A
// DelayUnits(3) ~= 4.5 us
// sample B
// if A != B: accept no keys
// release all columns HIGH
// DelayUnits(3)
//
// SMK hook mapping:
// user_matrix_pre_scan : raise all cols HIGH, lower selected, settle
// user_matrix_scan_col : sample A, delay, sample B, return equal or 0xFF
// user_matrix_post_scan: raise all cols HIGH, post-release delay
//
// Polarity: hook returns raw electrical levels (1 = released, 0 =
// pressed). SMK does matrix[col] = ~column_state. On mismatch return
// 0xFF so ~0xFF = 0 pressed bits (returning 0 would invert to all
// pressed).
//
// CRITICAL: RK84 strobes idle HIGH (stock ColumnsReleaseHigh the release routine).
// The NuPhy target releases LOW — this board must do the opposite.
// =====================================================================

#define MATRIX_COL_MASK_P1 0x3F
#define MATRIX_COL_MASK_P2 0x3F
#define MATRIX_COL_MASK_P3 0x3F
#define MATRIX_COL_MASK_P5 0x07

static void columns_high(void)
{
 /*
 * Stock ColumnsReleaseHigh the release routine raises the complete strobe
 * groups. P1.0-P1.4 are RGB PWM columns, not switch columns, but
 * stock raises the full six-bit group.
 */
 P1 |= MATRIX_COL_MASK_P1;
 P2 |= MATRIX_COL_MASK_P2;
 P3 |= MATRIX_COL_MASK_P3;
 P5 |= MATRIX_COL_MASK_P5;
}

static uint8_t rows_read_levels(void)
{
 /*
 * r0=P7.0 r1=P7.1 r2=P7.2 r3=P7.3 r4=P5.3 r5=P5.4
 * 1 = released, 0 = pressed (electrical levels).
 */
 return (uint8_t)(
 (P7 & 0x0F) |
 ((P5 & 0x18) << 1) |
 0xC0
 );
}

void user_matrix_pre_scan(uint8_t col)
{
 columns_high();

 switch (col) {
 case 0: P5_0 = 0; break;
 case 1: P5_1 = 0; break;
 case 2: P5_2 = 0; break;
 case 3: P3_5 = 0; break;
 case 4: P3_4 = 0; break;
 case 5: P3_3 = 0; break;
 case 6: P3_2 = 0; break;
 case 7: P3_1 = 0; break;
 case 8: P3_0 = 0; break;
 case 9: P2_5 = 0; break;
 case 10: P2_4 = 0; break;
 case 11: P2_3 = 0; break;
 case 12: P2_2 = 0; break;
 case 13: P2_1 = 0; break;
 case 14: P2_0 = 0; break;
 case 15: P1_5 = 0; break;
 }

 /*
 * Stock DelayUnits(2) ~= 3 us excluding call overhead.
 * delay_us(3) is the closest maintainable conservative value.
 */
 delay_us(3);
}

uint8_t user_matrix_scan_col(uint8_t col)
{
 uint8_t first;
 uint8_t second;

 (void)col;

 first = rows_read_levels();

 /*
 * Stock DelayUnits(3) ~= 4.5 us. Round upward.
 */
 delay_us(5);

 second = rows_read_levels();

 if (first != second) {
 return 0xFF;
 }

 return first;
}

void user_matrix_post_scan(void)
{
 columns_high();
 delay_us(5);
}
