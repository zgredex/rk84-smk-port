"""Compiled-C harness runners for the configurator firmware units
(matrix-layer resolution integration + config protocol).

Each compiles the REAL board .c files against stubs and fails on any
C-level check failure or compile error.
"""
import os
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STUBS = REPO / "tests" / "c_harness" / "stubs"
TMP = Path(os.environ.get("TMPDIR", "/tmp"))


def smk_tree() -> Path:
    t = Path(os.environ.get("RK84_SMK_TREE", ""))
    if not t.exists():
        raise unittest.SkipTest("RK84_SMK_TREE not set / missing")
    return t


def build_and_run(name: str, harness: str, extra_defs: list[str]):
    tree = smk_tree()
    board = tree / "src" / "keyboards" / "royalkludge-rk84"
    includes = [
        STUBS,
        tree / "src" / "smk",
        board,
        tree / "src",
        board / "layouts" / "default",
    ]
    cc = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0"]
    cc += extra_defs
    cc += ["-include", str(STUBS / "harness_stubs.h")]
    for d in includes:
        cc += ["-iquote", str(d)]
    cc += [str(harness), "-o", str(TMP / name)]

    build = subprocess.run(cc, capture_output=True, text=True)
    if build.returncode != 0:
        raise AssertionError(f"{name} build failed:\n{build.stderr}")
    run = subprocess.run([str(TMP / name)], capture_output=True, text=True)
    if run.returncode != 0:
        raise AssertionError(f"{name} failed:\n{run.stdout}\n{run.stderr}")
    if "0 failures" not in run.stdout:
        raise AssertionError(f"{name} reported failures:\n{run.stdout}")


class MatrixResolveHarness(unittest.TestCase):
    def test_matrix_resolution_integration(self):
        # Audit F1: KC_TRANSPARENT fallback must be correct (Fn+A -> KC_A).
        build_and_run(
            "rk84-matrix-resolve",
            REPO / "tests" / "c_harness" / "harness_matrix_resolve.c",
            ["-DSMK_DYNAMIC_KEYMAP=1"],
        )


class ConfigProtocolHarness(unittest.TestCase):
    def test_config_protocol(self):
        # Audit F2/F4/F5/F6: wire statuses, staging, locked-Fn idempotence,
        # exact-request cache.
        build_and_run(
            "rk84-config-protocol",
            REPO / "tests" / "c_harness" / "harness_config_protocol.c",
            ["-DSMK_DYNAMIC_KEYMAP=1", "-DSMK_CONFIG_PROTOCOL=1"],
        )


if __name__ == "__main__":
    unittest.main()
