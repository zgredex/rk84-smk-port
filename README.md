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

# Recovery image (first bench candidate):
meson setup build-rk84-recovery --buildtype=release -Drk84_stage=recovery
ninja -C build-rk84-recovery royalkludge-rk84_default_smk.hex

# Matrix/6KRO stage:
meson setup build-rk84-matrix --buildtype=release -Drk84_stage=matrix
ninja -C build-rk84-matrix royalkludge-rk84_default_smk.hex

# One-cell RGB stage:
meson setup build-rk84-rgb --buildtype=release -Drk84_stage=rgb
ninja -C build-rk84-rgb royalkludge-rk84_default_smk.hex

# Dual reports (only after host validation on Linux/macOS/Windows):
meson setup build-rk84-dual --buildtype=release \
    -Drk84_stage=matrix -Drk84_dual_reports=true
ninja -C build-rk84-dual royalkludge-rk84_default_smk.hex

# Validate every image (bounds + checksum):
for hexfile in build-rk84-*/royalkludge-rk84_default_smk.hex; do
    python3 ../rk84-smk-port/tools/check-hex-bounds.py "$hexfile"
done
```

Requires SDCC >= 4.3.0 and the meson/ninja toolchain.

## Offline verification (no hardware needed)

```sh
# 1. Test suite (zero dependencies, stdlib unittest):
python3 -m unittest discover -s tests -t .

# 2. Per-image checks:
python3 tools/check-hex-bounds.py      build/..._smk.hex           # bounds+checksum
python3 tools/check-recovery-no-pwm.py build/..._smk.ihx           # recovery: no PWM enable
python3 tools/check-usb-descriptors.py build/..._smk.ihx           # stock USB identity

# 3. Build manifest (ties artifact to source state):
python3 tools/make-manifest.py build/..._smk.hex build/..._smk.ihx --build-dir build/
```

The test suite covers:

- **HEX parsers**: truncated records, bad checksums, missing/malformed
  EOF, data after EOF, type 0x02/0x04 addressing, limits at
  0xBC00/0xEC00/0xEFFC/0xF000
- **PWM invariant**: no EPWM0 enable / PWM00CON 0xC2/0xCA in recovery
  images, including above-64 KiB addressing
- **USB descriptors**: VID:PID, bcdDevice, serial index, EP sizes,
  Feature ID 5, NKRO usage range, System/Consumer sizes
- **Report model**: 6KRO, modifiers, rollover, NKRO first/last usage,
  usage>0x70 rejection, System 2B, Consumer 3B, golden fixtures
- **Matrix/layer model**: sticky-key sequences (A/Fn/A, Fn/F8/Fn/F8,
  same-frame Fn+F8, unstable column), Fn never in report, recovery
  ignores matrix
- **RGB scheduler**: framebuffer bounds, channel/sink range, duty <
  period, one column per interrupt, recovery zero-writes
- **Reproducibility**: two clean builds byte-identical, manifest
  consistency

## Bench toolkit

```sh
# Record a full bench session (USB before/after, HID, logs):
./tools/bench-collect.sh session1 --flash build/..._smk.hex --sinowisp ./rk68-sinowisp-macos-ep0

# Restore stock — REFUSES unless backup MD5 == 4ca60eb0...:
./tools/restore-stock.sh
```

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
| `rk84_stage` | recovery | `recovery` (USB + ISP ID5 + chord only), `matrix` (scan + 6KRO), `rgb` (one-cell RGB) |
| `rk84_dual_reports` | false | Send simultaneous RK84 EP1 6KRO and EP2 NKRO (host-test first) |

Recovery image contents: normal USB descriptor set and ISP Feature ID 5,
but **no PWM scheduler, matrix scanning, RGB output, RF, or settings
writes**. The interface-1 route used by the verified ISP command is
preserved.

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
