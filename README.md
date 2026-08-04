# RK84 SMK Port

SMK board definition for the **Royal Kludge RK84** (84-key, SH68F90A
8051 MCU) targeting [carlossless/smk](https://github.com/carlossless/smk).

**Pinned upstream:** `carlossless/smk @ 08f4d0253389551b9ae9aad2464e2d7cacaf662e`

## What's here

```
src/keyboards/royalkludge-rk84/   the board definition (drop-in overlay)
  kbdef.h                         pins, matrix geometry, options
  kb.c                            board init / key handling
  user_init.c                     GPIO/PWM init + recovery boot chord
  user_matrix.c                   16x6 matrix scan (one col per PWM ISR)
  layouts/default/layout.c        native SMK keymap (84 keys)
  layouts/default/indicators.c    RGB renderer (21 PWM x 18 sinks)
patches/                          0001-rk84-meson.patch
                                  0002-rk84-framework.patch
tools/
  apply-to-smk.sh                 apply patches to a pinned SMK checkout
  check-hex-bounds.py             reject images that reach protected addresses
RESCUE.md                         verified return-to-stock procedure
```

This repo is a **patch overlay** for SMK, not a fork: the framework
changes are real unified diffs pinned to one upstream commit.

## Building (clean-room)

```sh
git clone https://github.com/carlossless/smk.git
cd smk
git checkout 08f4d0253389551b9ae9aad2464e2d7cacaf662e
SMK_DIR=$PWD bash ../rk84-smk-port/tools/apply-to-smk.sh

meson setup build-rk84 --buildtype=release
ninja -C build-rk84 royalkludge-rk84_default_smk.hex
python3 ../rk84-smk-port/tools/check-hex-bounds.py \
    build-rk84/royalkludge-rk84_default_smk.hex
```

Requires SDCC >= 4.3.0 and the meson/ninja toolchain.

## Hardware

- MCU: Sinowealth SH68F90A (8051 core), 24 MHz
- Matrix: 16 columns x 6 rows, idle-high strobes, active-low rows,
  settle + two-sample unanimity debounce
- USB: VID 0x258A / PID 0x0059; boot 6KRO (EP1) + NKRO (EP2,
  report ID 6, 16 bytes, usages 0x04-0x70) + System/Consumer reports
- RGB: 21 PWM column channels x 18 direct sink phases
  (6 rows x 3 components)

## Build modes (meson board options)

| Option | Default | Meaning |
|---|---|---|
| `rk84_recovery_only` | true | USB EP0 + ID 5 + Esc+Space chord only; no PWM/matrix |
| `rk84_rgb` | false | Enable the RGB renderer (safe one-cell bring-up first) |
| `rk84_stock_reports` | true | Stock 16-byte NKRO format + dual-report gating |

## Status

**PRE-FLASH.** Board files and framework patches build clean in a
clean-room checkout; the image passes the HEX bound checks. NOT yet
flashed to hardware. Bench validation per the recovery gates in
RESCUE.md (recovery-only image first, verified stock restore, then
matrix, then media/NKRO, then RGB one-cell) is required before any
feature image is trusted.

## Flash safety

- Application region 0x0000-0xEFFF only; bootloader 0xF000+ never
  touched; settings sector 0xEC00-0xEDFF; redirect sector (0xEFFC)
  never erased by app code.
- Feature report ID 5 (`05 75`) enters ROM ISP (SMK native).
- Esc + Space held at power-on jumps to the ROM bootloader.
- Verified return-to-stock: **RESCUE.md** (sinowisp EP0 tool).
- No automatic flash target for this board (sinowealth-kb-tool
  `--force` is NOT used).

## License

This project contains modifications to
[carlossless/smk](https://github.com/carlossless/smk) and is distributed
under the **same GPLv2 terms** as that project (see LICENSE).
