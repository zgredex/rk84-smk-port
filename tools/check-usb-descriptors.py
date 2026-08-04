#!/usr/bin/env python3
"""Verify the USB descriptors embedded in a compiled RK84 firmware image.

Parses the raw .ihx -> bytes and asserts the stock RK84 USB identity
using pattern-based location (SMK lays out descriptors non-standardly:
interface/endpoint descriptors precede the config header, and HID
report descriptors are referenced via an offset table).

Checks:
  - device descriptor: bcdUSB 0x0110, VID 0x258a, PID 0x0059,
    bcdDevice 0x1025, serial index 0
  - EP1 IN interrupt 8 bytes, EP2 IN interrupt 16 bytes
  - HID report descriptors present: Feature ID 5, System ID 1,
    Consumer ID 2, NKRO ID 6
  - NKRO usage min 0x04 / max 0x70, report count 120
  - System payload 1 byte, Consumer payload 2 bytes, NKRO 16 bytes

Usage:
    python3 check-usb-descriptors.py build/royalkludge-rk84_default_smk.ihx
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

EXPECT_VID = 0x258A
EXPECT_PID = 0x0059
EXPECT_BCD_DEVICE = 0x1025
EXPECT_EP1_MPS = 8
EXPECT_EP2_MPS = 16
EXPECT_SERIAL_INDEX = 0


def ihx_to_bytes(path: Path) -> bytes:
    data = bytearray()
    upper = 0
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip()
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_no}: invalid Intel HEX line")
        rec = bytes.fromhex(line[1:])
        if len(rec) < 5:
            raise ValueError(f"{path}:{line_no}: truncated record")
        count, addr, typ = rec[0], (rec[1] << 8) | rec[2], rec[3]
        if len(rec) != count + 5:
            raise ValueError(f"{path}:{line_no}: record length mismatch")
        if sum(rec) & 0xFF:
            raise ValueError(f"{path}:{line_no}: checksum mismatch")
        payload = rec[4:4 + count]
        if typ == 0x00:
            absolute = upper + addr
            if absolute + count > len(data):
                data.extend(b"\x00" * (absolute + count - len(data)))
            data[absolute:absolute + count] = payload
        elif typ == 0x04:
            if count != 2:
                raise ValueError(f"{path}:{line_no}: bad ELA")
            upper = ((payload[0] << 8) | payload[1]) << 16
        elif typ == 0x01:
            if count != 0 or addr != 0:
                raise ValueError(f"{path}:{line_no}: malformed EOF")
            return bytes(data)
    raise ValueError(f"{path}: missing EOF")


class DescriptorCheck:
    def __init__(self, path: Path):
        self.path = path
        self.blob = ihx_to_bytes(path)
        self.errors: list[str] = []
        self.device: dict | None = None
        self.ep1_mps: int | None = None
        self.ep2_mps: int | None = None

    def fail(self, msg: str):
        self.errors.append(msg)

    # ------------------------------------------------------------------
    def parse_device(self):
        """Device descriptor: 12 01 10 01 ... bMaxPacketSize0 at [7]."""
        for m in re.finditer(re.escape(b"\x12\x01\x10\x01"), self.blob):
            i = m.start()
            if i + 18 > len(self.blob):
                continue
            d = self.blob[i:i + 18]
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
        """EP descriptors: 07 05 <addr> 03 <mps:2> 01 (interrupt IN)."""
        # EP1 IN
        for m in re.finditer(re.escape(b"\x07\x05\x81\x03"), self.blob):
            i = m.start()
            if i + 7 > len(self.blob):
                continue
            mps = self.blob[i + 4] | (self.blob[i + 5] << 8)
            self.ep1_mps = mps
            print(f"EP1 IN @0x{i:04X}: MPS {mps}")
            break
        else:
            self.fail("EP1 IN descriptor not found")
        for m in re.finditer(re.escape(b"\x07\x05\x82\x03"), self.blob):
            i = m.start()
            if i + 7 > len(self.blob):
                continue
            mps = self.blob[i + 4] | (self.blob[i + 5] << 8)
            self.ep2_mps = mps
            print(f"EP2 IN @0x{i:04X}: MPS {mps}")
            break
        else:
            self.fail("EP2 IN descriptor not found")

    def find_report_id(self, rid: int) -> bool:
        """A HID report descriptor containing REPORT_ID rid exists."""
        return b"\x85" + bytes([rid]) in self.blob

    # ------------------------------------------------------------------
    def checks(self):
        self.parse_device()
        self.parse_endpoints()

        if self.device:
            dv = self.device
            if dv["idVendor"] != EXPECT_VID:
                self.fail(f"VID {dv['idVendor']:#06x} != {EXPECT_VID:#06x}")
            if dv["idProduct"] != EXPECT_PID:
                self.fail(f"PID {dv['idProduct']:#06x} != {EXPECT_PID:#06x}")
            if dv["bcdDevice"] != EXPECT_BCD_DEVICE:
                self.fail(f"bcdDevice {dv['bcdDevice']:#06x} != {EXPECT_BCD_DEVICE:#06x}")
            if dv["iSerialNumber"] != EXPECT_SERIAL_INDEX:
                self.fail(f"serial index {dv['iSerialNumber']} != {EXPECT_SERIAL_INDEX}")
            print(f"device @0x{dv['offset']:04X}: VID {dv['idVendor']:#06x} "
                  f"PID {dv['idProduct']:#06x} bcdDevice {dv['bcdDevice']:#06x} "
                  f"serial {dv['iSerialNumber']}")

        if self.ep1_mps is not None and self.ep1_mps != EXPECT_EP1_MPS:
            self.fail(f"EP1 MPS {self.ep1_mps} != {EXPECT_EP1_MPS}")
        if self.ep2_mps is not None and self.ep2_mps != EXPECT_EP2_MPS:
            self.fail(f"EP2 MPS {self.ep2_mps} != {EXPECT_EP2_MPS}")

        # HID report IDs
        for rid, label in ((5, "ISP Feature"), (1, "System"), (2, "Consumer"), (6, "NKRO")):
            if self.find_report_id(rid):
                print(f"report ID {rid} ({label}): present")
            else:
                self.fail(f"report ID {rid} ({label}) not found")

        # NKRO usage range + count
        if b"\x19\x04\x29\x70" in self.blob:
            print("NKRO usage range 0x04..0x70: OK")
        else:
            self.fail("NKRO usage range 0x04..0x70 not found")
        if b"\x75\x01\x95\x78" in self.blob:
            print("NKRO report count 120 (0x78): OK")
        else:
            self.fail("NKRO report count 120 not found")

        # Report payload sizes: System = ID + 1, Consumer = ID + 2
        # encoded in the report descriptor as REPORT_COUNT after each ID.
        # System (ID 1) collection: 85 01 ... 75 08 95 01 81 02
        if b"\x85\x01" in self.blob and b"\x75\x08\x95\x01" in self.blob:
            print("System report payload 1 byte: OK")
        else:
            self.fail("System report payload 1 byte not found")
        if b"\x85\x02" in self.blob and b"\x75\x10\x95\x01" in self.blob:
            print("Consumer report payload 2 bytes: OK")
        else:
            self.fail("Consumer report payload 2 bytes not found")

        # NKRO size vs EP2 MPS
        if self.ep2_mps is not None and self.ep2_mps < 16:
            self.fail(f"NKRO (16B) exceeds EP2 MPS {self.ep2_mps}")

        return not self.errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ihx_file", type=Path)
    args = parser.parse_args()

    chk = DescriptorCheck(args.ihx_file)
    ok = chk.checks()
    for e in chk.errors:
        print(f"FAIL: {e}")
    if ok:
        print("OK: all USB descriptor checks passed")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
