"""Source-faithful RK84 matrix + layer model (mirrors smk/matrix.c).

Keycodes come from the ACTUAL layout.c/keycodes.h via keymap_fixture,
never hand-approximated. Implements:

  - press-time keycode resolution with the active-keycode cache
    (SMK_ACTIVE_KEYCODE_CACHE=1)
  - releases use the stored keycode, never re-resolve
  - Fn (QK_MOMENTARY) never enters a HID report
  - KC_TRANSPARENT falls through to the base layer
  - recovery mode processes no matrix events

Scan-order behavior (Option B, documented): changed columns are
processed in ascending column order, exactly like the firmware
(matrix_task iterates col 0..MATRIX_COLS-1). If Fn (col 9) and F8
(col 8) change in the same completed frame, F8 is processed first
while the base layer is still active and resolves as plain F8. The
tests document this rather than asserting unverified Fn priority.
"""
from __future__ import annotations

from .keymap_fixture import KC, MO, is_momentary, momentary_layer, load_keymaps

# QK_MOMENTARY range from keycodes.h
QK_MOMENTARY = KC["QK_MOMENTARY"]
QK_MOMENTARY_MAX = KC["QK_MOMENTARY_MAX"]
KC_NO = KC["KC_NO"]
KC_TRANSPARENT = KC["KC_TRANSPARENT"]


class RK84MatrixModel:
    def __init__(self, recovery_only: bool = False):
        self.keymaps = load_keymaps()
        self.layer = 0
        self.active = {}  # (row,col) -> resolved keycode (stored on press)
        self.held = {}  # (row,col) -> resolved keycode while held
        self.hid_keys = []  # keycodes in the HID report
        self.report_log = []  # (keycode, pressed) events
        self.recovery_only = recovery_only

    # ------------------------------------------------------------------
    def resolve_keycode(self, row: int, col: int) -> int:
        qcode = self.keymaps[0][row][col]
        # a base-layer momentary key stays momentary under any layer
        if is_momentary(qcode):
            return qcode
        if self.layer:
            lc = self.keymaps[self.layer][row][col]
            if lc != KC_TRANSPARENT:
                return lc
        return qcode

    def process(self, row: int, col: int, pressed: bool):
        if self.recovery_only:
            return
        key = (row, col)
        if pressed:
            qcode = self.resolve_keycode(row, col)
            self.active[key] = qcode
            self.held[key] = qcode
        else:
            qcode = self.active.pop(key, KC_NO)
            if qcode == KC_NO:
                return
            del self.held[key]
        self.report_log.append((qcode, pressed))

        if is_momentary(qcode):
            if pressed:
                self.layer = momentary_layer(qcode)
            else:
                self.layer = 0
            return

        # normal key: update report state (Fn never enters)
        if pressed:
            if qcode not in self.hid_keys:
                self.hid_keys.append(qcode)
        else:
            if qcode not in [c for k, c in self.held.items() if k != key]:
                if qcode in self.hid_keys:
                    self.hid_keys.remove(qcode)

    # ------------------------------------------------------------------
    def process_frame(self, changes: list[tuple[int, int, bool]]):
        """Process a completed matrix frame like matrix_task():
        columns in ascending order; within a column, rows ascending."""
        by_col = {}
        for row, col, pressed in changes:
            by_col.setdefault(col, []).append((row, pressed))
        for col in sorted(by_col):
            for row, pressed in sorted(by_col[col]):
                self.process(row, col, pressed)

    def stuck_keys(self) -> list:
        held_codes = list(self.held.values())
        return [c for c in held_codes if c not in self.hid_keys]

    def press_release_balance(self) -> bool:
        counts = {}
        for qcode, pressed in self.report_log:
            counts[qcode] = counts.get(qcode, 0) + (1 if pressed else -1)
        return all(v == 0 for v in counts.values())
