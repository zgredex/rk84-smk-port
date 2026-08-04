"""Matrix + layer logic tests using the source-faithful keymap.

Positions come from the real layout (keymap_fixture):
  A = r3c1, F8 = r0c8, Fn = r5c9, RCtrl = r5c10, Space = r5c5,
  Up = r4c14, Down = r5c14.
"""
from __future__ import annotations

import unittest

from .keymap_fixture import KC, MO
from .matrix_model import KC_NO, KC_TRANSPARENT, QK_MOMENTARY, RK84MatrixModel

POS_A = (3, 1)
POS_F8 = (0, 8)
POS_FN = (5, 9)
POS_SPACE = (5, 5)

MEDIA_PLAY_PAUSE = KC["KC_MEDIA_PLAY_PAUSE"]


class SourceParityTests(unittest.TestCase):
    """The parsed keymap must match the documented layout exactly."""

    def test_layer0_shape(self):
        m = RK84MatrixModel()
        self.assertEqual(len(m.keymaps[0]), 6)
        for row in m.keymaps[0]:
            self.assertEqual(len(row), 16)

    def test_layer1_shape(self):
        m = RK84MatrixModel()
        self.assertEqual(len(m.keymaps[1]), 6)
        for row in m.keymaps[1]:
            self.assertEqual(len(row), 16)

    def test_a_position(self):
        m = RK84MatrixModel()
        self.assertEqual(m.keymaps[0][3][1], KC["KC_A"])

    def test_f8_position_and_value(self):
        m = RK84MatrixModel()
        self.assertEqual(m.keymaps[0][0][8], 0x41)  # KC_F8

    def test_fn_position(self):
        m = RK84MatrixModel()
        self.assertEqual(m.keymaps[0][5][9], MO(1))

    def test_rctrl_position(self):
        m = RK84MatrixModel()
        self.assertEqual(m.keymaps[0][5][10], KC["KC_RIGHT_CTRL"])

    def test_up_down_positions(self):
        m = RK84MatrixModel()
        self.assertEqual(m.keymaps[0][4][14], KC["KC_UP"])
        self.assertEqual(m.keymaps[0][5][14], KC["KC_DOWN"])

    def test_layer1_unmapped_are_transparent(self):
        m = RK84MatrixModel()
        # remapped cells in layer 1: row 0 (media row) + Up/Down
        # brightness at r4c14 / r5c14 (custom keycodes, not transparent)
        remapped = {(0, c) for c in range(16)} | {(4, 14), (5, 14)}
        for r in range(6):
            for c in range(16):
                if (r, c) in remapped:
                    continue
                self.assertEqual(
                    m.keymaps[1][r][c], KC_TRANSPARENT,
                    f"layer1 r{r}c{c} must be transparent",
                )

    def test_mo_semantics(self):
        self.assertTrue(QK_MOMENTARY <= MO(1) <= QK_MOMENTARY + 0x1F)
        self.assertEqual(MO(1) & 0x1F, 1)


class StickyKeyTests(unittest.TestCase):
    def test_a_down_fn_down_a_up_fn_up(self):
        m = RK84MatrixModel()
        m.process(*POS_A, True)
        m.process(*POS_FN, True)
        m.process(*POS_A, False)
        m.process(*POS_FN, False)
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_fn_down_f8_down_fn_up_f8_up(self):
        m = RK84MatrixModel()
        m.process(*POS_FN, True)
        m.process(*POS_F8, True)   # -> media play/pause
        m.process(*POS_FN, False)  # Fn released first
        m.process(*POS_F8, False)
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_shift_fn_media(self):
        m = RK84MatrixModel()
        m.process(4, 0, True)  # LSHIFT
        m.process(*POS_FN, True)
        m.process(*POS_F8, True)
        m.process(*POS_F8, False)
        m.process(*POS_FN, False)
        m.process(4, 0, False)
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_fn_never_in_report(self):
        m = RK84MatrixModel()
        m.process(*POS_FN, True)
        m.process(*POS_A, True)
        self.assertNotIn(MO(1), m.hid_keys)
        self.assertIn(KC["KC_A"], m.hid_keys)
        m.process(*POS_A, False)
        m.process(*POS_FN, False)

    def test_no_stuck_after_fn_release(self):
        m = RK84MatrixModel()
        m.process(*POS_A, True)
        m.process(*POS_FN, True)
        m.process(*POS_FN, False)  # Fn up, A still held
        self.assertEqual(m.hid_keys, [KC["KC_A"]])
        self.assertEqual(m.stuck_keys(), [])
        m.process(*POS_A, False)
        self.assertEqual(m.hid_keys, [])

    def test_transparent_falls_through(self):
        m = RK84MatrixModel()
        m.process(*POS_FN, True)
        m.process(*POS_A, True)  # transparent in layer1 -> base A
        self.assertIn(KC["KC_A"], m.hid_keys)
        m.process(*POS_A, False)
        m.process(*POS_FN, False)
        self.assertTrue(m.press_release_balance())


class SameFrameTests(unittest.TestCase):
    def test_fn_and_f8_same_frame_is_media(self):
        """Fn (col 9) and F8 (col 8) change in the same completed frame.
        Bug-7 fix (SMK_LAYER_KEYS_FIRST): the momentary layer-key
        transition is processed FIRST, so F8 resolves as its Fn-layer
        media action instead of plain F8."""
        m = RK84MatrixModel()
        m.process_frame([
            (*POS_F8, True),
            (*POS_FN, True),
        ])
        # F8 press now resolves AFTER Fn took effect -> media action
        # (KC_MEDIA_PLAY_PAUSE = 0xAE), not plain F8 (KC_F8 = 0x41)
        self.assertIn((0xAE, True), m.report_log, "F8 must resolve to its Fn media action")
        self.assertNotIn((KC["KC_F8"], True), m.report_log, "F8 must not emit plain F8")
        # releases balance, nothing stuck
        m.process_frame([
            (*POS_FN, False),
            (*POS_F8, False),
        ])
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_sequential_fn_then_f8_is_media(self):
        """Fn pressed in a PRIOR frame, then F8: media (the normal case)."""
        m = RK84MatrixModel()
        m.process_frame([(*POS_FN, True)])
        m.process_frame([(*POS_F8, True)])
        self.assertIn((MEDIA_PLAY_PAUSE, True), m.report_log)
        self.assertNotIn((KC["KC_F8"], True), m.report_log)
        m.process_frame([(*POS_F8, False), (*POS_FN, False)])
        self.assertTrue(m.press_release_balance())

    def test_unstable_column(self):
        m = RK84MatrixModel()
        m.process(0, 3, True)   # F3 col3 row0
        m.process(1, 3, True)   # something col3 row1 (base)
        m.process(2, 3, True)
        m.process(1, 3, False)
        m.process(2, 3, False)
        # only the still-held keys remain
        self.assertEqual(len(m.hid_keys), 1)
        m.process(0, 3, False)
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])


class RecoveryModeTests(unittest.TestCase):
    def test_recovery_ignores_matrix(self):
        m = RK84MatrixModel(recovery_only=True)
        m.process(*POS_A, True)
        m.process(*POS_FN, True)
        self.assertEqual(m.report_log, [])
        self.assertEqual(m.hid_keys, [])


if __name__ == "__main__":
    unittest.main()
