"""Report generation tests using the RK84 reference model."""
from __future__ import annotations

import unittest

from .report_model import (
    MOD_LCTRL,
    MOD_LSHIFT,
    MOD_RALT,
    NKRO_FIRST_USAGE,
    NKRO_LAST_USAGE,
    NKRO_REPORT_BITS,
    RK84ReportModel,
)

# HID usages
KC_A = 0x04
KC_F8 = 0x42
KC_ENTER = 0x28
MEDIA_PLAY_PAUSE = 0x00CD  # transport play/pause


class BootReportTests(unittest.TestCase):
    def test_empty_report(self):
        m = RK84ReportModel()
        self.assertEqual(m.boot_report(), bytes(8))

    def test_modifier_bits(self):
        m = RK84ReportModel()
        m.add_mods(MOD_LSHIFT | MOD_LCTRL)
        self.assertEqual(m.boot_report(), bytes([0x03, 0, 0, 0, 0, 0, 0, 0]))

    def test_single_key(self):
        m = RK84ReportModel()
        m.add_key(KC_A)
        r = m.boot_report()
        self.assertEqual(r[1], 0)  # reserved
        self.assertEqual(r[2], KC_A)
        self.assertEqual(r[3:], bytes(5))

    def test_rollover_six_keys(self):
        m = RK84ReportModel()
        for usage in range(KC_A, KC_A + 6):
            m.add_key(usage)
        r = m.boot_report()
        self.assertEqual(list(r[2:]), list(range(KC_A, KC_A + 6)))

    def test_release_all(self):
        m = RK84ReportModel()
        for usage in range(KC_A, KC_A + 4):
            m.add_key(usage)
        for usage in range(KC_A, KC_A + 4):
            m.del_key(usage)
        self.assertEqual(m.boot_report(), bytes(8))


class NkroTests(unittest.TestCase):
    def test_first_and_last_usage(self):
        m = RK84ReportModel()
        m.add_key(NKRO_FIRST_USAGE)
        self.assertEqual(m.nkro_report()[1], 0x01)
        m = RK84ReportModel()
        m.add_key(NKRO_LAST_USAGE)
        idx = NKRO_LAST_USAGE - NKRO_FIRST_USAGE
        self.assertEqual(m.nkro_report()[1 + (idx >> 3)], 1 << (idx & 7))

    def test_usage_above_0x70_rejected(self):
        m = RK84ReportModel()
        m.add_key(NKRO_LAST_USAGE + 1)
        self.assertEqual(bytes(m.nkro), bytes(NKRO_REPORT_BITS))

    def test_report_size_16(self):
        m = RK84ReportModel()
        m.add_key(KC_A)
        self.assertEqual(len(m.nkro_report()), 16)

    def test_no_modifier_byte_in_nkro(self):
        """Stock format: ID 6 + 15 bitmap, NO mods byte."""
        m = RK84ReportModel()
        m.add_mods(MOD_LCTRL)
        m.add_key(KC_A)
        r = m.nkro_report()
        self.assertEqual(r[0], 6)
        self.assertEqual(r[1], 0x01)  # first bitmap byte = usage 0x04
        self.assertEqual(len(r), 16)

    def test_get_first_key(self):
        m = RK84ReportModel()
        self.assertEqual(m.get_first_key(), 0)  # KC_NO on empty
        m.add_key(KC_F8)
        self.assertEqual(m.get_first_key(), KC_F8)

    def test_simultaneous_press(self):
        m = RK84ReportModel()
        m.add_mods(MOD_LSHIFT)
        m.add_key(KC_A)
        m.add_key(KC_ENTER)
        self.assertTrue(m.has_anykey())
        r = m.boot_report()
        self.assertEqual(r[0], MOD_LSHIFT)


class SystemConsumerTests(unittest.TestCase):
    def test_system_report_two_bytes(self):
        m = RK84ReportModel()
        m.set_system(0x01)  # Power
        r = m.system_report()
        self.assertEqual(r, bytes([1, 0x01]))
        self.assertEqual(len(r), 2)

    def test_consumer_report_three_bytes(self):
        m = RK84ReportModel()
        m.set_consumer(MEDIA_PLAY_PAUSE)
        r = m.consumer_report()
        self.assertEqual(len(r), 3)
        self.assertEqual(r[0], 2)
        self.assertEqual(r[1] | (r[2] << 8), MEDIA_PLAY_PAUSE)

    def test_consumer_le(self):
        m = RK84ReportModel()
        m.set_consumer(0x0102)
        self.assertEqual(m.consumer_report(), bytes([2, 0x02, 0x01]))

    def test_dual_and_single_both_emit_nkro(self):
        m = RK84ReportModel(dual=False)
        m.add_key(KC_A)
        reports = m.send_all()
        self.assertEqual(len(reports), 2)  # boot + nkro


class GoldenFixtureTests(unittest.TestCase):
    """Stock-format golden reports (recovered from the original firmware)."""

    def test_golden_empty(self):
        m = RK84ReportModel()
        self.assertEqual(m.boot_report(), bytes(8))

    def test_golden_a_pressed(self):
        m = RK84ReportModel()
        m.add_key(KC_A)
        self.assertEqual(
            m.boot_report(),
            bytes([0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]),
        )

    def test_golden_shift_a(self):
        m = RK84ReportModel()
        m.add_mods(MOD_LSHIFT)
        m.add_key(KC_A)
        self.assertEqual(
            m.boot_report(),
            bytes([0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]),
        )

    def test_golden_media_play(self):
        m = RK84ReportModel()
        m.set_consumer(MEDIA_PLAY_PAUSE)
        self.assertEqual(m.consumer_report(), bytes([0x02, 0xCD, 0x00]))


if __name__ == "__main__":
    unittest.main()
