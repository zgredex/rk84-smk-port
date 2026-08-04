"""Shared fixtures for the RK84 test suite.

Fixtures are generated programmatically (no binary blobs in the repo):
- synthetic Intel HEX images for parser negative tests
- the real recovery build products when available (tests skip otherwise)
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"

# Artifact paths come from environment variables (set by CI integration
# jobs or locally); fall back to common local build trees.
_RECOVERY_HEX = Path(os.environ.get(
    "RK84_RECOVERY_HEX",
    "/tmp/smk/build-f-recovery/royalkludge-rk84_default_smk.hex",
))
_RECOVERY_IHX = Path(os.environ.get(
    "RK84_RECOVERY_IHX",
    "/tmp/smk/build-f-recovery/royalkludge-rk84_default_smk.ihx",
))
_MATRIX_HEX = Path(os.environ.get(
    "RK84_MATRIX_HEX",
    "/tmp/smk/build-f-matrix/royalkludge-rk84_default_smk.hex",
))
_MATRIX_IHX = Path(os.environ.get(
    "RK84_MATRIX_IHX",
    "/tmp/smk/build-f-matrix/royalkludge-rk84_default_smk.ihx",
))


# ---------------------------------------------------------------------
# Intel HEX construction helpers
# ---------------------------------------------------------------------

def hex_record(rec_type: int, addr: int, payload: bytes) -> str:
    rec = bytes([len(payload), (addr >> 8) & 0xFF, addr & 0xFF, rec_type]) + payload
    cs = (-sum(rec)) & 0xFF
    return ":" + (rec + bytes([cs])).hex().upper()


def hex_data(addr: int, payload: bytes) -> str:
    return hex_record(0x00, addr, payload)


def hex_ela(base16: int) -> str:
    """Extended linear address record (type 0x04)."""
    return hex_record(0x04, 0, base16.to_bytes(2, "big"))


def hex_esa(segment: int) -> str:
    """Extended segment address record (type 0x02)."""
    return hex_record(0x02, 0, segment.to_bytes(2, "big"))


def hex_eof() -> str:
    return hex_record(0x01, 0, b"")


def make_hex(records: list[str]) -> str:
    return "\n".join(records) + "\n"


def make_hex_file(path: Path, records: list[str]) -> Path:
    path.write_text(make_hex(records))
    return path


# ---------------------------------------------------------------------
# Known-good / known-bad HEX fixtures
# ---------------------------------------------------------------------

def good_hex() -> str:
    """Minimal valid image: 4 data bytes at 0, 2 at 0x20, EOF."""
    return make_hex([
        hex_data(0x0000, bytes.fromhex("01020304")),
        hex_data(0x0020, bytes.fromhex("AABB")),
        hex_eof(),
    ])


def good_hex_above_64k() -> str:
    """Data below AND above 64 KiB via an ELA record."""
    return make_hex([
        hex_data(0x0000, bytes.fromhex("01020304")),
        hex_ela(0x0001),
        hex_data(0x0000, bytes.fromhex("DEADBEEF")),
        hex_eof(),
    ])


def bad_truncated() -> str:
    """Record shorter than 5 bytes (cannot even unpack fields)."""
    return make_hex([":00FF"])


def bad_checksum() -> str:
    """Data record with a deliberately wrong checksum byte."""
    # 02 00 00 00 A B 00 -> sum != 0
    return make_hex([":02000000AABB00", hex_eof()])


def bad_zero_len() -> str:
    """Zero-length data record (strict mode rejects)."""
    return make_hex([":0000000000", hex_eof()])


def bad_missing_eof() -> str:
    return make_hex([hex_data(0x0000, bytes.fromhex("01020304"))])


def bad_malformed_eof() -> str:
    """EOF record with nonzero addr (malformed)."""
    return make_hex([
        hex_data(0x0000, bytes.fromhex("01020304")),
        hex_record(0x01, 0x0001, b""),
    ])


def bad_data_after_eof() -> str:
    return make_hex([
        hex_data(0x0000, bytes.fromhex("01020304")),
        hex_eof(),
        hex_data(0x0010, bytes.fromhex("FF")),
    ])


def bad_unsupported_type() -> str:
    return make_hex([
        hex_data(0x0000, bytes.fromhex("01020304")),
        hex_record(0x05, 0, b"\x00\x00\x00\x00"),
        hex_eof(),
    ])


def over_limit(limit: int) -> str:
    """Image with a data byte AT the limit address (must fail)."""
    return make_hex([
        hex_data(limit & 0xFFFF, bytes.fromhex("01")),
        hex_eof(),
    ])


def at_boundary(limit: int) -> str:
    """Image whose highest byte is limit-1 (must pass)."""
    addr = limit - 1
    return make_hex([
        hex_data(addr & 0xFFFF, bytes.fromhex("01")),
        hex_eof(),
    ])


# ---------------------------------------------------------------------
# Real build products (env-var driven; integration tests FAIL when
# required artifacts are absent, synthetic tests never need them)
# ---------------------------------------------------------------------

def find_recovery_hex() -> Path | None:
    if _RECOVERY_HEX.exists():
        return _RECOVERY_HEX
    return None


def find_recovery_ihx() -> Path | None:
    if _RECOVERY_IHX.exists():
        return _RECOVERY_IHX
    return None


def find_matrix_ihx() -> Path | None:
    if _MATRIX_IHX.exists():
        return _MATRIX_IHX
    return None


def require_recovery_artifacts():
    """Integration helper: raise (fail the test) if the recovery
    artifacts are missing — required integration tests must not skip."""
    missing = []
    if not _RECOVERY_HEX.exists():
        missing.append(f"RK84_RECOVERY_HEX={_RECOVERY_HEX}")
    if not _RECOVERY_IHX.exists():
        missing.append(f"RK84_RECOVERY_IHX={_RECOVERY_IHX}")
    if missing:
        raise AssertionError(
            "recovery artifacts required but missing: "
            + "; ".join(missing)
        )


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def skip_if_no_build(reason: str = "recovery build products not present"):
    if find_recovery_hex() is None or find_recovery_ihx() is None:
        import unittest
        raise unittest.SkipTest(reason)


def run_tool(name: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(TOOLS / name), *args],
        capture_output=True,
        text=True,
    )
