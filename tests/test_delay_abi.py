"""Delay ABI regression test (RK84-SMK-WORKING-IMPLEMENTATION §3.4).

The naked delay_us() clobbered caller R6/R7 and returned them zeroed,
so delay_ms(N) executed exactly one delay_us(1000) (~1 ms) and exited
for ANY N — the 1.2s and 4s RGB probes appeared identical.

This test asserts on the COMPILED ASSEMBLY of the actual build that:
  - delay_us saves caller R6/R7 (via ACC) before loading DPL/DPH;
  - delay_us restores R7/R6 before RET;
  - delay_ms still contains a loop around LCALL delay_us;
  - delay_ms keeps its counter in R6:R7 across the call (the reload
    pattern `mov ar4,r6 / mov ar5,r7 / dec r6` before the LCALL).

Env: RK84_SMK_TREE points at a PATCHED (not pristine) SMK tree with a
completed build. SKIP if the tree has no build artifacts.
"""
import os
import unittest
from pathlib import Path


def find_delay_asm(tree: Path) -> Path:
    """Locate the NEWEST compiled delay.asm under the tree's build dirs.

    rglob order is filesystem-dependent; multiple build dirs may exist
    (stale pre-fix builds), so pick by mtime — the freshest build is
    the one the patch set actually produced.
    """
    candidates = [c for c in tree.rglob("delay.asm") if "build" in str(c)]
    if not candidates:
        raise FileNotFoundError("no compiled delay.asm found under RK84_SMK_TREE")
    return max(candidates, key=lambda p: p.stat().st_mtime)


class DelayAbiTests(unittest.TestCase):

    asm_text = None

    @classmethod
    def setUpClass(cls):
        tree = Path(os.environ.get("RK84_SMK_TREE", ""))
        if not tree.exists():
            raise unittest.SkipTest("RK84_SMK_TREE not set / missing")
        try:
            cls.asm_text = find_delay_asm(tree).read_text(errors="replace")
        except FileNotFoundError as exc:
            raise unittest.SkipTest(str(exc))

    def test_delay_us_saves_r6_r7_before_loading_counter(self):
        """Save sequence must precede the DPL/DPH load."""
        us = self.asm_text.index("_delay_us:")
        save = self.asm_text.find("push acc", us)
        dpl = self.asm_text.find("mov r6, dpl", us)
        r6_save = self.asm_text.find("mov a, r6", us)
        r7_save = self.asm_text.find("mov a, r7", us)
        self.assertGreaterEqual(save, 0, "delay_us: missing 'push acc'")
        self.assertGreaterEqual(dpl, 0, "delay_us: missing 'mov r6, dpl'")
        self.assertGreaterEqual(r6_save, 0, "delay_us: missing 'mov a, r6'")
        self.assertGreaterEqual(r7_save, 0, "delay_us: missing 'mov a, r7'")
        self.assertLess(save, r6_save)
        self.assertLess(r6_save, r7_save)
        self.assertLess(r7_save, dpl,
                        "delay_us must save R6/R7 BEFORE loading the counter")

    def test_delay_us_restores_r7_r6_before_ret(self):
        """Restore sequence must precede RET, in reverse order."""
        us = self.asm_text.index("_delay_us:")
        ms = self.asm_text.index("_delay_ms:")
        body = self.asm_text[us:ms]
        r7_restore = body.find("mov r7, a")
        r6_restore = body.find("mov r6, a")
        ret = body.rfind("ret")
        self.assertGreaterEqual(r7_restore, 0, "missing 'mov r7, a' restore")
        self.assertGreaterEqual(r6_restore, 0, "missing 'mov r6, a' restore")
        self.assertGreaterEqual(ret, 0, "missing RET in delay_us")
        self.assertLess(r7_restore, r6_restore)
        self.assertLess(r6_restore, ret,
                        "delay_us must restore R6/R7 before RET")

    def test_delay_ms_loops_around_delay_us(self):
        """delay_ms must contain a loop body that calls delay_us."""
        ms = self.asm_text.index("_delay_ms:")
        body = self.asm_text[ms:]
        self.assertIn("lcall\t_delay_us", body)
        self.assertIn("00101$:", body)
        self.assertIn("sjmp\t00101$", body)

    def test_delay_ms_counter_lives_in_r6_r7(self):
        """The counter must be kept in R6/R7 (the registers delay_us
        previously clobbered) — proving the ABI contract matters."""
        ms = self.asm_text.index("_delay_ms:")
        body = self.asm_text[ms:]
        self.assertIn("mov\tr6, dpl", body)
        self.assertIn("mov\tr7, dph", body)
        # reload copy before decrement, then the call
        self.assertIn("mov\tar4,r6", body)
        self.assertIn("mov\tar5,r7", body)


if __name__ == "__main__":
    unittest.main()
