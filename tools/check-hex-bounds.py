#!/usr/bin/env python3
"""Check that an Intel HEX image keeps code below a protected address.

Temporary first-image policy: highest application data address < 0xBC00.
Normal SMK policy: linked code < 0xEC00; settings sector 0xEC00-0xEDFF;
redirect sector containing 0xEFFC is never erased by the app;
bootloader 0xF000-0xFFFF.

Usage:
    python3 check-hex-bounds.py build/royalkludge-rk84_default_smk.hex
    python3 check-hex-bounds.py build/royalkludge-rk84_default_smk.hex --limit 0xEC00
"""
from __future__ import annotations

import argparse
from pathlib import Path


def iter_hex_addresses(path: Path):
    upper = 0
    eof_seen = False

    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip()

        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_no}: invalid Intel HEX line")

        data = bytes.fromhex(line[1:])
        count = data[0]
        addr = (data[1] << 8) | data[2]
        record_type = data[3]
        payload = data[4:4 + count]

        # Structural validation: declared length must match the line,
        # and the checksum byte must make the whole record sum to 0.
        if len(payload) != count:
            raise ValueError(
                f"{path}:{line_no}: record length mismatch "
                f"(declared {count}, got {len(payload)})"
            )
        if sum(data) & 0xFF:
            raise ValueError(
                f"{path}:{line_no}: checksum mismatch"
            )

        if eof_seen:
            raise ValueError(
                f"{path}:{line_no}: data after EOF record"
            )

        if record_type == 0x00:
            for offset in range(len(payload)):
                yield upper + addr + offset
        elif record_type == 0x04:
            if len(payload) != 2:
                raise ValueError(f"{path}:{line_no}: bad ELA record")
            upper = ((payload[0] << 8) | payload[1]) << 16
        elif record_type == 0x01:
            eof_seen = True
            # Continue scanning so data after EOF is caught by the
            # eof_seen guard above.
            continue
        else:
            # Unknown record types (e.g. 0x05 start linear address)
            # are ignored for bounds purposes but still checksummed.
            continue

    if not eof_seen:
        raise ValueError(f"{path}: missing EOF record (type 01)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex_file", type=Path)
    parser.add_argument("--limit", type=lambda value: int(value, 0), default=0xBC00)
    args = parser.parse_args()

    addresses = list(iter_hex_addresses(args.hex_file))
    highest = max(addresses, default=-1)

    if highest >= args.limit:
        raise SystemExit(
            f"FAIL: highest address 0x{highest:04X} "
            f"reaches limit 0x{args.limit:04X}"
        )

    print(f"OK: highest address 0x{highest:04X}, below 0x{args.limit:04X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
