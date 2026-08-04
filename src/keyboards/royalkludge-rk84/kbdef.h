#pragma once

#include "sh68f90a.h"
#include "keycodes.h"

// =====================================================================
// RK84 (SH68F90A) — pinout and timing derived from the factory
// firmware's runtime behavior.
//
// MATRIX: 16 cols x 6 rows, active-low strobes released HIGH.
// cols c0=P5.0 c1=P5.1 c2=P5.2 c3=P3.5 c4=P3.4 c5=P3.3 c6=P3.2
// c7=P3.1 c8=P3.0 c9=P2.5 c10=P2.4 c11=P2.3 c12=P2.2
// c13=P2.1 c14=P2.0 c15=P1.5
// rows r0=P7.0 r1=P7.1 r2=P7.2 r3=P7.3 r4=P5.3 r5=P5.4
// read = (P7 & 0x0F) | ((P5 & 0x18) << 1); pull-ups
// P7PCR|=0x0F, P5PCR|=0x18.
// timing: strobe LOW -> delay 2 (~3us) -> sample A -> delay 3
// (~4.5us) -> sample B -> accept if equal -> release HIGH
// -> delay 3.
// KEYMAP: default 84-key layout seeded from the factory's default
// usage table (126-byte) + 7 modifier fields (field = USB modifier
// bit). Fn = c9r5 hard-coded in the Fn detector. RCtrl = c10r5
// (USB RCtrl bit). c12r3 = ANSI phantom KC_NO.
// RGB: 21 PWM column channels x 18 sink phases (6 rows x 3 comp).
// DUTY2 registers; duty = source * brightness(0-5) * 2,
// max 2550 < period 2560. 19-phase schedule (0 = matrix only).
// MODE: P5.5 = wired/radio hardware switch (SMK policy: stable high
// -> wired; stock gates via one-shot bit 0x28 — simplification);
// P5.6 = wireless select (low -> 2.4G, high -> remembered BT).
// SLEEP: SFR 0x8E=0x55 then PCON|=0x02; wake via EX4 (radio) / USB
// (wired). Timer2 NOT a wake source.
// =====================================================================

#define MATRIX_ROWS 6
#define MATRIX_COLS 16

// ---------------------------------------------------------------------
// Mode switch pins (NOT IMPLEMENTED in Milestone 1)
// ---------------------------------------------------------------------
// P5.5/P5.6 are the factory's wired/wireless selection inputs.
// Milestone 1 ALWAYS uses USB regardless of physical switch position:
// the mode state machine is not implemented in this milestone.
// P5.5 = wired/radio hardware switch. STOCK gates the wired transition
// behind an unresolved one-shot bit (0x28) — using "P5.5 stable high ->
// wired" directly is a deliberate SMK board policy, not byte-exact
// stock behavior. Debounce with the same 16ms two-sample scheme as P5.6.
// P5.6 = wireless selection (low -> 2.4G profile 0, high -> remembered
//        BT slot 1-3). Two samples per 34-step cycle (~544ms): sample at
//        step 20, 30, decide at 31.
#define MODE_SWITCH_PIN P5_5
#define MODE_SWITCH_PIN_BIT _P5_5
#define WIRELESS_SELECT_PIN P5_6

// ---------------------------------------------------------------------
// RGB — 21 PWM column channels x 18 direct sink phases. DUTY2 registers
// ---------------------------------------------------------------------
// Renderer columns map to PWM channels:
// ch0 = PWM40 (SFR), ch1 = PWM41, ch2 = PWM42
// ch3 = PWM05... ch8 = PWM00 (SFRX 0xFFED-0xFFE8 / 0xFFD5-0xFFD0)
// ch9 = PWM15... ch14 = PWM10 (SFRX 0xFFF3-0xFFEE / 0xFFDB-0xFFD6)
// ch15 = PWM25... ch20 = PWM20 (SFRX 0xFFF9-0xFFF4 / 0xFFE1-0xFFDC)
// Sinks (phase 1-18): r0: P4.1/P6.0/P4.0, r1: P6.1/P0.4/P0.3,
// r2: P6.2/P6.7/P6.6, r3: P6.3/P0.2/P5.7, r4: P6.4/P4.5/P4.6,
// r5: P6.5/P4.3/P4.4. Phase 0 = all sinks off.
#define RGB_ROWS 6
#define RGB_COLS 21
#define RGB_PHASES 19
#define RGB_BRIGHTNESS_MAX 5

// PWM control addresses (SFRX)
// PWM00-05 CON: 0xFF80-0xFF85; PWM10-15: 0xFF86-0xFF8B;
// PWM20-25: 0xFF8C-0xFF91; PWM40-42: SFR 0xDA-0xDC.
// Period: all groups 0x0A00 (PWM0PERD 0xFF98/0xFF9C, PWM1 0xFF99/0xFF9D,
// PWM2 0xFF9A/0xFF9E, PWM4 SFR 0xDD/0xDE) = 2560 counts @ /4 = 426.7us.

// ---------------------------------------------------------------------
// Recovery chord (): Esc + Space held at power-up -> isp_jump
// Esc: col 0 = P5.0, row 0 = P7.0
// Space: col 5 = P3.3, row 5 = P5.4
// ---------------------------------------------------------------------
#define RECOVERY_COL_ESC P5_0
#define RECOVERY_COL_ESC_BIT _P5_0
#define RECOVERY_ROW_ESC P7_0
#define RECOVERY_ROW_ESC_BIT _P7_0
#define RECOVERY_COL_SPACE P3_3
#define RECOVERY_COL_SPACE_BIT _P3_3
#define RECOVERY_ROW_SPACE P5_4
#define RECOVERY_ROW_SPACE_BIT _P5_4

// ---------------------------------------------------------------------
// Custom keycodes (SMK SAFE_RANGE base)
// ---------------------------------------------------------------------
enum custom_keycodes {
    RGB_BRI_UP = SAFE_RANGE, // keyboard RGB brightness up (Fn+Up, stock)
    RGB_BRI_DN,              // keyboard RGB brightness down (Fn+Down, stock)
};

// ---------------------------------------------------------------------
// Build-mode gates (meson board options; defaults safe for recovery)
// ---------------------------------------------------------------------
#ifndef RK84_RGB_ENABLE
#define RK84_RGB_ENABLE 0
#endif
#ifndef RK84_RECOVERY_ONLY
#define RK84_RECOVERY_ONLY 0
#endif
#ifndef RK84_USB_FULL
#define RK84_USB_FULL 0
#endif
#ifndef RK84_WIRELESS_ENABLE
#define RK84_WIRELESS_ENABLE 0
#endif

/* Recovery-only and RGB are mutually exclusive: a recovery image must
 * not initialize or drive any RGB/PWM output. */
#if RK84_RECOVERY_ONLY && RK84_RGB_ENABLE
#    error "Recovery-only and RGB modes are mutually exclusive"
#endif

/* Recovery firmware must not initialize the radio. */
#if RK84_RECOVERY_ONLY && RK84_WIRELESS_ENABLE
#    error "Recovery firmware must not initialize the radio"
#endif

/* The USB-full stage is the complete wired keyboard: no RGB, no radio.
 * It is mutually exclusive with recovery (recovery has no scan loop). */
#if RK84_USB_FULL && RK84_RECOVERY_ONLY
#    error "USB-full and recovery-only stages are mutually exclusive"
#endif
#if RK84_USB_FULL && RK84_RGB_ENABLE
#    error "USB-full stage must not enable RGB (RGB is a later stage)"
#endif
#if RK84_USB_FULL && RK84_WIRELESS_ENABLE
#    error "USB-full stage must not enable the radio (wireless is a later stage)"
#endif

/* The USB-full stage provides the board-level suspend/resume hooks
 * (kb.c) that the framework's USB ISR calls on SUSPIF/RESMIF. */
#if RK84_USB_FULL
#define RK84_USB_HOOKS 1
#endif
