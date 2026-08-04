"""Matrix + layer logic tests (sticky-key / Fn sequences)."""
from __future__ import annotations

import unittest

from .matrix_model import MO_MASK, KC_NO, RK84MatrixModel


class StickyKeyTests(unittest.TestCase):
    def test_a_down_fn_down_a_up_fn_up(self):
        """Classic sticky-key sequence: A released while Fn held must
        release as A, not resolve through layer 1 to something else."""
        m = RK84MatrixModel()
        m.process(0, 0, True)   # A down (base)
        m.process(5, 9, True)   # Fn down (layer 1)
        m.process(0, 0, False)  # A up
        m.process(5, 9, False)  # Fn up
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_fn_down_f8_down_fn_up_f8_up(self):
        """Media key pressed under Fn, Fn released first."""
        m = RK84MatrixModel()
        m.process(5, 9, True)   # Fn down
        m.process(0, 5, True)   # F8 down -> media play/pause
        m.process(5, 9, False)  # Fn up FIRST
        m.process(0, 5, False)  # F8 up
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_shift_fn_media(self):
        """Shift held, Fn+media, releases."""
        m = RK84MatrixModel()
        m.process(0, 0, True)   # A (shift-like normal key)
        m.process(5, 9, True)   # Fn
        m.process(0, 5, True)   # F8 -> media
        m.process(0, 5, False)
        m.process(5, 9, False)
        m.process(0, 0, False)
        self.assertTrue(m.press_release_balance())
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])

    def test_fn_never_in_report(self):
        m = RK84MatrixModel()
        m.process(5, 9, True)   # Fn down
        m.process(0, 0, True)   # A down
        self.assertNotIn(MO_MASK | 1, m.hid_keys)
        self.assertIn(0x04, m.hid_keys)  # A is sent
        m.process(0, 0, False)
        m.process(5, 9, False)

    def test_no_stuck_after_fn_release(self):
        """Keys held across Fn release stay held until physical release."""
        m = RK84MatrixModel()
        m.process(0, 0, True)   # A down
        m.process(5, 9, True)   # Fn down
        m.process(5, 9, False)  # Fn up (A still held)
        self.assertEqual(m.hid_keys, [0x04])  # A still sent
        self.assertEqual(m.stuck_keys(), [])
        m.process(0, 0, False)
        self.assertEqual(m.hid_keys, [])

    def test_transparent_falls_through(self):
        """Fn+key with KC_TRANSPARENT resolves to base keycode."""
        m = RK84MatrixModel()
        m.process(5, 9, True)   # Fn down
        m.process(0, 1, True)   # some transparent cell -> base (0x04)
        m.process(0, 1, False)
        m.process(5, 9, False)
        self.assertTrue(m.press_release_balance())


class SameFrameTests(unittest.TestCase):
    def test_fn_and_f8_same_frame(self):
        """Fn + F8 active in the same completed scan frame. Order of
        processing matters; the stored-keycode cache must not leave a
        plain-F8 event or a stuck key. We simulate both orders."""
        for order in [(5, 9), (0, 5)], [(0, 5), (5, 9)]:
            with self.subTest(order=order):
                m = RK84MatrixModel()
                for r, c in order:
                    m.process(r, c, True)
                # both down; whichever resolved first, releases balance
                for r, c in reversed(order):
                    m.process(r, c, False)
                self.assertTrue(m.press_release_balance())
                self.assertEqual(m.hid_keys, [])

    def test_unstable_column(self):
        """A column with several keys changing while unstable: no key
        may end up stuck or double-pressed."""
        m = RK84MatrixModel()
        # 3 keys on col 0 go down, 2 up, 1 stays
        m.process(0, 0, True)
        m.process(1, 0, True)
        m.process(2, 0, True)
        m.process(1, 0, False)
        m.process(2, 0, False)
        self.assertEqual(len(m.hid_keys), 1)  # only (0,0) held
        m.process(0, 0, False)
        self.assertEqual(m.hid_keys, [])
        self.assertEqual(m.stuck_keys(), [])


class RecoveryModeTests(unittest.TestCase):
    def test_recovery_ignores_matrix(self):
        m = RK84MatrixModel(recovery_only=True)
        m.process(0, 0, True)
        m.process(5, 9, True)
        self.assertEqual(m.report_log, [])
        self.assertEqual(m.hid_keys, [])


if __name__ == "__main__":
    unittest.main()
