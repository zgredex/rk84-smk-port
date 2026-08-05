"""Tests for tools/generate-configurator-keymap.py (Q1 audit): the
generated TypeScript default keymap must match the firmware layout
source, and --check must fail when the file is stale.
"""
from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GEN = REPO / "tools" / "generate-configurator-keymap.py"
OUT = REPO / "configurator" / "src" / "generated" / "rk84-default-keymap.ts"


def smk_tree() -> Path:
    """S1 (audit): an UNSET RK84_SMK_TREE must skip, never resolve to
    '.' (Path('') == '.' would defeat the check)."""
    raw = os.environ.get("RK84_SMK_TREE")
    if not raw:
        raise unittest.SkipTest("RK84_SMK_TREE not set")
    tree = Path(raw)
    if not tree.is_dir():
        raise unittest.SkipTest(f"RK84_SMK_TREE missing: {tree}")
    return tree


class GenerateConfiguratorKeymapTests(unittest.TestCase):
    def test_check_passes_on_current_output(self):
        """The committed generated file matches the firmware layout."""
        smk_tree()
        proc = subprocess.run(
            ["python3", str(GEN), "--check"],
            capture_output=True, text=True,
        )
        self.assertEqual(
            proc.returncode, 0,
            f"generated keymap stale:\n{proc.stdout}\n{proc.stderr}",
        )

    def test_check_fails_on_stale_output(self):
        """A tampered generated file is detected by --check."""
        smk_tree()
        original = OUT.read_text()
        try:
            OUT.write_text(original.replace("0x0029", "0x00ff", 1))
            proc = subprocess.run(
                ["python3", str(GEN), "--check"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(proc.returncode, 0,
                                "--check must fail on stale output")
        finally:
            OUT.write_text(original)

    def test_generated_cells_count(self):
        """The generated file holds exactly 192 cells (2 layers x 6 x 16)."""
        self.assertTrue(OUT.exists(), "generated keymap missing")
        text = OUT.read_text()
        self.assertEqual(text.count("0x"), 192,
                         "generated keymap must have exactly 192 cells")


if __name__ == "__main__":
    unittest.main()
