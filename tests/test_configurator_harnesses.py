"""Compiled-C harness runners for the configurator firmware units
(matrix-layer resolution integration + config protocol).

Each compiles the REAL board .c files against stubs and fails on any
C-level check failure or compile error.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STUBS = REPO / "tests" / "c_harness" / "stubs"
HARNESS_DIR = REPO / "tests" / "c_harness"
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


def extract_c_function(source: str, signature: str) -> str:
    """Extract a complete C function body (balanced braces) from source,
    starting at `signature` (Q2: the production usb.c wrapper).
    Skips forward declarations (signature NOT followed by '{')."""
    idx = 0
    while True:
        start = source.index(signature, idx)
        idx = start + len(signature)
        # skip the signature itself, find the next '{'
        rest = source[idx:]
        # a forward declaration ends with ';' before any '{'
        semi = rest.find(";")
        brace = rest.find("{")
        if semi != -1 and (brace == -1 or semi < brace):
            continue  # declaration, not definition
        brace = source.index("{", start)
        depth = 0
        for i in range(brace, len(source)):
            if source[i] == "{":
                depth += 1
            elif source[i] == "}":
                depth -= 1
                if depth == 0:
                    return source[start:i + 1]
        raise ValueError(f"unbalanced function: {signature}")
    raise ValueError(f"function not found: {signature}")


def build_usb_ep0(tc: unittest.TestCase):
    """Q2 (audit): compile the REAL step_ep0_in_xfer() extracted from
    the actual patched usb.c, injected into the harness template — the
    wrapper can never drift from production."""
    tree = smk_tree()
    usb_source = (tree / "src" / "platform" / "sh68f90a" / "usb.c").read_text()
    wrapper = extract_c_function(usb_source, "static void step_ep0_in_xfer()")
    template = (HARNESS_DIR / "usb_ep0_template.c").read_text()
    if "/* INSERT_PRODUCTION_WRAPPER */" not in template:
        raise AssertionError("usb_ep0_template.c missing INSERT_PRODUCTION_WRAPPER")
    generated = template.replace("/* INSERT_PRODUCTION_WRAPPER */", wrapper)

    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "usb_ep0_generated.c"
        src.write_text(generated)
        out = Path(td) / "rk84-usb-ep0"
        includes = [STUBS, tree / "src" / "smk",
                    tree / "src" / "keyboards" / "royalkludge-rk84",
                    tree / "src",
                    tree / "src" / "keyboards" / "royalkludge-rk84" / "layouts" / "default"]
        cc = ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O0",
              "-DSMK_DYNAMIC_KEYMAP=1", "-DSMK_CONFIG_PROTOCOL=1",
              "-include", str(STUBS / "harness_stubs.h")]
        for d in includes:
            cc += ["-iquote", str(d)]
        cc += [str(src), "-o", str(out)]
        build = subprocess.run(cc, capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(f"usb_ep0 build failed:\n{build.stderr}")
        run = subprocess.run([str(out)], capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(f"usb_ep0 failed:\n{run.stdout}\n{run.stderr}")
        if "0 failures" not in run.stdout:
            raise AssertionError(f"usb_ep0 reported failures:\n{run.stdout}")


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


class UsbEp0WrapperHarness(unittest.TestCase):
    def test_real_usb_wrapper(self):
        # Audit P5/N4/Q2: the REAL usb.c step_ep0_in_xfer() — SET_EP0_CNT
        # arming, EP0_IN_BUF copy, IN_DATA/RECV_STATUS state mapping,
        # source/remaining accounting, zero-length + short reads.
        # Q2: the wrapper is EXTRACTED from the actual patched usb.c at
        # build time and compiled — never a hand copy.
        build_usb_ep0(self)


if __name__ == "__main__":
    unittest.main()
