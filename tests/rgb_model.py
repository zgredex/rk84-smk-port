"""Reference model of the RK84 RGB scheduler invariants.

Two INDEPENDENT counters advance every PWM interrupt:
  - matrix current_step: 0..15 (MATRIX_COLS), wraps every 16
  - rgb_phase:           0..18 (RGB_PHASES), wraps every 19

The combined schedule repeats after lcm(16, 19) = 304 interrupts.
"""
from __future__ import annotations

MATRIX_COLS = 16
RGB_COLS = 21
RGB_ROWS = 6
RGB_COMPONENTS = 3
RGB_PHASES = 19  # 0 = matrix only, 1..18 = 6 rows x 3 components
PWM_PERIOD = 0x0A00  # 2560
BRIGHTNESS_MAX = 5
INITIAL_BRIGHTNESS = 1
INITIAL_SOURCE = 64
SAFE_DUTY = INITIAL_SOURCE * INITIAL_BRIGHTNESS * 2  # 128
SINK_PHASES = 18  # 6 rows x 3 components


class RGBSchedulerModel:
    def __init__(self, mode: str = "rgb"):
        """mode: 'recovery' | 'matrix' | 'rgb'."""
        assert mode in ("recovery", "matrix", "rgb")
        self.mode = mode
        self.matrix_col = 0
        self.rgb_phase = 0
        self.brightness = 0
        self.fb = [[0] * (RGB_ROWS * RGB_COLS) for _ in range(RGB_COMPONENTS)]
        self.pwm_writes = 0
        self.sink_writes = 0
        self.scan_log = []
        self.max_duty_written = 0

    # ------------------------------------------------------------------
    def start(self):
        if self.mode == "recovery":
            return
        self.brightness = INITIAL_BRIGHTNESS
        if self.mode == "rgb":
            self.fb = [[0] * (RGB_ROWS * RGB_COLS) for _ in range(3)]
            self.fb[0][0] = INITIAL_SOURCE

    def step(self):
        """One PWM interrupt: scan one matrix column AND advance one
        RGB phase (they are independent)."""
        if self.mode == "recovery":
            return None

        scanned = self.matrix_col
        self.scan_log.append(scanned)

        if self.rgb_phase == 0:
            pass  # matrix-only phase
        else:
            if self.mode == "rgb":
                row = (self.rgb_phase - 1) // 3
                component = (self.rgb_phase - 1) % 3
                base = row * RGB_COLS
                for c in range(RGB_COLS):
                    source = self.fb[component][base + c]
                    duty = source * self.brightness * 2
                    self.pwm_writes += 1
                    self.max_duty_written = max(self.max_duty_written, duty)
                    assert 0 <= c < RGB_COLS
                assert 1 <= self.rgb_phase <= SINK_PHASES
                self.sink_writes += 1
            # matrix mode: no sink enables

        self.matrix_col = (self.matrix_col + 1) % MATRIX_COLS
        self.rgb_phase = (self.rgb_phase + 1) % RGB_PHASES
        return scanned

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
            if self.scan_log:
                errs.append("recovery mode scanned matrix")
            return errs

        for comp in range(RGB_COMPONENTS):
            if len(self.fb[comp]) != RGB_ROWS * RGB_COLS:
                errs.append(f"component {comp} framebuffer size wrong")
        if self.max_possible_duty() >= PWM_PERIOD:
            errs.append("max duty reaches/exceeds period")
        if not self.scan_log:
            errs.append("matrix never scanned")
        if self.mode == "matrix" and self.sink_writes != 0:
            errs.append("matrix mode enabled sinks")
        return errs
