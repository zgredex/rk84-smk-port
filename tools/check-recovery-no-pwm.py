#!/usr/bin/env python3
"""Assert that a recovery RK84 image contains no PWM scheduler enable.

A recovery-only image must not be able to start the PWM interrupt:
  - no `ORL IEN1,#0x02`  (EPWM0 enable, IEN1 = SFR 0xA9)
  - no `MOV DPTR,#0xFF80; MOV A,#0xC2; MOVX @DPTR,A` (PWM00CON = 0xC2)
  - no `MOV DPTR,#0xFF80; MOV A,#0xCA; MOVX @DPTR,A` (PWM00CON = 0xCA)

The scan runs on the FINAL linked image (.ihx -> raw bytes), not on
intermediate .rel files, so it proves the machine code the MCU will
actually execute contains no enable path.

Usage:
    python3 check-recovery-no-pwm.py build/royalkludge-rk84_default_smk.ihx
"""
from __future__ import annotations

import argparse
from pathlib import Path

# IEN1 is SFR 0xA9 on SH68F90A; EPWM0 is bit 1.
# ORL direct,#imm = 0x43
EPWM0_ENABLE = bytes([0x43, 0xA9, 0x02])

# PWM00CON is SFRX 0xFF80. MOV DPTR,#imm16 = 0x90; MOV A,#imm = 0x74;
# MOVX @DPTR,A = 0xF0.
PWM00CON_C2 = bytes([0x90, 0xFF, 0x80, 0x74, 0xC2, 0xF0])
PWM00CON_CA = bytes([0x90, 0xFF, 0x80, 0x74, 0xCA, 0xF0])


def ihx_to_bytes(path: Path) -> bytes:
    data = bytearray()
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip()
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_no}: invalid Intel HEX line")
        rec = bytes.fromhex(line[1:])
        count = rec[0]
        addr = (rec[1] << 8) | rec[2]
        typ = rec[3]
        payload = rec[4:4 + count]
        if len(payload) != count or sum(rec) & 0xFF:
            raise ValueError(f"{path}:{line_no}: bad record")
        if typ == 0x00:
            base = len(data)
            if addr + count > len(data):
                data.extend(b"\x00" * (addr + count - len(data)))
            data[addr:addr + count] = payload
        elif typ == 0x01:
            return bytes(data)
    raise ValueError(f"{path}: missing EOF record")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ihx_file", type=Path)
    args = parser.parse_args()

    blob = ihx_to_bytes(args.ihx_file)

    epwm0 = blob.count(EPWM0_ENABLE)
    c2 = blob.count(PWM00CON_C2)
    ca = blob.count(PWM00CON_CA)

    print(f"EPWM0 enable (43 A9 02): {epwm0}")
    print(f"PWM00CON = 0xC2 writes:  {c2}")
    print(f"PWM00CON = 0xCA writes:  {ca}")

    if epwm0 or c2 or ca:
        raise SystemExit("FAIL: recovery image contains a PWM enable path")
    print("OK: recovery image has no PWM scheduler enable path")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
