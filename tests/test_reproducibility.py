"""Reproducibility + manifest tests.

The reproducibility test PERFORMS two complete clean builds in fresh
build directories and asserts byte-identical output. It requires the
patched SMK tree (apply-to-smk.sh must have been run). Fails loudly
when the tree is absent — this is a REQUIRED integration test.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from . import REPO_ROOT

SMK_TREE = Path(os.environ.get("RK84_SMK_TREE", "/tmp/smk-verify"))
STAGE = os.environ.get("RK84_REPRO_STAGE", "recovery")


def build_stage(smk: Path, build_dir: Path) -> None:
    subprocess.run(
        ["meson", "setup", str(build_dir), "--buildtype=release",
         f"-Drk84_stage={STAGE}"],
        cwd=smk, check=True, capture_output=True,
    )
    subprocess.run(
        ["ninja", "-C", str(build_dir), "royalkludge-rk84_default_smk.hex"],
        cwd=smk, check=True, capture_output=True,
    )


class ReproducibilityTests(unittest.TestCase):
    def test_two_clean_builds_byte_identical(self):
        if not SMK_TREE.exists():
            self.fail(
                f"RK84_SMK_TREE={SMK_TREE} absent (required integration "
                "test; run apply-to-smk.sh first)"
            )
        if shutil.which("meson") is None or shutil.which("ninja") is None:
            self.fail("meson/ninja not installed")
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            build_a = td / "build-a"
            build_b = td / "build-b"
            build_stage(SMK_TREE, build_a)
            build_stage(SMK_TREE, build_b)
            hex_a = build_a / "royalkludge-rk84_default_smk.hex"
            hex_b = build_b / "royalkludge-rk84_default_smk.hex"
            ihx_a = build_a / "royalkludge-rk84_default_smk.ihx"
            ihx_b = build_b / "royalkludge-rk84_default_smk.ihx"
            for f in (hex_a, hex_b, ihx_a, ihx_b):
                self.assertTrue(f.exists(), f)
            self.assertEqual(
                hashlib.sha256(hex_a.read_bytes()).hexdigest(),
                hashlib.sha256(hex_b.read_bytes()).hexdigest(),
                "recovery HEX must be byte-identical",
            )
            self.assertEqual(
                hashlib.sha256(ihx_a.read_bytes()).hexdigest(),
                hashlib.sha256(ihx_b.read_bytes()).hexdigest(),
                "recovery IHX must be byte-identical",
            )

    def test_manifest_consistent(self):
        if not SMK_TREE.exists():
            self.fail("RK84_SMK_TREE absent (required integration test)")
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            build_dir = td / "build"
            build_stage(SMK_TREE, build_dir)
            hex_f = build_dir / "royalkludge-rk84_default_smk.hex"
            ihx_f = build_dir / "royalkludge-rk84_default_smk.ihx"
            proc = subprocess.run(
                [
                    "python3",
                    str(REPO_ROOT / "tools" / "make-manifest.py"),
                    str(hex_f), str(ihx_f),
                    "--build-dir", str(build_dir),
                ],
                capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            out = proc.stdout
            self.assertIn(f"stage:             {STAGE}", out)
            self.assertIn("hex_sha256:", out)
            self.assertIn("usb_vid_pid:       258A:0059", out)
            if STAGE == "recovery":
                self.assertIn("pwm_epwm0_count:  0", out)
                self.assertIn("pwm_00c2_count:   0", out)
                self.assertIn("pwm_00ca_count:   0", out)
            import re
            m = re.search(r"highest_written_address:\s+0x([0-9A-Fa-f]+)", out)
            self.assertIsNotNone(m)
            self.assertLess(int(m.group(1), 16), 0xBC00)


if __name__ == "__main__":
    unittest.main()
