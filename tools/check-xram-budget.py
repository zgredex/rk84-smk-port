#!/usr/bin/env python3
"""XRAM budget gate for the RK84 builds.

Parses an SDCC `.mem` file and fails if static EXTERNAL RAM (XDATA)
usage exceeds the budget. Rationale (companion-app feasibility): a
future dynamic keymap (384 B) + the existing RGB framebuffer (378 B) +
active-key cache must fit alongside today's 1,207 B without silently
drifting over the 4 KiB XRAM limit.

Usage:
  check-xram-budget.py <file.mem> [--limit <bytes>]

Default limit: 3072 (conservative — leaves room for compiler
temporaries and the configurator command buffer).

Exit codes: 0 = within budget, 1 = over budget / unparsable.
"""
import argparse
import re
import sys
from pathlib import Path

DEFAULT_LIMIT = 3072


def parse_mem(path: Path) -> dict:
    """Return {name: size} for the memory areas listed in an SDCC .mem."""
    areas = {}
    in_table = False
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("Name") and "Start" in line:
            in_table = True
            continue
        if not in_table:
            continue
        if line.startswith("-") or not line:
            continue
        # Format: Name  Start  End  Size  Max
        #   EXTERNAL RAM     0x0000   0x04b6    1207     4096
        #   ROM/EPROM/FLASH  0x0000   0x3038   12345    60416
        #   PAGED EXT. RAM                      0      256
        # Names may contain spaces; split at the first hex address.
        m = re.match(r"^(.*?)\s+(0x[0-9a-fA-F]+|\d+)\s+(0x[0-9a-fA-F]+|\d+)\s+(\d+)\s+(\d+)\s*$", line)
        if not m:
            continue
        name = m.group(1).strip()
        start = int(m.group(2), 0)
        end = int(m.group(3), 0)
        size = int(m.group(4), 10)
        maxv = int(m.group(5), 10)
        areas[name] = {"start": start, "end": end, "size": size, "max": maxv}
    return areas


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("memfile")
    ap.add_argument("--limit", type=int, default=DEFAULT_LIMIT)
    args = ap.parse_args()

    path = Path(args.memfile)
    if not path.exists():
        print(f"ERROR: .mem file not found: {path}", file=sys.stderr)
        return 1

    areas = parse_mem(path)
    if "EXTERNAL RAM" not in areas:
        print(f"ERROR: no EXTERNAL RAM area in {path} (parsed: {list(areas)})",
              file=sys.stderr)
        return 1

    used = areas["EXTERNAL RAM"]["size"]
    avail = areas["EXTERNAL RAM"]["max"]
    flash = areas.get("ROM/EPROM/FLASH", {}).get("size", 0)
    flash_max = areas.get("ROM/EPROM/FLASH", {}).get("max", 0)

    print(f"XRAM:  {used} B used of {avail} B  ({100 * used / avail:.1f}%)")
    print(f"FLASH: {flash} B used of {flash_max} B  ({100 * flash / flash_max:.1f}%)")
    print(f"budget limit: {args.limit} B XRAM")

    if used > args.limit:
        print(f"FAIL: XRAM {used} B exceeds budget {args.limit} B — "
              f"the dynamic-keymap configurator will not fit. "
              f"Audit new XRAM allocations before proceeding.",
              file=sys.stderr)
        return 1

    print(f"OK: XRAM within budget (<= {args.limit} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
