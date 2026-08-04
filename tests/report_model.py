"""Reference model of the RK84 report layer (mirrors smk/report.c).

Implements the stock RK84 report semantics in pure Python so the exact
report bytes can be validated without hardware:

  - 6KRO boot report (ID 0): modifiers + 6 key slots
  - NKRO report (ID 6): 15-bitmap bytes, usages 0x04..0x70
  - System report (ID 1): 2 bytes total (ID + 1 usage byte)
  - Consumer report (ID 2): 3 bytes total (ID + 2 usage bytes)

Dual-report mode sends EP1 + ID6 together; single mode sends one or
the other.
"""
from __future__ import annotations

NKRO_FIRST_USAGE = 0x04
NKRO_LAST_USAGE = 0x70
NKRO_REPORT_BITS = 15
KEYBOARD_REPORT_KEYS = 6

# modifier bits (USB HID keyboard page 0xE0-0xE7 order)
MOD_LCTRL = 0x01
MOD_LSHIFT = 0x02
MOD_LALT = 0x04
MOD_LGUI = 0x08
MOD_RCTRL = 0x10
MOD_RSHIFT = 0x20
MOD_RALT = 0x40
MOD_RGUI = 0x80


class RK84ReportModel:
    def __init__(self, dual: bool = False):
        self.dual = dual
        self.mods = 0
        self.keys: list[int] = []  # 6KRO key slots
        self.nkro = bytearray(NKRO_REPORT_BITS)
        self.system = 0
        self.consumer = 0

    # ------------------------------------------------------------------
    def nkro_usage_valid(self, usage: int) -> bool:
        return NKRO_FIRST_USAGE <= usage <= NKRO_LAST_USAGE

    def add_key(self, usage: int):
        if self.nkro_usage_valid(usage):
            idx = usage - NKRO_FIRST_USAGE
            self.nkro[idx >> 3] |= 1 << (idx & 7)
        if usage not in self.keys and len(self.keys) < KEYBOARD_REPORT_KEYS:
            self.keys.append(usage)

    def del_key(self, usage: int):
        if self.nkro_usage_valid(usage):
            idx = usage - NKRO_FIRST_USAGE
            self.nkro[idx >> 3] &= ~(1 << (idx & 7))
        if usage in self.keys:
            self.keys.remove(usage)

    def add_mods(self, mask: int):
        self.mods |= mask

    def del_mods(self, mask: int):
        self.mods &= ~mask

    def set_system(self, usage: int):
        self.system = usage

    def set_consumer(self, usage: int):
        self.consumer = usage

    # ------------------------------------------------------------------
    def boot_report(self) -> bytes:
        """6KRO: mods + reserved + 6 keys, 8 bytes total, NO report ID
        byte (matches smk report_keyboard_t / stock EP1 report)."""
        keys = self.keys + [0] * (KEYBOARD_REPORT_KEYS - len(self.keys))
        return bytes([self.mods, 0]) + bytes(keys[:KEYBOARD_REPORT_KEYS])

    def nkro_report(self) -> bytes:
        """Stock: ID 6 + 15 bitmap bytes (no modifier byte)."""
        return bytes([6]) + bytes(self.nkro)

    def system_report(self) -> bytes:
        """ID 1 + 1 usage byte = 2 bytes total."""
        return bytes([1, self.system & 0xFF])

    def consumer_report(self) -> bytes:
        """ID 2 + 2 usage bytes little-endian = 3 bytes total."""
        return bytes([2, self.consumer & 0xFF, (self.consumer >> 8) & 0xFF])

    def keyboard_reports(self) -> list[bytes]:
        """Reports sent on every keyboard-state change.

        Firmware behavior (RK84_STOCK_REPORTS):
          - EP1 6KRO boot report: ALWAYS sent
          - EP2 NKRO (ID 6): only when RK84_DUAL_REPORTS is defined

        System and Consumer reports are separate event paths, not part
        of the periodic keyboard report.
        """
        out = [self.boot_report()]
        if self.dual:
            out.append(self.nkro_report())
        return out

    # ------------------------------------------------------------------
    def get_first_key(self) -> int:
        for i, b in enumerate(self.nkro):
            if b:
                bit = 0
                while not (b & (1 << bit)):
                    bit += 1
                return NKRO_FIRST_USAGE + (i << 3) + bit
        return 0  # KC_NO

    def has_anykey(self) -> bool:
        return any(self.nkro) or any(self.keys) or self.mods != 0
