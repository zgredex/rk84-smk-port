# RK84 SMK Port

SMK board definition for the **Royal Kludge RK84** (84-key, SH68F90A
8051 MCU) targeting [carlossless/smk](https://github.com/carlossless/smk).

## What's here

```
src/keyboards/royalkludge-rk84/   the board definition (drop-in overlay)
  kbdef.h                         pins, matrix geometry, options
  kb.c                            board init / key handling
  user_init.c                     GPIO/PWM init + recovery boot chord
  user_matrix.c                   16x6 matrix scan (one col per PWM ISR)
  layouts/default/layout.c        native SMK keymap (84 keys)
  layouts/default/indicators.c    static RGB renderer (21 PWM x 18 sinks)
patches/                          minimal SMK framework changes required
```

This mirrors the upstream SMK tree structure: copy
`src/keyboards/royalkludge-rk84/` into an SMK checkout, apply the
patches, add the meson entry, build.

## Hardware

- MCU: Sinowealth SH68F90A (8051 core), 24 MHz
- Matrix: 16 columns x 6 rows, idle-high strobes, active-low rows,
  settle + two-sample unanimity debounce
- USB: VID 0x258A / PID 0x0059; boot 6KRO (EP1) + NKRO (EP2,
  report ID 6, 16 bytes) + System/Consumer reports
- RGB: 21 PWM column channels x 18 direct sink phases
  (6 rows x 3 components), static color + brightness in this milestone

## Status

Milestone 1 (wired-only): board files complete, builds clean with
SDCC 4.6.0. Not yet flashed to hardware — bench validation pending.

## Building

```sh
cd /path/to/smk
meson setup build-rk84 --buildtype=release
ninja -C build-rk84 royalkludge-rk84_default_smk.hex
```

Requires SDCC >= 4.3.0 and the meson/ninja toolchain.

## Flash safety

The application region is 0x0000-0xEFFF only; the bootloader at
0xF000+ is never touched. A boot chord (Esc + Space held at power-on)
jumps to the ROM bootloader for recovery.

## License

Board files and patches are original work released under the MIT
license, matching the upstream SMK project.
