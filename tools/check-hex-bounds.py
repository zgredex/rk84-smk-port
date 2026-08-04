#!/usr/bin/env python3
"""Check that an Intel HEX image keeps code below a protected address.

Temporary first-image policy: highest application data address < 0xBC00.
Normal SMK policy: linked code < 0xEC00; settings sector 0xEC00-0xEDFF;
redirect sector containing 0xEFFC is never erased by the app;
bootloader 0xF000-0xFFFF.

Uses the shared strictly-validated parser (hexlib).

Usage:
    python3 check-hex-bounds.py build/royalkludge-rk84_default_smk.hex
    python3 check-hex-bounds.py build/royalkludge-rk84_default_smk.hex --limit 0xEC00
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from hexlib import hex_extent


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex_file", type=Path)
    parser.add_argument("--limit", type=lambda value: int(value, 0), default=0xBC00)
    args = parser.parse_args()

    try:
        written, lowest, highest = hex_extent(args.hex_file)
    except ValueError as e:
        print(f"FAIL: {args.hex_file}: {e}", file=sys.stderr)
        return 1

    if highest >= args.limit:
        print(
            f"FAIL: highest address 0x{highest:04X} "
            f"reaches limit 0x{args.limit:04X}",
            file=sys.stderr,
        )
        return 1

    print(
        f"OK: {written} bytes written, lowest 0x{lowest:04X}, "
        f"highest 0x{highest:04X}, below 0x{args.limit:04X}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
