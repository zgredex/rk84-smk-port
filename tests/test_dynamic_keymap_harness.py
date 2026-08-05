"""Compiled-C harness runner for the dynamic keymap (configurator M2).

Builds and runs tests/c_harness/harness_dynamic_keymap.c against the
REAL board dynamic_keymap.c with stub headers. Fails if any C check
fails or if the harness cannot compile.
"""
import os
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HARNESS = REPO / "tests" / "c_harness" / "harness_dynamic_keymap.c"
STUBS = REPO / "tests" / "c_harness" / "stubs"
OUT = Path(os.environ.get("TMPDIR", "/tmp")) / "rk84-dk-harness"


def smk_tree() -> Path:
    t = Path(os.environ.get("RK84_SMK_TREE", ""))
    if not t.exists():
        raise unittest.SkipTest("RK84_SMK_TREE not set / missing")
    return t


class DynamicKeymapHarnessTests(unittest.TestCase):

    def test_harness_compiles_and_passes(self):
        tree = smk_tree()
        include_dirs = [
            STUBS,
            tree / "src" / "smk",
            tree / "src" / "keyboards" / "royalkludge-rk84",
            tree / "src",
            tree / "src" / "keyboards" / "royalkludge-rk84" / "layouts" / "default",
        ]
        cc = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
              "-DSMK_DYNAMIC_KEYMAP=1",
              "-include", str(STUBS / "harness_stubs.h")]
        for d in include_dirs:
            cc += ["-iquote", str(d)]
        cc += [str(HARNESS), "-o", str(OUT)]

        build = subprocess.run(cc, capture_output=True, text=True)
        self.assertEqual(
            build.returncode, 0,
            f"harness build failed:\n{build.stderr}")

        run = subprocess.run([str(OUT)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0,
                         f"harness failed:\n{run.stdout}\n{run.stderr}")
        self.assertIn("0 failures", run.stdout,
                      f"harness reported failures:\n{run.stdout}")


if __name__ == "__main__":
    unittest.main()
