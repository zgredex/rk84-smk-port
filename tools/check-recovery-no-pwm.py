#!/usr/bin/env python3
"""Assert that a recovery RK84 image contains no PWM scheduler enable.

A recovery-only image must not be able to start the PWM interrupt.
This scan checks the FINAL linked image (.hex -> raw bytes via the
shared hexlib parser) for the specific compiler encodings produced by
this SDCC build:

  - no `ORL IEN1,#0x02`  (EPWM0 enable, IEN1 = SFR 0xA9)
  - no `MOV DPTR,#0xFF80; MOV A,#0xC2; MOVX @DPTR,A` (PWM00CON = 0xC2)
  - no `MOV DPTR,#0xFF80; MOV A,#0xCA; MOVX @DPTR,A` (PWM00CON = 0xCA)

Note: this is a regression check for the current compiler output, not
a proof against arbitrary instruction sequences (e.g. loading IEN1
through the accumulator). A simulator that traps every write to IEN1
and 0xFF80 would be the stronger invariant.

Usage:
    python3 check-recovery-no-pwm.py build/royalkludge-rk84_default_smk.hex
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from hexlib import parse_hex

# IEN1 is SFR 0xA9 on SH68F90A; EPWM0 is bit 1.
# ORL direct,#imm = 0x43
EPWM0_ENABLE = bytes([0x43, 0xA9, 0x02])

# PWM00CON is SFRX 0xFF80. MOV DPTR,#imm16 = 0x90; MOV A,#imm = 0x74;
# MOVX @DPTR,A = 0xF0.
PWM00CON_C2 = bytes([0x90, 0xFF, 0x80, 0x74, 0xC2, 0xF0])
PWM00CON_CA = bytes([0x90, 0xFF, 0x80, 0x74, 0xCA, 0xF0])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex_file", type=Path)
    args = parser.parse_args()

    try:
        blob = parse_hex(args.hex_file)
    except ValueError as e:
        print(f"FAIL: {args.hex_file}: {e}", file=sys.stderr)
        return 1

    epwm0 = blob.count(EPWM0_ENABLE)
    c2 = blob.count(PWM00CON_C2)
    ca = blob.count(PWM00CON_CA)

    print(f"EPWM0 enable (43 A9 02): {epwm0}")
    print(f"PWM00CON = 0xC2 writes:  {c2}")
    print(f"PWM00CON = 0xCA writes:  {ca}")

    if epwm0 or c2 or ca:
        print("FAIL: recovery image contains a PWM enable path", file=sys.stderr)
        return 1
    print("OK: recovery image has no PWM scheduler enable path")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
