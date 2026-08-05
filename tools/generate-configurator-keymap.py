#!/usr/bin/env python3
"""Generate configurator/src/generated/rk84-default-keymap.ts from the
source-faithful firmware layout parser (Q1 audit).

The mock's compiled-default keymap MUST be the real RK84 layout, not a
synthetic blank map. This generator derives the TypeScript constant
from the ACTUAL layout.c / keycodes.h / kbdef.h via
tests.keymap_fixture.load_keymaps() — the same parser the matrix model
uses.

Usage:
    generate-configurator-keymap.py            # write the file
    generate-configurator-keymap.py --check    # fail if stale (CI)
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "configurator" / "src" / "generated" / "rk84-default-keymap.ts"


def build_source() -> str:
    sys.path.insert(0, str(REPO))
    if "RK84_SMK_TREE" not in os.environ:
        raise SystemExit("RK84_SMK_TREE must point at the pinned SMK tree")
    from tests.keymap_fixture import load_keymaps  # noqa: PLC0415

    layers = load_keymaps()  # dict[int, list[list[int]]]
    values = [
        layers[layer][row][col]
        for layer in range(2)
        for row in range(6)
        for col in range(16)
    ]
    if len(values) != 2 * 6 * 16:
        raise SystemExit(f"expected 192 cells, got {len(values)}")

    body = ",\n  ".join(f"0x{value:04x}" for value in values)
    return (
        "/* Generated from firmware layout.c via tests/keymap_fixture.py;\n"
        " * do not edit by hand. Run tools/generate-configurator-keymap.py\n"
        " * after changing the RK84 layout. */\n"
        "export const RK84_DEFAULT_KEYMAP = new Uint16Array([\n"
        f"  {body},\n"
        "]);\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if the generated file is stale")
    args = parser.parse_args()

    source = build_source()
    if args.check:
        if not OUT.exists():
            print(f"stale: {OUT} missing", file=sys.stderr)
            return 1
        if OUT.read_text() != source:
            print(f"stale: {OUT} differs from firmware layout", file=sys.stderr)
            return 1
        print(f"OK: {OUT} matches firmware layout")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(source)
    print(f"wrote {OUT} ({len(values := []) or 192} cells)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
