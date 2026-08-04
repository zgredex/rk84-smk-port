"""Shared, strictly-validated Intel HEX parser.

All RK84 verification tools use this single implementation so parser
behaviour (length/checksum/EOF/type handling) cannot drift between
them. Strictness rules:

  - every record must be exactly count + 5 bytes (count, addr[2],
    type, checksum)
  - every record checksum must sum to zero
  - EOF must be a clean type-01 record with count=0 and addr=0
  - data after EOF is rejected
  - unsupported record types are rejected
  - type 0x02 (ESA) and 0x04 (ELA) update the segment base

API:
    parse_hex(path) -> bytes            # image bytes (sparse->dense)
    hex_extent(path) -> (written, lowest, highest)
    iter_records(path) -> [Record]
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass
class Record:
    line_no: int
    type: int
    addr: int
    payload: bytes


def _parse_record(line_no: int, raw: str) -> Record:
    line = raw.strip()
    if not line.startswith(":"):
        raise ValueError(f"line {line_no}: invalid Intel HEX line")
    data = bytes.fromhex(line[1:])
    if len(data) < 5:
        raise ValueError(f"line {line_no}: truncated Intel HEX record")
    count = data[0]
    addr = (data[1] << 8) | data[2]
    typ = data[3]
    if len(data) != count + 5:
        raise ValueError(
            f"line {line_no}: record length mismatch "
            f"(expected {count + 5} bytes, got {len(data)})"
        )
    if sum(data) & 0xFF:
        raise ValueError(f"line {line_no}: checksum mismatch")
    return Record(line_no, typ, addr, data[4:4 + count])


def iter_records(path: Path) -> list[Record]:
    records: list[Record] = []
    eof_seen = False
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        rec = _parse_record(line_no, raw)
        if eof_seen:
            raise ValueError(f"line {line_no}: data after EOF record")
        if rec.type == 0x01:
            if rec.payload or rec.addr != 0:
                raise ValueError(f"line {line_no}: malformed EOF record")
            eof_seen = True
            continue
        if rec.type not in (0x00, 0x02, 0x04):
            raise ValueError(
                f"line {line_no}: unsupported record type {rec.type:#04x}"
            )
        if rec.type == 0x00 and not rec.payload:
            raise ValueError(f"line {line_no}: zero-length data record")
        records.append(rec)
    if not eof_seen:
        raise ValueError("missing EOF record (type 01)")
    return records


def parse_hex(path: Path) -> bytes:
    """Dense image bytes; data written at absolute addresses."""
    data = bytearray()
    upper = 0
    for rec in iter_records(path):
        if rec.type == 0x00:
            absolute = upper + rec.addr
            if absolute + len(rec.payload) > len(data):
                data.extend(b"\x00" * (absolute + len(rec.payload) - len(data)))
            data[absolute:absolute + len(rec.payload)] = rec.payload
        elif rec.type == 0x04:
            if len(rec.payload) != 2:
                raise ValueError(f"line {rec.line_no}: bad ELA record")
            upper = ((rec.payload[0] << 8) | rec.payload[1]) << 16
        elif rec.type == 0x02:
            if len(rec.payload) != 2:
                raise ValueError(f"line {rec.line_no}: bad ESA record")
            upper = ((rec.payload[0] << 8) | rec.payload[1]) << 4
    return bytes(data)


def hex_extent(path: Path) -> tuple[int, int, int]:
    """(written_byte_count, lowest_written_address, highest_written_address)."""
    upper = 0
    lowest = None
    highest = -1
    written = 0
    for rec in iter_records(path):
        if rec.type == 0x00:
            absolute = upper + rec.addr
            if lowest is None or absolute < lowest:
                lowest = absolute
            highest = max(highest, absolute + len(rec.payload) - 1)
            written += len(rec.payload)
        elif rec.type == 0x04:
            if len(rec.payload) != 2:
                raise ValueError(f"line {rec.line_no}: bad ELA record")
            upper = ((rec.payload[0] << 8) | rec.payload[1]) << 16
        elif rec.type == 0x02:
            if len(rec.payload) != 2:
                raise ValueError(f"line {rec.line_no}: bad ESA record")
            upper = ((rec.payload[0] << 8) | rec.payload[1]) << 4
    if lowest is None:
        return 0, 0, -1
    return written, lowest, highest
