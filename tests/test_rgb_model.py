"""RGB scheduler invariant tests using the reference model."""
from __future__ import annotations

import unittest

from .rgb_model import (
    BRIGHTNESS_MAX,
    INITIAL_BRIGHTNESS,
    INITIAL_SOURCE,
    PWM_PERIOD,
    RGB_COLS,
    RGB_COMPONENTS,
    RGB_PHASES,
    RGB_ROWS,
    SAFE_DUTY,
    SINK_PHASES,
    RGBSchedulerModel,
)


class SchedulerTests(unittest.TestCase):
    def test_recovery_zero_writes(self):
        m = RGBSchedulerModel("recovery")
        m.start()
        for _ in range(RGB_PHASES * 4):
            m.step()
        self.assertEqual(m.invariants(), [])

    def test_matrix_no_sinks(self):
        m = RGBSchedulerModel("matrix")
        m.start()
        for _ in range(RGB_PHASES * 4):
            m.step()
        self.assertEqual(m.invariants(), [])

    def test_rgb_one_cell_low_duty(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        self.assertEqual(m.brightness, INITIAL_BRIGHTNESS)
        # one component of one LED lit at source 64
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

    def test_one_matrix_col_per_interrupt(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        cols = []
        for _ in range(RGB_PHASES):
            cols.append(m.step())
        self.assertEqual(cols, list(range(RGB_PHASES)))

    def test_sink_phase_range(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        for _ in range(RGB_PHASES):
            m.step()
        # each render phase enabled exactly one sink, max 18 total
        self.assertLessEqual(m.sink_writes, SINK_PHASES)

    def test_phase_wraparound(self):
        m = RGBSchedulerModel("rgb")
        m.start()
        for _ in range(RGB_PHASES * 3):
            m.step()
        self.assertEqual(m.phase % RGB_PHASES, 0)

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
        # 3 components x (6 rows x 21 cols) = 378 cells
        m = RGBSchedulerModel("rgb")
        m.start()
        for comp in range(RGB_COMPONENTS):
            self.assertEqual(len(m.fb[comp]), RGB_ROWS * RGB_COLS)

    def test_sink_table_size(self):
        self.assertEqual(SINK_PHASES, (RGB_ROWS * RGB_COMPONENTS))

    def test_phase_count(self):
        # 19 phases: 0 (matrix) + 18 (6 rows x 3 components)
        self.assertEqual(RGB_PHASES, 1 + RGB_ROWS * RGB_COMPONENTS)

    def test_safe_duty_value(self):
        self.assertEqual(SAFE_DUTY, 128)  # 64 x 1 x 2, 5% of 2560


if __name__ == "__main__":
    unittest.main()
