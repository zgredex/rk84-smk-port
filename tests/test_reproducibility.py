"""Reproducibility + manifest tests.

The recovery build must be byte-identical across two clean build
directories, and the manifest generator must report coherent values.
These tests need a local SMK working tree; they skip when absent.
"""
from __future__ import annotations

import hashlib
import subprocess
import unittest
from pathlib import Path

from . import REPO_ROOT, run_tool, skip_if_no_build

SMK_TREE = Path("/tmp/smk-verify")


def find_two_builds() -> list[Path] | None:
    """Two independently built recovery hexes from clean dirs."""
    hexes = sorted(SMK_TREE.glob("build-repro-*/royalkludge-rk84_default_smk.hex"))
    return hexes if len(hexes) >= 2 else None


@unittest.skipUnless(SMK_TREE.exists(), "SMK tree not present")
class ReproducibilityTests(unittest.TestCase):
    def test_two_clean_builds_identical(self):
        hexes = find_two_builds()
        if hexes is None:
            self.skipTest("repro builds not present")
        a = hexes[0].read_bytes()
        b = hexes[1].read_bytes()
        self.assertEqual(
            hashlib.sha256(a).hexdigest(),
            hashlib.sha256(b).hexdigest(),
            "recovery builds must be byte-identical",
        )

    def test_recovery_manifest_consistent(self):
        hexes = find_two_builds()
        if hexes is None:
            self.skipTest("repro builds not present")
        h = hexes[0]
        ihx = h.with_suffix(".ihx")
        proc = subprocess.run(
            [
                "python3",
                str(REPO_ROOT / "tools" / "make-manifest.py"),
                str(h),
                str(ihx),
                "--build-dir",
                str(h.parent),
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        out = proc.stdout
        self.assertIn("stage:             recovery", out)
        self.assertIn("hex_sha256:", out)
        self.assertIn("usb_vid_pid:       258A:0059", out)
        self.assertIn("pwm_epwm0_count:  0", out)
        self.assertIn("pwm_00c2_count:   0", out)
        self.assertIn("pwm_00ca_count:   0", out)
        # highest address must be below the first-stage cap
        import re
        m = re.search(r"highest_address:\s+0x([0-9A-Fa-f]+)", out)
        self.assertIsNotNone(m)
        self.assertLess(int(m.group(1), 16), 0xBC00)

    def test_matrix_manifest_differs(self):
        """Matrix build must not claim recovery invariants."""
        matrix = SMK_TREE / "build-matrix" / "royalkludge-rk84_default_smk.hex"
        if not matrix.exists():
            self.skipTest("matrix build not present")
        ihx = matrix.with_suffix(".ihx")
        proc = subprocess.run(
            [
                "python3",
                str(REPO_ROOT / "tools" / "make-manifest.py"),
                str(matrix),
                str(ihx),
                "--build-dir",
                str(matrix.parent),
            ],
            capture_output=True,
            text=True,
        )
        out = proc.stdout
        self.assertIn("stage:             matrix", out)
        self.assertIn("pwm_epwm0_count:  1", out)


if __name__ == "__main__":
    unittest.main()
