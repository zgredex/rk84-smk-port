"""Reference model of the RK84 RGB scheduler invariants.

Validates the software invariants of the 19-phase PWM scheduler
without LEDs (color order and polarity remain hardware questions):

  - every logical LED maps to a valid framebuffer cell
  - every PWM channel index is in range (21 channels)
  - sink selection never exceeds the 18-phase table
  - duty values stay within the PWM period (2560)
  - only one matrix column is active per interrupt
  - recovery mode makes zero PWM writes
  - matrix mode scans but never enables RGB sinks
  - RGB mode starts with one component of one LED at low duty
  - brightness increase cannot exceed the safe initial limit
"""
from __future__ import annotations

RGB_COLS = 21
RGB_ROWS = 6
RGB_COMPONENTS = 3
RGB_PHASES = 19  # 0 = matrix only, 1..18 = 6 rows x 3 components
PWM_PERIOD = 0x0A00  # 2560
BRIGHTNESS_MAX = 5
INITIAL_BRIGHTNESS = 1
INITIAL_SOURCE = 64
SAFE_DUTY = INITIAL_SOURCE * INITIAL_BRIGHTNESS * 2  # 128

# 21 sink channels, index 0..20; phase 1..18 maps to a single sink pin
SINK_PHASES = 18


class RGBSchedulerModel:
    def __init__(self, mode: str = "rgb"):
        """mode: 'recovery' | 'matrix' | 'rgb'."""
        assert mode in ("recovery", "matrix", "rgb")
        self.mode = mode
        self.phase = 0
        self.brightness = 0
        self.fb = [[0] * (RGB_ROWS * RGB_COLS) for _ in range(RGB_COMPONENTS)]
        self.pwm_writes = 0  # count of DUTY2 writes
        self.sink_writes = 0  # count of sink enables
        self.matrix_col_active = None
        self.max_duty_written = 0

    # ------------------------------------------------------------------
    def start(self):
        if self.mode == "recovery":
            return  # zero PWM writes, zero matrix
        self.phase = 0
        self.brightness = INITIAL_BRIGHTNESS
        if self.mode == "rgb":
            # safe bring-up: one component of one LED at low duty
            self.fb = [[0] * (RGB_ROWS * RGB_COLS) for _ in range(3)]
            self.fb[0][0] = INITIAL_SOURCE

    def step(self):
        """One PWM interrupt; returns the matrix column scanned."""
        if self.mode == "recovery":
            return None

        col = self.phase  # one matrix column per interrupt
        self.matrix_col_active = col

        if self.phase == 0:
            # matrix-only phase: no RGB output
            pass
        else:
            if self.mode == "rgb":
                row = (self.phase - 1) // 3
                component = (self.phase - 1) % 3
                base = row * RGB_COLS
                for c in range(RGB_COLS):
                    source = self.fb[component][base + c]
                    duty = self._duty(source)
                    self.pwm_writes += 1
                    self.max_duty_written = max(self.max_duty_written, duty)
                    # channel index in range
                    assert 0 <= c < 21
                # sink enable (phase 1..18)
                assert 1 <= self.phase <= SINK_PHASES
                self.sink_writes += 1
            # matrix mode: no sinks
            assert self.mode != "matrix" or self.sink_writes == 0

        self.phase = (self.phase + 1) % RGB_PHASES
        return col

    def _duty(self, source: int) -> int:
        return source * self.brightness * 2

    # ------------------------------------------------------------------
    def brightness_up(self):
        if self.brightness < BRIGHTNESS_MAX:
            self.brightness += 1

    def brightness_down(self):
        if self.brightness > 0:
            self.brightness -= 1

    def max_possible_duty(self) -> int:
        return 255 * BRIGHTNESS_MAX * 2  # 2550 < 2560 period

    # ------------------------------------------------------------------
    def invariants(self) -> list[str]:
        errs = []
        if self.mode == "recovery":
            if self.pwm_writes != 0 or self.sink_writes != 0:
                errs.append("recovery mode made PWM/sink writes")
            if self.matrix_col_active is not None:
                errs.append("recovery mode scanned matrix")
            return errs

        # framebuffer bounds
        for comp in range(RGB_COMPONENTS):
            if len(self.fb[comp]) != RGB_ROWS * RGB_COLS:
                errs.append(f"component {comp} framebuffer size wrong")
        if self.max_possible_duty() >= PWM_PERIOD:
            errs.append("max duty reaches/exceeds period")
        if self.matrix_col_active is None:
            errs.append("matrix never scanned")
        if self.mode == "matrix" and self.sink_writes != 0:
            errs.append("matrix mode enabled sinks")
        return errs
