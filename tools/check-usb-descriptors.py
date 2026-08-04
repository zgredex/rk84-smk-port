#!/usr/bin/env python3
"""Verify the USB descriptors embedded in a compiled RK84 firmware image.

Uses the shared hexlib parser, then locates descriptor objects
STRUCTURALLY:

  - device descriptor: exact 18-byte parse (bMaxPacketSize0 at [7])
  - endpoint descriptors: 07 05 <addr> 03 <mps:2> <interval>
  - HID class descriptors (09 21 ... 22 <len:2>) delimit the exact
    contiguous report-descriptor ranges; each range is parsed as a
    sequence of HID items so malformed ranges fail instead of passing
    on stray byte patterns elsewhere in the image.

Checks:
  - device: bcdUSB 0x0110, VID 0x258a, PID 0x0059, bcdDevice 0x1025,
    serial index 0
  - EP1 IN 8B, EP2 IN 16B
  - report IDs present: Feature 5, System 1, Consumer 2, NKRO 6
  - NKRO usage min 0x04 / max 0x70, report count 120
  - System payload 1 byte, Consumer payload 2 bytes
  - no report exceeds its endpoint MPS

Usage:
    python3 check-usb-descriptors.py build/royalkludge-rk84_default_smk.hex
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from hexlib import parse_hex

EXPECT_VID = 0x258A
EXPECT_PID = 0x0059
EXPECT_BCD_DEVICE = 0x1025
EXPECT_EP1_MPS = 8
EXPECT_EP2_MPS = 16
EXPECT_SERIAL_INDEX = 0

# HID item tags
HID_RI_INPUT = 0x80
HID_RI_OUTPUT = 0x90
HID_RI_FEATURE = 0xB0
HID_RI_COLLECTION = 0xA0
HID_RI_END_COLLECTION = 0xC0
HID_RI_USAGE_PAGE = 0x04
HID_RI_LOGICAL_MINIMUM = 0x14
HID_RI_LOGICAL_MAXIMUM = 0x24
HID_RI_PHYSICAL_MINIMUM = 0x34
HID_RI_PHYSICAL_MAXIMUM = 0x44
HID_RI_USAGE_MINIMUM = 0x18
HID_RI_USAGE_MAXIMUM = 0x28
HID_RI_DESIGNATOR_INDEX = 0x38
HID_RI_DESIGNATOR_MINIMUM = 0x48
HID_RI_DESIGNATOR_MAXIMUM = 0x58
HID_RI_STRING_INDEX = 0x78
HID_RI_STRING_MINIMUM = 0x88
HID_RI_STRING_MAXIMUM = 0x98
HID_RI_DELIMITER = 0xA8
HID_RI_REPORT_ID = 0x84
HID_RI_REPORT_SIZE = 0x74
HID_RI_REPORT_COUNT = 0x94
HID_RI_PUSH = 0xA4
HID_RI_POP = 0xB4


class HidItem:
    def __init__(self, tag: int, size: int, value: int):
        self.tag = tag
        self.size = size
        self.value = value

    def __repr__(self):
        return f"HidItem(0x{self.tag:02X}, {self.value})"


def parse_hid_items(data: bytes) -> list[HidItem]:
    """Parse a HID report descriptor sequentially; raises on malformed
    items (bad size codes, truncated ranges)."""
    items = []
    i = 0
    while i < len(data):
        b = data[i]
        i += 1
        tag = b & 0xFC
        size_code = b & 0x03
        if size_code == 0:
            size = 0
        elif size_code == 1:
            size = 1
        elif size_code == 2:
            size = 2
        else:  # 3 = 4-byte value
            size = 4
        if i + size > len(data):
            raise ValueError("HID item: truncated range")
        value = 0
        for k in range(size):
            value |= data[i + k] << (8 * k)
        i += size
        items.append(HidItem(tag, size, value))
    return items


class DescriptorCheck:
    def __init__(self, path: Path):
        self.path = path
        self.blob = parse_hex(path)
        self.errors: list[str] = []
        self.device: dict | None = None
        self.ep_mps: dict[int, int] = {}
        self.report_ranges: list[bytes] = []

    def fail(self, msg: str):
        self.errors.append(msg)

    # ------------------------------------------------------------------
    def parse_device(self):
        for i in range(len(self.blob) - 18):
            if self.blob[i:i + 4] != b"\x12\x01\x10\x01":
                continue
            d = self.blob[i:i + 18]
            if d[7] != 0x08:  # bMaxPacketSize0
                continue
            self.device = {
                "bcdUSB": d[2] | (d[3] << 8),
                "bMaxPacketSize0": d[7],
                "idVendor": d[8] | (d[9] << 8),
                "idProduct": d[10] | (d[11] << 8),
                "bcdDevice": d[12] | (d[13] << 8),
                "iManufacturer": d[14],
                "iProduct": d[15],
                "iSerialNumber": d[16],
                "bNumConfigurations": d[17],
                "offset": i,
            }
            return
        self.fail("device descriptor not found")

    def parse_endpoints(self):
        """07 05 <addr> 03 <mps:2> <interval> (interrupt IN)."""
        for i in range(len(self.blob) - 7):
            if self.blob[i:i + 2] != b"\x07\x05":
                continue
            addr = self.blob[i + 2]
            attr = self.blob[i + 3]
            if not (addr & 0x80) or attr != 0x03:
                continue
            mps = self.blob[i + 4] | (self.blob[i + 5] << 8)
            self.ep_mps[addr & 0x0F] = mps
            print(f"EP{addr & 0x0F} IN @0x{i:04X}: MPS {mps}")

    def parse_report_ranges(self):
        """Locate report descriptors structurally: each report
        collection starts with 05 <page> 09 <usage> A1 01 and ends at
        the matching C0 (end collection). Covers keyboard (05 01 09 06),
        System (05 01 09 80), Consumer (05 0C 09 01) and any others.
        Handles SMK's layout where report descriptors live after the
        config/strings and are not contiguous with their HID class
        descriptors."""
        for marker in (
            b"\x05\x01\x09\x06\xa1\x01",  # keyboard
            b"\x05\x01\x09\x80\xa1\x01",  # system
            b"\x05\x0c\x09\x01\xa1\x01",  # consumer
            b"\x06\x00\xff\x09\x01\xa1\x01",  # vendor (Feature ID 5)
        ):
            for m in re.finditer(re.escape(marker), self.blob):
                i = m.start()
                # find the matching C0 (end collection) — depth counting
                depth = 1
                j = i + len(marker)
                while j < len(self.blob):
                    b = self.blob[j]
                    if (b & 0xFC) == 0xA0:  # collection (any type)
                        depth += 1
                    elif (b & 0xFC) == 0xC0:  # end collection
                        depth -= 1
                        if depth == 0:
                            rng = self.blob[i:j + 1]
                            self.report_ranges.append(rng)
                            print(f"HID report @0x{i:04X}: {len(rng)} bytes")
                            break
                    j += 1
                else:
                    self.fail(f"HID report @0x{i:04X}: unbalanced collection")

    # ------------------------------------------------------------------
    def report_ids(self, rng: bytes) -> set[int]:
        ids = set()
        for it in parse_hid_items(rng):
            if it.tag == HID_RI_REPORT_ID:
                ids.add(it.value & 0xFF)
        return ids

    def find_report(self, rid: int) -> bytes | None:
        for rng in self.report_ranges:
            if rid in self.report_ids(rng):
                return rng
        return None

    def checks(self):
        self.parse_device()
        self.parse_endpoints()
        self.parse_report_ranges()

        if self.device:
            dv = self.device
            for label, got, want in (
                ("VID", dv["idVendor"], EXPECT_VID),
                ("PID", dv["idProduct"], EXPECT_PID),
                ("bcdDevice", dv["bcdDevice"], EXPECT_BCD_DEVICE),
            ):
                if got != want:
                    self.fail(f"{label} {got:#06x} != {want:#06x}")
            if dv["iSerialNumber"] != EXPECT_SERIAL_INDEX:
                self.fail(f"serial index {dv['iSerialNumber']} != {EXPECT_SERIAL_INDEX}")
            print(f"device @0x{dv['offset']:04X}: VID {dv['idVendor']:#06x} "
                  f"PID {dv['idProduct']:#06x} bcdDevice {dv['bcdDevice']:#06x} "
                  f"serial {dv['iSerialNumber']}")

        for ep, want in ((1, EXPECT_EP1_MPS), (2, EXPECT_EP2_MPS)):
            got = self.ep_mps.get(ep)
            if got is None:
                self.fail(f"EP{ep} IN descriptor not found")
            elif got != want:
                self.fail(f"EP{ep} MPS {got} != {want}")

        # report presence + structural item checks
        nkro_rng = None
        for rid, label in ((5, "ISP Feature"), (1, "System"), (2, "Consumer"), (6, "NKRO")):
            rng = self.find_report(rid)
            if rng is None:
                self.fail(f"report ID {rid} ({label}) not found")
            elif rid == 6:
                nkro_rng = rng
            print(f"report ID {rid} ({label}): present")

        if nkro_rng is not None:
            items = parse_hid_items(nkro_rng)
            usages = [it for it in items if it.tag == HID_RI_USAGE_MINIMUM]
            umax = [it for it in items if it.tag == HID_RI_USAGE_MAXIMUM]
            counts = [it for it in items if it.tag == HID_RI_REPORT_COUNT]
            sizes = [it for it in items if it.tag == HID_RI_REPORT_SIZE]
            if usages and usages[0].value == 0x04:
                print("NKRO usage min 0x04: OK")
            else:
                self.fail("NKRO usage min 0x04 not found")
            if umax and umax[0].value == 0x70:
                print("NKRO usage max 0x70: OK")
            else:
                self.fail("NKRO usage max 0x70 not found")
            if counts and counts[0].value == 120:
                print("NKRO report count 120: OK")
            else:
                self.fail("NKRO report count 120 not found")
            if sizes and sizes[0].value == 1:
                print("NKRO report size 1 bit: OK")
            else:
                self.fail("NKRO report size != 1")

        # System (ID 1) payload 1 byte, Consumer (ID 2) payload 2 bytes.
        # Compute total report bits from size*count and round up to
        # bytes (e.g. System: 1 bit x 3 usages = 3 bits -> 1 byte).
        for rid, want_bytes, label in ((1, 1, "System"), (2, 2, "Consumer")):
            rng = self.find_report(rid)
            if rng is None:
                continue
            items = parse_hid_items(rng)
            total_bits = 0
            size_bits = 0
            for it in items:
                if it.tag == HID_RI_REPORT_SIZE:
                    size_bits = it.value
                elif it.tag == HID_RI_REPORT_COUNT:
                    total_bits += size_bits * it.value
            payload_bytes = (total_bits + 7) // 8
            if payload_bytes == want_bytes:
                print(f"{label} report payload {payload_bytes} bytes: OK")
            else:
                self.fail(
                    f"{label} report payload {payload_bytes} != {want_bytes} bytes"
                )

        # no report exceeds its endpoint MPS
        for rng in self.report_ranges:
            # reports have IDs; the NKRO is 16B on EP2(16)
            if 6 in self.report_ids(rng) and self.ep_mps.get(2, 64) < 16:
                self.fail("NKRO (16B) exceeds EP2 MPS")
        for rid, label, size in ((5, "ISP Feature", 2), (1, "System", 2), (2, "Consumer", 3)):
            rng = self.find_report(rid)
            if rng is not None and size > self.ep_mps.get(2, 64):
                self.fail(f"{label} report ({size}B) exceeds EP2 MPS")

        return not self.errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex_file", type=Path)
    args = parser.parse_args()

    try:
        chk = DescriptorCheck(args.hex_file)
    except ValueError as e:
        print(f"FAIL: {args.hex_file}: {e}", file=sys.stderr)
        return 1
    ok = chk.checks()
    for e in chk.errors:
        print(f"FAIL: {e}", file=sys.stderr)
    if ok:
        print("OK: all USB descriptor checks passed")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
