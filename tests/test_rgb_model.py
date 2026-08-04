"""RGB scheduler invariant tests (independent matrix/RGB counters)."""
from __future__ import annotations

import unittest

from .rgb_model import (
    BRIGHTNESS_MAX,
    INITIAL_BRIGHTNESS,
    INITIAL_SOURCE,
    MATRIX_COLS,
    PWM_PERIOD,
    RGB_COLS,
    RGB_COMPONENTS,
    RGB_PHASES,
    RGB_ROWS,
    SAFE_DUTY,
    SINK_PHASES,
    RGBSchedulerModel,
)


class CounterTests(unittest.TestCase):
    def test_matrix_cols_0_15_then_wrap(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        first16 = [m.step() for _ in range(16)]
        self.assertEqual(first16, list(range(16)))
        self.assertEqual(m.step(), 0)  # wraps after 16

    def test_rgb_phases_0_18_then_wrap(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        phases = []
        for _ in range(19):
            m.step()
            phases.append(m.rgb_phase)
        # after 19 steps, rgb_phase wrapped to 0 (next phase is 0)
        self.assertEqual(phases[-1], 0)
        # capture the phases seen
        m2 = RGBSchedulerModel("rgb")
        m2.start()
        seen = []
        for _ in range(19):
            seen.append(m2.rgb_phase)
            m2.step()
        self.assertEqual(seen, list(range(19)))

    def test_combined_schedule_repeats_at_304(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        # sample the (matrix_col, rgb_phase) pair every step
        pairs = []
        for _ in range(304 + 16):
            pairs.append((m.matrix_col, m.rgb_phase))
            m.step()
        self.assertEqual(pairs[0], pairs[304])
        self.assertEqual(pairs[15], pairs[304 + 15])
        # and NOT earlier (304 = lcm(16,19))
        self.assertNotEqual(pairs[0], pairs[16 * 19 // 2])

    def test_matrix_mode_no_sinks(self):
        m = RGBSchedulerModel("matrix")
        m.start()
        for _ in range(RGB_PHASES * 4):
            m.step()
        self.assertEqual(m.invariants(), [])

    def test_recovery_neither(self):
        m = RGBSchedulerModel("recovery")
        m.start()
        for _ in range(RGB_PHASES * 4):
            m.step()
        self.assertEqual(m.invariants(), [])

    def test_matrix_scan_present_in_matrix_mode(self):
        m = RGBSchedulerModel("matrix")
        m.start()
        for _ in range(MATRIX_COLS * 3):
            m.step()
        self.assertTrue(m.scan_log)
        self.assertEqual(len(m.scan_log), MATRIX_COLS * 3)


class SchedulerTests(unittest.TestCase):
    def test_recovery_zero_writes(self):
        m = RGBSchedulerModel("recovery")
        m.start()
        for _ in range(304):
            m.step()
        self.assertEqual(m.pwm_writes, 0)
        self.assertEqual(m.sink_writes, 0)

    def test_rgb_one_cell_low_duty(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        self.assertEqual(m.brightness, INITIAL_BRIGHTNESS)
        self.assertEqual(m.fb[0][0], INITIAL_SOURCE)
        for _ in range(RGB_PHASES):
            m.step()
        self.assertLessEqual(m.max_duty_written, SAFE_DUTY)
        self.assertEqual(m.invariants(), [])

    def test_max_duty_below_period(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        m.brightness = BRIGHTNESS_MAX
        m.fb[0][0] = 255
        for _ in range(RGB_PHASES):
            m.step()
        self.assertLess(m.max_duty_written, PWM_PERIOD)

    def test_sink_phase_range(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        for _ in range(RGB_PHASES * 2):
            m.step()
        self.assertLessEqual(m.sink_writes, SINK_PHASES * 2)

    def test_brightness_bounds(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        for _ in range(10):
            m.brightness_up()
        self.assertEqual(m.brightness, BRIGHTNESS_MAX)
        for _ in range(10):
            m.brightness_down()
        self.assertEqual(m.brightness, 0)


class GeometryTests(unittest.TestCase):
    def test_framebuffer_dimensions(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        for comp in range(RGB_COMPONENTS):
            self.assertEqual(len(m.fb[comp]), RGB_ROWS * RGB_COLS)

    def test_sink_table_size(self):
        self.assertEqual(SINK_PHASES, RGB_ROWS * RGB_COMPONENTS)

    def test_phase_count(self):
        self.assertEqual(RGB_PHASES, 1 + RGB_ROWS * RGB_COMPONENTS)

    def test_safe_duty_value(self):
        self.assertEqual(SAFE_DUTY, 128)


if __name__ == "__main__":
    unittest.main()
