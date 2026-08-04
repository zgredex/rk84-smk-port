"""Source-faithful RK84 keymap fixture.

Parses the ACTUAL board sources (layout.c + keycodes.h + kbdef.h) and
produces the exact numeric keymap arrays the firmware uses. No manual
approximation: if parsing fails to produce exactly 6x16 cells per
layer, the tests fail.

Usage (host-side):
    from tests.keymap_fixture import load_keymaps, KC
"""
from __future__ import annotations

import os
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BOARD = REPO / "src" / "keyboards" / "royalkludge-rk84"
LAYOUT_C = BOARD / "layouts" / "default" / "layout.c"
KBDEF_H = BOARD / "kbdef.h"

# The canonical keycode definitions come from the PINNED SMK tree,
# never from a patch or a local approximation. Set RK84_SMK_TREE in CI
# (and locally) to the checked-out pinned SMK repository.
SMK_TREE = Path(os.environ["RK84_SMK_TREE"])
KEYCODES_H = SMK_TREE / "src" / "smk" / "keycodes.h"

if not KEYCODES_H.is_file():
    raise RuntimeError(
        f"pinned SMK keycodes missing: {KEYCODES_H} "
        "(set RK84_SMK_TREE to the checked-out "
        "carlossless/smk@08f4d02 tree)"
    )

MATRIX_ROWS = 6
MATRIX_COLS = 16

# ----------------------------------------------------------------------
# keycode constant resolution
# ----------------------------------------------------------------------

def _load_kc_values() -> dict[str, int]:
    """Resolve every KC_* / QK_* name to its numeric value from the
    pinned SMK keycodes.h (enum members, #define macros, aliases)."""
    values: dict[str, int] = {}

    text = ""
    if KEYCODES_H.exists():
        text = KEYCODES_H.read_text()
    if not text:
        raise RuntimeError(f"cannot read SMK keycodes.h: {KEYCODES_H}")

    # ---- pass 1: enum members with explicit values and increments ----
    aliases: dict[str, str] = {}
    last = 0x0000
    in_enum = False
    for line in text.splitlines():
        line = line.split("//")[0].strip()
        if not line:
            continue
        if "enum" in line and "{" in line:
            in_enum = True
            last = 0x0000
            continue
        if in_enum and "}" in line:
            in_enum = False
            continue
        if not in_enum:
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*0x([0-9A-Fa-f]+)", line)
        if m:
            values[m.group(1)] = int(m.group(2), 16)
            last = int(m.group(2), 16)
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)", line)
        if m:
            aliases[m.group(1)] = m.group(2)
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*,", line)
        if m:
            last += 1
            values[m.group(1)] = last

    # ---- pass 2: #define macros ----
    for m in re.finditer(
        r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\(?\s*([^)\s]+)",
        text,
    ):
        name, val = m.group(1), m.group(2).strip()
        if val in values:
            values[name] = values[val]
        elif re.fullmatch(r"0x[0-9A-Fa-f]+", val):
            values[name] = int(val, 16)
        elif re.fullmatch(r"\d+", val):
            values[name] = int(val)

    # ---- pass 3: aliases (iterate until stable) ----
    remaining = dict(aliases)
    for _ in range(8):
        resolved_any = False
        for name, target in list(remaining.items()):
            if target in values:
                values[name] = values[target]
                del remaining[name]
                resolved_any = True
        if not resolved_any:
            break

    return values


def _resolve_mo(expr: str, kc: dict[str, int]) -> int:
    """MO(1) -> QK_MOMENTARY | 1"""
    m = re.match(r"MO\((\d+)\)", expr)
    if m:
        return kc["QK_MOMENTARY"] | (int(m.group(1)) & 0x1F)
    raise ValueError(f"cannot resolve keycode expression: {expr!r}")


def _resolve_cell(expr: str, kc: dict[str, int]) -> int:
    expr = expr.strip()
    if expr.startswith("MO("):
        return _resolve_mo(expr, kc)
    if expr in kc:
        return kc[expr]
    raise ValueError(f"unresolved keycode: {expr!r}")


def _load_custom_keycodes(kc: dict[str, int]) -> dict[str, int]:
    """Resolve board custom keycodes (kbdef.h enum custom_keycodes) using
    SAFE_RANGE from keycodes.h."""
    if not KBDEF_H.exists():
        return {}
    text = KBDEF_H.read_text()
    m = re.search(r"enum custom_keycodes \{(.*?)\}", text, re.S)
    if not m:
        return {}
    safe = kc.get("SAFE_RANGE")
    if safe is None:
        raise ValueError("SAFE_RANGE not found in keycodes.h")
    values: dict[str, int] = {}
    last = safe - 1
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip()
        if not line:
            continue
        mm = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(SAFE_RANGE|0x[0-9A-Fa-f]+|\d+)", line)
        if mm:
            if mm.group(2) == "SAFE_RANGE":
                values[mm.group(1)] = safe
                last = safe
            else:
                values[mm.group(1)] = int(mm.group(2), 0)
                last = int(mm.group(2), 0)
            continue
        mm = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*,", line)
        if mm:
            last += 1
            values[mm.group(1)] = last
    return values


# ----------------------------------------------------------------------
# layout.c parsing
# ----------------------------------------------------------------------

def _parse_layout_text(text: str, kc: dict[str, int]) -> dict[int, list[list[int]]]:
    layers: dict[int, list[list[int]]] = {}

    # split the keymaps array into per-layer blocks
    for m in re.finditer(r"\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        layer_num = int(m.group(1))
        body = m.group(2)
        rows: list[list[int]] = []
        for row_m in re.finditer(r"\{\s*([^}]*?)\s*\}", body, re.S):
            cells_src = [c.strip() for c in row_m.group(1).split(",") if c.strip()]
            cells = [_resolve_cell(c, kc) for c in cells_src]
            rows.append(cells)
        layers[layer_num] = rows

    # strict validation: exactly 6 rows x 16 cols per layer
    for num, rows in layers.items():
        if len(rows) != MATRIX_ROWS:
            raise ValueError(
                f"layer {num}: expected {MATRIX_ROWS} rows, got {len(rows)}"
            )
        for ri, row in enumerate(rows):
            if len(row) != MATRIX_COLS:
                raise ValueError(
                    f"layer {num} row {ri}: expected {MATRIX_COLS} cols, "
                    f"got {len(row)}"
                )
    return layers


def load_keymaps() -> dict[int, list[list[int]]]:
    """Load the numeric keymaps from board sources.

    Returns {0: [[...6x16...]], 1: [...], ...}. Fails loudly on any
    parse/size mismatch.
    """
    if not LAYOUT_C.exists():
        raise FileNotFoundError(f"layout.c not found: {LAYOUT_C}")
    kc = _load_kc_values()
    kc.update(_load_custom_keycodes(kc))
    layers = _parse_layout_text(LAYOUT_C.read_text(), kc)
    if 0 not in layers:
        raise ValueError("layout.c has no layer 0")
    return layers


# convenience: the constants the models/tests need
KC = _load_kc_values()


def MO(layer: int) -> int:
    return KC["QK_MOMENTARY"] | (layer & 0x1F)


def is_momentary(code: int) -> bool:
    return KC["QK_MOMENTARY"] <= code <= KC["QK_MOMENTARY_MAX"]


def momentary_layer(code: int) -> int:
    return code & 0x1F


if __name__ == "__main__":
    km = load_keymaps()
    print(f"layers: {sorted(km)}")
    print(f"layer0 r3c1 (A):  0x{km[0][3][1]:04X}")
    print(f"layer0 r0c8 (F8): 0x{km[0][0][8]:04X}")
    print(f"layer0 r5c9 (Fn): 0x{km[0][5][9]:04X}")
    print(f"layer0 r5c10(RCtl):0x{km[0][5][10]:04X}")
    print(f"layer0 r4c14 (Up):0x{km[0][4][14]:04X}")
    print(f"layer0 r5c14 (Dn):0x{km[0][5][14]:04X}")
    tr = sum(1 for row in km[1] for c in row if c == KC["KC_TRANSPARENT"])
    print(f"layer1 transparent cells: {tr}/96")
