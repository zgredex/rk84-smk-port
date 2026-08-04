"""Compiled-C harness runner: builds and runs tests/c_harness/harness_report.c
against the REAL smk/report.c (RK84_STOCK_REPORTS + NKRO_ENABLE) with stub
headers, and fails if any C-level check fails.

This is the final offline gate the USB-stage audit demanded: the Python
models mirror the C semantics, but only the harness exercises the actual
compiled code (it would have caught the NKRO-bitmap self-clear immediately).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HARNESS = REPO / "tests" / "c_harness" / "harness_report.c"
STUBS = REPO / "tests" / "c_harness" / "stubs"


def _smk_tree() -> Path:
    tree = os.environ.get("RK84_SMK_TREE")
    if not tree:
        raise unittest.SkipTest("RK84_SMK_TREE not set (needs the SMK checkout)")
    p = Path(tree)
    if not (p / "src" / "smk" / "report.c").exists():
        raise unittest.SkipTest(f"RK84_SMK_TREE={p} has no src/smk/report.c")
    return p


class CHarnessTests(unittest.TestCase):
    def test_harness_report_compiles_and_passes(self):
        smk = _smk_tree()
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("no host C compiler")
        out = tempfile.mktemp(suffix=".bin")
        try:
            proc = subprocess.run(
                [
                    cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
                    "-DRK84_STOCK_REPORTS=1", "-DNKRO_ENABLE=1", "-DDEBUG=0",
                    f"-include{STUBS / 'harness_stubs.h'}",
                    f"-I{STUBS}",
                    f"-I{smk / 'src' / 'smk'}",
                    f"-I{smk / 'src' / 'kb'}",
                    f"-I{smk / 'src'}",
                    f"-I{smk / 'src' / 'platform' / 'sh68f90a'}",
                    f"-I{smk / 'src' / 'keyboards' / 'royalkludge-rk84'}",
                    str(HARNESS),
                    str(smk / "src" / "smk" / "report.c"),
                    "-o", out,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0,
                             f"harness build failed:\n{proc.stderr}")
            run = subprocess.run([out], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0,
                             f"harness run failed:\n{run.stdout}\n{run.stderr}")
            self.assertIn("ALL C HARNESS TESTS PASS", run.stdout)
        finally:
            if os.path.exists(out):
                os.unlink(out)


if __name__ == "__main__":
    unittest.main()
