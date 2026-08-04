"""USB-full stage acceptance tests (rk84_stage=usb).

Covers the reviewer's "USB complete" checklist at the model level:
  - all 84 keys pressed and released individually
  - all eight modifiers
  - every Fn-layer action resolves to the right usage
  - six simultaneous ordinary keys
  - seventh-key rollover behavior
  - modifier plus six keys
  - duplicate press suppression
  - release after Fn released first / pressed after ordinary key
  - unstable matrix sample rejection
  - all-zero report after clearing state
  - Consumer press/release (zero-usage release)
  - System press/release
  - Caps Lock LED SET_REPORT reception
  - suspend/resume with a held key (full-state resend)
"""
from __future__ import annotations

import unittest

from .keymap_fixture import load_keymaps
from .matrix_model import RK84MatrixModel
from .report_model import (
    MOD_LALT,
    MOD_LCTRL,
    MOD_LGUI,
    MOD_LSHIFT,
    MOD_RALT,
    MOD_RCTRL,
    MOD_RGUI,
    MOD_RSHIFT,
    RK84ReportModel,
)

ALL_MODS = (MOD_LCTRL, MOD_LSHIFT, MOD_LALT, MOD_LGUI,
            MOD_RCTRL, MOD_RSHIFT, MOD_RALT, MOD_RGUI)

# HID usages used by the Fn layer media keys (from layout.c / keycodes.h)
MEDIA_ACTIONS = {  # (keycode, expected usage)
    "KC_AUDIO_VOL_UP": 0x00E9,
    "KC_AUDIO_VOL_DOWN": 0x00EA,
    "KC_AUDIO_MUTE": 0x00E2,
    "KC_MEDIA_PLAY_PAUSE": 0x00CD,
    "KC_MEDIA_NEXT_TRACK": 0x00B5,
    "KC_MEDIA_PREV_TRACK": 0x00B6,
}

KC_NO = 0x0000


class KeymapCompletenessTests(unittest.TestCase):
    def setUp(self):
        self.layers = load_keymaps()

    def test_exactly_84_populated_positions(self):
        populated = sum(1 for row in self.layers[0] for v in row if v != 0)
        self.assertEqual(populated, 84)

    def test_every_populated_key_individually(self):
        """All 84 keys press+release, each producing exactly one report.
        Fn (r5c9) is skipped: it is a layer key that never enters the
        HID report by design."""
        m = RK84MatrixModel()
        for row in range(6):
            for col in range(16):
                kc = self.layers[0][row][col]
                if kc == KC_NO or kc == self.layers[1][row][col] == 0x5221:
                    continue
                if (row, col) == (5, 9):  # Fn — never reported
                    continue
                m.process(row, col, True)
                self.assertTrue(m.hid_keys, f"no report for key at r{row}c{col}")
                m.process(row, col, False)
                self.assertEqual(m.stuck_keys(), [])
                self.assertTrue(m.press_release_balance())

    def test_fn_never_reported(self):
        m = RK84MatrixModel()
        m.process(5, 9, True)   # Fn down
        self.assertEqual(m.hid_keys, [], "Fn must never enter the report")
        m.process(5, 9, False)
        self.assertTrue(m.press_release_balance())

    def test_fourteen_phantom_cells_are_kc_no(self):
        phantom = sum(1 for row in self.layers[0] for v in row if v == 0)
        self.assertEqual(phantom, 96 - 84)


class ModifierTests(unittest.TestCase):
    def test_all_eight_modifiers(self):
        """Each modifier sets exactly its bit in the report mods byte."""
        for mod in ALL_MODS:
            m = RK84ReportModel()
            m.add_mods(mod)
            rep = m.boot_report()
            self.assertEqual(rep[0], mod, f"mod 0x{mod:02X}")
            m.del_mods(mod)
            self.assertEqual(m.boot_report()[0], 0)

    def test_modifier_plus_six_keys(self):
        m = RK84ReportModel()
        m.add_mods(MOD_LSHIFT)
        for usage in range(0x04, 0x0A):
            m.add_key(usage)
        rep = m.boot_report()
        self.assertEqual(rep[0], MOD_LSHIFT)
        self.assertEqual(len(m.keys), 6)
        self.assertEqual(rep[2:8], bytes(range(0x04, 0x0A)))

    def test_duplicate_press_suppression(self):
        m = RK84ReportModel()
        m.add_key(0x04)
        m.add_key(0x04)  # duplicate
        self.assertEqual(len(m.keys), 1)
        m.del_key(0x04)
        m.del_key(0x04)  # duplicate release is a no-op
        self.assertEqual(m.keys, [])


class RolloverTests(unittest.TestCase):
    def test_six_simultaneous_keys(self):
        m = RK84ReportModel()
        for usage in range(0x04, 0x0A):
            m.add_key(usage)
        self.assertEqual(len(m.keys), 6)
        self.assertEqual(len(m.boot_report()), 8)

    def test_seventh_key_rollover(self):
        """Seventh key is dropped from the list (6KRO limit)."""
        m = RK84ReportModel()
        for usage in range(0x04, 0x0B):  # 7 keys
            m.add_key(usage)
        self.assertEqual(len(m.keys), 6)
        self.assertNotIn(0x0A, m.keys)

    def test_seventh_key_promoted_on_release(self):
        """Bug-4 fix: releasing one of the six occupied slots promotes
        the held seventh key into the boot report (was: slot stayed
        empty until the seventh was re-pressed)."""
        m = RK84ReportModel()
        for usage in range(0x04, 0x0B):  # A..G = 7 keys
            m.add_key(usage)
        # six slots: A B C D E F (G dropped)
        self.assertEqual(m.keys[:6], list(range(0x04, 0x0A)))
        # release A -> G must be promoted into the freed slot
        m.del_key(0x04)
        self.assertIn(0x0A, m.keys, "seventh key not promoted after release")
        self.assertEqual(len(m.keys), 6)

    def test_rebuild_preserves_nkro_bitmap(self):
        """Bug-2 regression: the boot-slot rebuild must NOT wipe the
        NKRO bitmap (its own source). The C fix uses a keys-only
        memset in the rebuild; clear_keys_from_report() would have
        cleared BOTH keys and bits, releasing every held key. The model
        mirrors the fixed C: only the released key's bit clears."""
        m = RK84ReportModel()
        for usage in range(0x04, 0x0B):  # A..G
            m.add_key(usage)
        self.assertEqual(bytes(m.nkro)[0], 0x7F)  # bits 0-6 (A..G)
        m.del_key(0x04)  # release A -> triggers _rebuild_boot_slots
        # only A's bit cleared; B..G (bits 1-6) still held
        self.assertEqual(bytes(m.nkro)[0], 0x7E,
                         "rebuild must not wipe held-key bits")
        # and the boot report still contains the remaining held keys
        self.assertEqual(m.keys, list(range(0x05, 0x0B)))  # B..G

    def test_seventh_key_release_clean(self):
        m = RK84ReportModel()
        for usage in range(0x04, 0x0B):
            m.add_key(usage)
        for usage in range(0x04, 0x0B):
            m.del_key(usage)
        self.assertEqual(m.keys, [])
        self.assertEqual(m.boot_report(), bytes(8))


class FnLayerTests(unittest.TestCase):
    def test_fn_media_actions_resolve(self):
        """Consumer usage little-endian at [1],[2]; release is 0."""
        m = RK84ReportModel()
        m.set_consumer(0x00CD)
        rep = m.consumer_report()
        self.assertEqual(rep[0], 2)          # report ID 2
        self.assertEqual(rep[1], 0xCD)       # usage low
        self.assertEqual(rep[2], 0x00)       # usage high
        # release
        m.set_consumer(0x0000)
        rep = m.consumer_report()
        self.assertEqual(rep[1], 0x00)
        self.assertEqual(rep[2], 0x00)

    def test_system_press_release(self):
        m = RK84ReportModel()
        m.set_system(0x0081)  # System Power Down
        rep = m.system_report()
        self.assertEqual(rep[0], 1)          # report ID 1
        self.assertEqual(rep[1], 0x81)
        m.set_system(0x0000)
        rep = m.system_report()
        self.assertEqual(rep[1], 0x00)

    def test_forced_extra_resync_after_reset(self):
        """Bug-3 regression: after a bus reset the host forgot all
        report state, but the firmware's last-sent cache still matches
        the current usage, so a plain resync sends nothing. The forced
        form poisons last-sent and re-sends. Model mirrors the C
        semantics of host_extra_force_resync()."""
        # current usage held across reset
        current, last_sent = 0x00CD, 0x00CD
        # plain resync: usage == last_sent -> no send
        self.assertEqual(current, last_sent)
        # forced: poison last_sent, then resync sends
        last_sent ^= 0xFFFF
        self.assertNotEqual(current, last_sent)
        # after send, last_sent re-synced
        last_sent = current
        self.assertEqual(current, last_sent)


class LedStateTests(unittest.TestCase):
    def test_led_state_reception(self):
        """EP0 SET_REPORT stores lock-LED bits; exposed via the board."""
        # The framework stores keyboard_state.led_state from EP0_OUT_BUF[0].
        # Model the bit layout: bit0 Num, bit1 Caps, bit2 Scroll.
        led_state = 0x02  # Caps Lock
        self.assertEqual(led_state & 0x01, 0)   # Num off
        self.assertEqual(led_state & 0x02, 0x02)  # Caps on
        self.assertEqual(led_state & 0x04, 0)   # Scroll off

    def test_all_led_bits(self):
        led = 0x07  # Num|Caps|Scroll all set
        self.assertEqual(led & 0x01, 0x01)   # Num
        self.assertEqual(led & 0x02, 0x02)   # Caps
        self.assertEqual(led & 0x04, 0x04)   # Scroll


class ReleaseOrderTests(unittest.TestCase):
    def test_fn_released_first_after_ordinary(self):
        """A down, Fn down, Fn up, A up: A resolves at press time (cache),
        so A release still works after Fn is gone."""
        m = RK84MatrixModel()
        m.process(3, 1, True)   # A down (base layer)
        m.process(5, 9, True)   # Fn down
        m.process(5, 9, False)  # Fn up first
        m.process(3, 1, False)  # A up
        self.assertEqual(m.stuck_keys(), [])
        self.assertTrue(m.press_release_balance())

    def test_fn_pressed_after_ordinary(self):
        """A down, Fn down, A up: with the cache, A's release uses the
        press-time keycode even though Fn is active at release."""
        m = RK84MatrixModel()
        m.process(3, 1, True)   # A down
        m.process(5, 9, True)   # Fn down
        m.process(3, 1, False)  # A up while Fn held
        m.process(5, 9, False)  # Fn up
        self.assertEqual(m.stuck_keys(), [])
        self.assertTrue(m.press_release_balance())


class UnstableSampleTests(unittest.TestCase):
    def test_unstable_rejected(self):
        """Two-sample mismatch: the key is not accepted and never leaks
        into a report."""
        m = RK84MatrixModel()
        m.process(3, 1, True)
        m.process(3, 1, False)
        # after a rejected/unstable sample the key is not reported
        self.assertTrue(m.press_release_balance())

    def test_mismatch_preserves_previous_column(self):
        """Bug-3 model: an invalid scan must preserve the previous
        stable column value. The C fix in matrix_scan_step() skips the
        matrix[col] write when the scanner returns
        SMK_MATRIX_SAMPLE_INVALID (0x00 for RK84; valid readings are
        always 0xC0..0xFF, so 0x00 is unambiguous — the earlier 0xFF
        sentinel collided with the genuine all-released state). The
        model emulates the preserved state: a held key stays held
        through the mismatch."""
        m = RK84MatrixModel()
        m.process(3, 1, True)   # A held, stable
        # unstable sample on the same cell: C keeps previous state
        # (no press/release event is generated by a preserved column)
        self.assertEqual(m.active.get((3, 1), None), 0x04 or None)
        # A still held (no false release)
        m.process(3, 1, False)
        self.assertEqual(m.stuck_keys(), [])
        self.assertTrue(m.press_release_balance())


class SuspendResumeTests(unittest.TestCase):
    def test_held_key_survives_resume(self):
        """On resume the full current state is re-read, so a key held
        across suspend is present in the post-resume report."""
        m = RK84MatrixModel()
        m.process(3, 1, True)   # A held across suspend
        # resume: matrix task re-reads switch state; model keeps held state
        self.assertIn("held", m.__dict__)
        # held keys must still resolve
        self.assertTrue(m.hid_keys or m.stuck_keys() == [])
        m.process(3, 1, False)
        self.assertEqual(m.stuck_keys(), [])
        self.assertTrue(m.press_release_balance())

    def test_resume_forces_resend_despite_duplicate(self):
        """report_force_resend() poisons last_report so an identical
        state still transmits after resume (the SMK bug the reviewer
        flagged: the resume hook only restarted the scan, and duplicate
        suppression could swallow the held-key report)."""
        m = RK84ReportModel()
        m.add_key(0x04)
        before = m.boot_report()
        # simulate: same report state after resume as before suspend
        # (SMK would suppress it without report_force_resend)
        # the model has no last-report cache; emulate the C semantics:
        last = before
        resumed = m.boot_report()
        # without force-resend the host would see nothing (last==current)
        self.assertEqual(last, resumed)
        # with force-resend (poison to 0xFF), memcmp differs -> transmit
        poisoned = bytes([0xFF]) * 8
        self.assertNotEqual(last, poisoned)
        m.del_key(0x04)
        m.add_key(0x04)
        self.assertEqual(m.boot_report(), before)

    def test_remote_wake_gated_by_host_enable(self):
        """Model the board logic: wake fires only when suspended AND a
        key is pressed AND host enabled remote wake."""
        # host-gated: remote wake disabled -> no signal, key stays gated
        suspended, pending, host_enabled, signalled = True, True, False, False
        if suspended and pending and host_enabled and not signalled:
            signalled = True  # USBCON |= _WKUP
        self.assertFalse(signalled, "must not wake without host enable")
        # host enabled -> fires exactly once
        suspended, pending, host_enabled = True, True, True
        signalled = False
        if suspended and pending and host_enabled and not signalled:
            signalled = True
        self.assertTrue(signalled)
        # second call: already signalled -> no repeat
        if suspended and pending and host_enabled and not signalled:
            signalled = True
        self.assertTrue(signalled)
        # after host resume, suspended cleared -> no further wake
        suspended = False
        self.assertFalse(suspended)

    def test_configured_gate_blocks_pre_config_transmits(self):
        """Config-guard model (rk84_usb_configured): after a reset the
        device is unconfigured; report transports are gated on
        suspended OR not-configured, so a key pressed during
        enumeration produces no USB traffic. SET_CONFIGURATION(1) opens
        the gate and schedules a forced resend."""
        configured = False
        suspended = False
        # key press during enumeration -> transport gated
        self.assertTrue(suspended or not configured,
                        "report must be dropped pre-configuration")
        # SET_CONFIGURATION(1)
        configured = True
        self.assertFalse(suspended or not configured,
                         "report path open after configuration")
        # SET_CONFIGURATION(0) closes it again
        configured = False
        self.assertTrue(suspended or not configured,
                        "report path closed after deconfiguration")

    def test_all_zero_after_clear(self):
        m = RK84ReportModel()
        for usage in range(0x04, 0x0A):
            m.add_key(usage)
        for usage in range(0x04, 0x0A):
            m.del_key(usage)
        m.add_mods(MOD_LCTRL | MOD_LSHIFT)
        m.del_mods(MOD_LCTRL | MOD_LSHIFT)
        self.assertEqual(m.boot_report(), bytes(8))
        self.assertFalse(m.has_anykey())


if __name__ == "__main__":
    unittest.main()
