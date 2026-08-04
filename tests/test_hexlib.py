"""Tests for the shared Intel HEX parser (tools/hexlib.py)."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from hexlib import hex_extent, iter_records, parse_hex  # noqa: E402


def rec(typ: int, addr: int, payload: bytes) -> str:
    r = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, typ]) + payload
    return ":" + (r + bytes([(-sum(r)) & 0xFF])).hex().upper()


def make(records: list[str]) -> str:
    return "\n".join(records) + "\n"


class HexLibValidTests(unittest.TestCase):
    def test_parse_dense(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([rec(0, 0x0000, b"\x01\x02"), rec(0, 0x0002, b"\x03"), rec(1, 0, b"")]))
            blob = parse_hex(p)
            self.assertEqual(blob, b"\x01\x02\x03")

    def test_parse_with_hole(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([rec(0, 0x0000, b"\x01"), rec(0, 0x0010, b"\xFF"), rec(1, 0, b"")]))
            blob = parse_hex(p)
            self.assertEqual(blob[0], 0x01)
            self.assertEqual(blob[0x10], 0xFF)
            self.assertEqual(len(blob), 0x11)

    def test_ela_addressing(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(4, 0, b"\x00\x01"),       # ELA base 0x10000
                rec(0, 0x0000, b"\xAA"),      # data at 0x10000
                rec(1, 0, b""),
            ]))
            blob = parse_hex(p)
            self.assertEqual(blob[0x10000], 0xAA)

    def test_esa_addressing(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(2, 0, b"\x10\x00"),       # ESA 0x10000
                rec(0, 0x0000, b"\xBB"),
                rec(1, 0, b""),
            ]))
            blob = parse_hex(p)
            self.assertEqual(blob[0x10000], 0xBB)

    def test_extent(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([rec(0, 0x0008, b"\x01\x02\x03"), rec(1, 0, b"")]))
            written, lowest, highest = hex_extent(p)
            self.assertEqual((written, lowest, highest), (3, 0x0008, 0x000A))

    def test_eof_required(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([rec(0, 0x0000, b"\x01")]))
            with self.assertRaises(ValueError):
                parse_hex(p)


class HexLibRejectTests(unittest.TestCase):
    CASES = [
        ("truncated", ":00FF"),
        ("zero-len data", ":0000000000"),
        ("bad checksum", ":02000000AABB00"),
        ("data after eof", rec(0, 0, b"\x01") + "\n" + rec(1, 0, b"") + "\n" + rec(0, 0, b"\x02")),
        ("unsupported type", rec(0x05, 0, b"\x00\x00\x00\x00") + "\n" + rec(1, 0, b"")),
    ]

    def test_rejections(self):
        for name, content in self.CASES:
            with self.subTest(name):
                with tempfile.TemporaryDirectory() as td:
                    p = Path(td) / "x.hex"
                    p.write_text(make(content.splitlines()) if "\n" not in content else content)
                    with self.assertRaises(ValueError, msg=name):
                        parse_hex(p)

    def test_malformed_eof(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([rec(0, 0, b"\x01"), rec(1, 1, b"")]))
            with self.assertRaises(ValueError):
                parse_hex(p)


class HexLibEofStrictnessTests(unittest.TestCase):
    def test_records_after_eof_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(rec(1, 0, b"") + "\n" + rec(0, 0, b"\x01") + "\n")
            with self.assertRaises(ValueError):
                list(iter_records(p))


class HexLibOverlapTests(unittest.TestCase):
    def test_overlapping_records_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(0, 0x0000, b"\x01\x02"),
                rec(0, 0x0001, b"\xFF"),  # overlaps 0x0001
                rec(1, 0, b""),
            ]))
            with self.assertRaises(ValueError):
                hex_extent(p)

    def test_parse_hex_rejects_overlap_too(self):
        """parse_hex must share the same overlap strictness."""
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(0, 0x0000, b"\x01"),
                rec(0, 0x0000, b"\x02"),  # duplicate address
                rec(1, 0, b""),
            ]))
            with self.assertRaises(ValueError):
                parse_hex(p)

    def test_duplicate_address_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(0, 0x0000, b"\x01"),
                rec(0, 0x0000, b"\x02"),  # same address twice
                rec(1, 0, b""),
            ]))
            with self.assertRaises(ValueError):
                hex_extent(p)

    def test_nonoverlapping_counted_once(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "x.hex"
            p.write_text(make([
                rec(0, 0x0000, b"\x01\x02"),
                rec(0, 0x0002, b"\x03"),
                rec(1, 0, b""),
            ]))
            written, lowest, highest = hex_extent(p)
            self.assertEqual((written, lowest, highest), (3, 0, 2))


if __name__ == "__main__":
    unittest.main()
