"""Tests for tools/check-usb-descriptors.py."""
from __future__ import annotations

import unittest

from . import find_recovery_ihx, run_tool, skip_if_no_build


class DescriptorVerifierTests(unittest.TestCase):
    def test_recovery_descriptors_pass(self):
        skip_if_no_build()
        ihx = find_recovery_ihx()
        proc = run_tool("check-usb-descriptors.py", str(ihx))
        self.assertEqual(proc.returncode, 0, proc.stderr + proc.stdout)
        # key assertions must appear in the output
        for needle in (
            "VID 0x258a",
            "PID 0x0059",
            "bcdDevice 0x1025",
            "serial 0",
            "EP1 IN",
            "MPS 8",
            "EP2 IN",
            "MPS 16",
            "report ID 5 (ISP Feature): present",
            "report ID 6 (NKRO): present",
            "NKRO usage range 0x04..0x70: OK",
            "NKRO report count 120",
            "System report payload 1 byte: OK",
            "Consumer report payload 2 bytes: OK",
        ):
            self.assertIn(needle, proc.stdout, needle)


if __name__ == "__main__":
    unittest.main()
