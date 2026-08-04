"""Negative/positive tests for tools/check-hex-bounds.py."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from . import (
    at_boundary,
    bad_checksum,
    bad_data_after_eof,
    bad_malformed_eof,
    bad_missing_eof,
    bad_truncated,
    bad_unsupported_type,
    bad_zero_len,
    good_hex,
    good_hex_above_64k,
    hex_data,
    hex_eof,
    hex_esa,
    make_hex,
    make_hex_file,
    over_limit,
    run_tool,
)


class HexBoundsRejectTests(unittest.TestCase):
    """Malformed or out-of-bounds images must be rejected (exit != 0)."""

    CASES = [
        ("truncated record", bad_truncated, "truncated"),
        ("wrong checksum", bad_checksum, "checksum"),
        ("zero-length data record", bad_zero_len, "zero-length"),
        ("missing EOF", bad_missing_eof, "missing EOF"),
        ("malformed EOF", bad_malformed_eof, "malformed EOF"),
        ("data after EOF", bad_data_after_eof, "after EOF"),
        ("unsupported record type", bad_unsupported_type, "unsupported record type"),
    ]

    def run_case(self, name: str, builder, needle: str):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", builder().splitlines())
            proc = run_tool("check-hex-bounds.py", str(path))
            self.assertNotEqual(
                proc.returncode, 0, f"{name}: tool must reject"
            )
            self.assertIn(needle, proc.stderr + proc.stdout, name)

    def test_rejects_all_malformed(self):
        for name, builder, needle in self.CASES:
            with self.subTest(name):
                self.run_case(name, builder, needle)

    def test_rejects_over_bc00(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", over_limit(0xBC00).splitlines())
            proc = run_tool("check-hex-bounds.py", str(path))
            self.assertNotEqual(proc.returncode, 0)

    def test_rejects_over_ec00(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", over_limit(0xEC00).splitlines())
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0xEC00"
            )
            self.assertNotEqual(proc.returncode, 0)

    def test_rejects_over_effc(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", over_limit(0xEFFC).splitlines())
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0xEFFC"
            )
            self.assertNotEqual(proc.returncode, 0)

    def test_rejects_over_f000(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", over_limit(0xF000).splitlines())
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0xF000"
            )
            self.assertNotEqual(proc.returncode, 0)

    def test_rejects_data_at_effc(self):
        """Data exactly AT the 0xEFFC redirect slot must fail."""
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(
                Path(td) / "x.hex",
                make_hex([hex_data(0xEFFC, bytes.fromhex("02")), hex_eof()]).splitlines(),
            )
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0xEFFC"
            )
            self.assertNotEqual(proc.returncode, 0)


class HexBoundsAcceptTests(unittest.TestCase):
    """Valid images must pass."""

    def test_accepts_good(self):
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(Path(td) / "x.hex", good_hex().splitlines())
            proc = run_tool("check-hex-bounds.py", str(path))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("OK", proc.stdout)

    def test_accepts_ela_above_64k(self):
        """ELA-addressed data is understood; highest address > 64k counts."""
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(
                Path(td) / "x.hex", good_hex_above_64k().splitlines()
            )
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0x20000"
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("0x10003", proc.stdout)

    def test_accepts_esa(self):
        """Type 0x02 extended segment records are parsed."""
        with tempfile.TemporaryDirectory() as td:
            records = [
                hex_esa(0x3000),
                hex_data(0x0000, bytes.fromhex("01")),
                hex_eof(),
            ]
            path = make_hex_file(Path(td) / "x.hex", make_hex(records).splitlines())
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0x30000"
            )
            # data at 0x30000 == limit -> must FAIL
            self.assertNotEqual(proc.returncode, 0)
            proc = run_tool(
                "check-hex-bounds.py", str(path), "--limit", "0x40000"
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_accepts_at_boundary(self):
        """Highest byte exactly at limit-1 passes."""
        with tempfile.TemporaryDirectory() as td:
            path = make_hex_file(
                Path(td) / "x.hex", at_boundary(0xBC00).splitlines()
            )
            proc = run_tool("check-hex-bounds.py", str(path))
            self.assertEqual(proc.returncode, 0, proc.stderr)


if __name__ == "__main__":
    unittest.main()
