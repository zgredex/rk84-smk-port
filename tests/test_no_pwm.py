"""Tests for tools/check-recovery-no-pwm.py."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from . import (
    find_recovery_ihx,
    hex_data,
    hex_eof,
    hex_ela,
    make_hex,
    make_hex_file,
    run_tool,
    skip_if_no_build,
)

# ORL IEN1,#0x02 (EPWM0 enable); IEN1 = SFR 0xA9, bit 1
EPWM0 = bytes.fromhex("43 A9 02")
# MOV DPTR,#0xFF80; MOV A,#0xC2; MOVX @DPTR,A
PWM00CON_C2 = bytes.fromhex("90 FF 80 74 C2 F0")
# MOV DPTR,#0xFF80; MOV A,#0xCA; MOVX @DPTR,A
PWM00CON_CA = bytes.fromhex("90 FF 80 74 CA F0")


def ihx_with(records: list[str], *payloads: bytes) -> str:
    """Build an .ihx containing the given raw payloads as data records."""
    lines = []
    addr = 0x0000
    for payload in payloads:
        lines.append(hex_data(addr, payload))
        addr += len(payload)
    lines.extend(records)
    lines.append(hex_eof())
    return make_hex(lines)


class NoPwmRejectTests(unittest.TestCase):
    """Images containing any PWM enable signature must fail."""

    def run_case(self, name: str, payloads):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.ihx", ihx_with([], *payloads).splitlines())
            proc = run_tool("check-recovery-no-pwm.py", str(path))
            self.assertNotEqual(proc.returncode, 0, name)

    def test_epwm0_enable_rejected(self):
        self.run_case("epwm0", [EPWM0])

    def test_pwm00con_c2_rejected(self):
        self.run_case("0xC2", [PWM00CON_C2])

    def test_pwm00con_ca_rejected(self):
        self.run_case("0xCA", [PWM00CON_CA])

    def test_pattern_above_64k_rejected(self):
        """EPWM0 signature placed above 0x10000 via ELA must be found."""
        with tempfile.TemporaryDirectory() as td:
            records = [hex_ela(0x0001), hex_data(0x0000, EPWM0), hex_eof()]
            path = make_hex_file(Path(td) / "x.ihx", make_hex(records).splitlines())
            proc = run_tool("check-recovery-no-pwm.py", str(path))
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("EPWM0 enable (43 A9 02): 1", proc.stdout)

    def test_malformed_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(
                Path(td) / "x.ihx", [":00FF", hex_eof()]
            )
            proc = run_tool("check-recovery-no-pwm.py", str(path))
            self.assertNotEqual(proc.returncode, 0)


class NoPwmAcceptTests(unittest.TestCase):
    """Images without PWM signatures must pass."""

    def test_clean_image_passes(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(
                Path(td) / "x.ihx",
                ihx_with([], bytes.fromhex("00 01 02 03")).splitlines(),
            )
            proc = run_tool("check-recovery-no-pwm.py", str(path))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("OK", proc.stdout)

    def test_clean_above_64k_passes(self):
        with tempfile.TemporaryDirectory() as td:
            records = [hex_ela(0x0001), hex_data(0x0000, bytes(4)), hex_eof()]
            path = make_hex_file(Path(td) / "x.ihx", make_hex(records).splitlines())
            proc = run_tool("check-recovery-no-pwm.py", str(path))
            self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_real_recovery_image_passes(self):
        skip_if_no_build("recovery build products not present")
        ihx = find_recovery_ihx()
        proc = run_tool("check-recovery-no-pwm.py", str(ihx))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("OK", proc.stdout)

    def test_real_matrix_image_fails(self):
        """The matrix stage DOES enable PWM0 -> checker must fail it."""
        candidates = [
            Path("/tmp/smk/build-f-matrix/royalkludge-rk84_default_smk.ihx"),
            Path("/tmp/smk/build-t-matrix/royalkludge-rk84_default_smk.ihx"),
        ]
        matrix = next((c for c in candidates if c.exists()), None)
        if matrix is None:
            self.skipTest("matrix build not present")
        proc = run_tool("check-recovery-no-pwm.py", str(matrix))
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
