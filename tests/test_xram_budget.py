"""Unit tests for tools/check-xram-budget.py (.mem parser + gate)."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOL = REPO / "tools" / "check-xram-budget.py"

SAMPLE_MEM = """\
Internal RAM layout:
      0 1 2 3 4 5 6 7 8 9 A B C D E F
0x00:|0|0|0|0|0|0|0|0|a|a|a|a|a|b|b|b|

Stack starts at: 0x41 (sp set to 0x40) with 191 bytes available.

Other memory:
   Name             Start    End      Size     Max
   ---------------- -------- -------- -------- --------
   PAGED EXT. RAM                         0      256
   EXTERNAL RAM     0x0000   0x04b6    1207     4096
   ROM/EPROM/FLASH  0x0000   0x3038   12345    60416
"""


def load_parser():
    spec = importlib.util.spec_from_file_location("check_xram_budget", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class ParseMemTests(unittest.TestCase):
    def test_parses_areas(self):
        mod = load_parser()
        with tempfile.NamedTemporaryFile("w", suffix=".mem", delete=False) as f:
            f.write(SAMPLE_MEM)
            p = Path(f.name)
        try:
            areas = mod.parse_mem(p)
        finally:
            p.unlink()
        self.assertIn("EXTERNAL RAM", areas)
        self.assertEqual(areas["EXTERNAL RAM"]["size"], 1207)
        self.assertEqual(areas["EXTERNAL RAM"]["max"], 4096)
        self.assertIn("ROM/EPROM/FLASH", areas)
        self.assertEqual(areas["ROM/EPROM/FLASH"]["size"], 12345)
        # PAGED EXT. RAM has no Start/End columns (short line); it may
        # be skipped — the gate only needs EXTERNAL RAM.
        self.assertNotIn("PAGED EXT. RAM", areas)


class GateTests(unittest.TestCase):
    def _run(self, limit, mem=SAMPLE_MEM):
        with tempfile.NamedTemporaryFile("w", suffix=".mem", delete=False) as f:
            f.write(mem)
            p = f.name
        try:
            return subprocess.run(
                [sys.executable, str(TOOL), p, "--limit", str(limit)],
                capture_output=True, text=True)
        finally:
            Path(p).unlink()

    def test_within_budget_passes(self):
        r = self._run(3072)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("OK", r.stdout)

    def test_over_budget_fails(self):
        r = self._run(1000)
        self.assertEqual(r.returncode, 1)
        self.assertIn("FAIL", r.stderr)

    def test_boundary_passes(self):
        r = self._run(1207)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
