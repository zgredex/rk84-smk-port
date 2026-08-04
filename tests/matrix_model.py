"""Reference model of the RK84 matrix + layer logic (mirrors smk/matrix.c).

Implements the press-time keycode resolution with the active-keycode
cache (SMK_ACTIVE_KEYCODE_CACHE=1) so the sticky-key behaviour can be
validated on the host:

  - keycodes are resolved and STORED on press
  - releases use the stored keycode, never re-resolve
  - Fn (momentary layer 1) never enters a HID report itself
  - KC_TRANSPARENT falls through to the base layer
  - recovery mode processes no matrix events

The layout used is the RK84 default (16 cols x 6 rows), with Fn at
c9r5 and the Fn layer's media keys on row 0.
"""
from __future__ import annotations

KC_NO = 0
KC_TRANSPARENT = 0x7FFF
MO_MASK = 0x0200  # QK_MOMENTARY tag (simplified)

# keymap[row][col] -> keycode (only entries needed by tests; the rest
# fall through to the base layer via KC_TRANSPARENT in layer 1).
BASE = {
    (0, 0): 0x04,   # A
    (0, 5): 0x42,   # F8 (col5 row0 in the real layout is F8)
    (5, 9): MO_MASK | 1,  # Fn at c9r5
    (5, 10): 0xE4,  # RCtrl
    (5, 5): 0x2C,   # Space
}
FN_LAYER = {
    (0, 0): 0x04,          # KC_TRANSPARENT semantics: A stays A
    (0, 5): 0x00CD,        # Fn+F8 -> media play/pause
    (4, 14): 0x9F01,       # RGB_BRI_UP (custom)
    (5, 14): 0x9F02,       # RGB_BRI_DN (custom)
}


def resolve_keycode(row: int, col: int, layer: int) -> int:
    base = BASE.get((row, col), 0x04)  # default: A-like
    if base & MO_MASK:
        return base  # base-layer momentary stays momentary
    if layer:
        lc = FN_LAYER.get((row, col), KC_TRANSPARENT)
        if lc != KC_TRANSPARENT:
            return lc
    return base


class MatrixEvent:
    def __init__(self, row: int, col: int, pressed: bool):
        self.row, self.col, self.pressed = row, col, pressed


class RK84MatrixModel:
    """Tracks held keys + the active-keycode cache like matrix.c."""

    def __init__(self, recovery_only: bool = False):
        self.layer = 0
        self.active = {}  # (row,col) -> keycode (stored on press)
        self.held = {}  # (row,col) -> resolved keycode while held
        self.hid_keys = []  # what would be in the HID report
        self.report_log = []  # (keycode, pressed) events sent to report
        self.recovery_only = recovery_only

    def process(self, row: int, col: int, pressed: bool):
        if self.recovery_only:
            return  # recovery mode never processes matrix events
        key = (row, col)
        if pressed:
            qcode = resolve_keycode(row, col, self.layer)
            self.active[key] = qcode
            self.held[key] = qcode
        else:
            qcode = self.active.pop(key, KC_NO)
            if qcode == KC_NO:
                return
            del self.held[key]
        self.report_log.append((qcode, pressed))

        if qcode & MO_MASK:
            if pressed:
                self.layer = qcode & 0xFF
            else:
                self.layer = 0
            return

        # simulate report state: normal keys go in, Fn never does
        if not (qcode & MO_MASK):
            if pressed:
                if qcode not in self.hid_keys:
                    self.hid_keys.append(qcode)
            else:
                # only drop from the report when no OTHER held physical
                # key still resolves to the same keycode
                if qcode not in [c for k, c in self.held.items() if k != key]:
                    if qcode in self.hid_keys:
                        self.hid_keys.remove(qcode)

    # ------------------------------------------------------------------
    def send(self, fn_layer: int = 1):
        """What the HID report would contain (sorted, no Fn)."""
        return sorted(k for k in self.hid_keys)

    def stuck_keys(self) -> list:
        """Keycodes that are held (physically down, resolved) but absent
        from the HID report — these would be stuck."""
        held_codes = list(self.held.values())
        return [c for c in held_codes if c not in self.hid_keys]

    def press_release_balance(self) -> bool:
        """Every press has exactly one matching release."""
        counts = {}
        for qcode, pressed in self.report_log:
            counts[qcode] = counts.get(qcode, 0) + (1 if pressed else -1)
        return all(v == 0 for v in counts.values())
