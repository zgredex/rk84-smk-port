#!/usr/bin/env python3
"""Generate a build manifest tying a firmware artifact to its source state.

Extracts from a built .hex/.ihx and the repository:
  - port commit (this repo) + pinned SMK commit
  - SDCC / meson / ninja versions
  - build options (from build.ninja)
  - HEX SHA-256, highest written address, code size
  - XRAM usage (from the .map when present)
  - PWM-signature counts (recovery invariant)
  - USB VID:PID (from the device descriptor)

Usage:
    python3 make-manifest.py <hex> <ihx> [build_dir]
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def sh(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True).stdout.strip()
    except Exception:
        return ""


def ihx_to_bytes(path: Path) -> bytes:
    data = bytearray()
    upper = 0
    for line in path.read_text().splitlines():
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        count, addr, typ = rec[0], (rec[1] << 8) | rec[2], rec[3]
        payload = rec[4:4 + count]
        if typ == 0x00:
            absolute = upper + addr
            if absolute + count > len(data):
                data.extend(b"\x00" * (absolute + count - len(data)))
            data[absolute:absolute + count] = payload
        elif typ == 0x04:
            upper = ((payload[0] << 8) | payload[1]) << 16
        elif typ == 0x01:
            return bytes(data)
    return bytes(data)


def highest_hex_address(path: Path) -> int:
    upper = 0
    highest = -1
    for line in path.read_text().splitlines():
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        count, addr, typ = rec[0], (rec[1] << 8) | rec[2], rec[3]
        if typ == 0x00:
            highest = max(highest, upper + addr + count - 1)
        elif typ == 0x04:
            upper = ((rec[4] << 8) | rec[5]) << 16
    return highest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex", type=Path)
    parser.add_argument("ihx", type=Path)
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    manifest: list[str] = []
    add = manifest.append

    # source state
    add(f"port_commit:       {sh(['git', '-C', str(REPO), 'rev-parse', 'HEAD'])}")
    add(f"pinned_smk_commit: 08f4d0253389551b9ae9aad2464e2d7cacaf662e")

    # toolchain
    add(f"sdcc:              {sh(['sdcc', '--version']).splitlines()[0] if sh(['sdcc','--version']) else 'n/a'}")
    add(f"meson:             {sh(['meson', '--version'])}")
    add(f"ninja:             {sh(['ninja', '--version'])}")

    # build options: pull -D defines from the RK84 compile lines only
    opts: list[str] = []
    if args.build_dir and (args.build_dir / "build.ninja").exists():
        ninja = (args.build_dir / "build.ninja").read_text()
        # only lines compiling the rk84 board's own sources
        for line in ninja.splitlines():
            if "royalkludge-rk84" not in line or "COMMAND" not in line:
                continue
            for m in re.finditer(r"-D(RK84_[A-Z_]+=[01]|SMK_ACTIVE_KEYCODE_CACHE=[01]|NKRO_ENABLE=[01]|USB_[A-Z0-9_]+=(?:0x[0-9a-fA-F]+|[0-9]+))", line):
                if m.group(1) not in opts:
                    opts.append(m.group(1))
        # stage from -Drk84_* defines
        if any("RK84_RECOVERY_ONLY=1" in o for o in opts):
            add("stage:             recovery")
        elif any("RK84_RGB_ENABLE=1" in o for o in opts):
            add("stage:             rgb")
        else:
            add("stage:             matrix")
        if any("RK84_DUAL_REPORTS=1" in o for o in opts):
            add("dual_reports:      true")
    add("build_options:    " + ", ".join(sorted(opts)) if opts else "n/a")

    # artifact identity
    add(f"hex_sha256:        {hashlib.sha256(args.hex.read_bytes()).hexdigest()}")
    add(f"ihx_sha256:        {hashlib.sha256(args.ihx.read_bytes()).hexdigest()}")
    highest = highest_hex_address(args.hex)
    add(f"highest_address:   0x{highest:04X}")
    blob = ihx_to_bytes(args.ihx)
    add(f"code_size:         0x{len(blob):04X} ({len(blob)} bytes)")

    # PWM signatures
    epwm0 = blob.count(bytes.fromhex("43 A9 02"))
    c2 = blob.count(bytes.fromhex("90 FF 80 74 C2 F0"))
    ca = blob.count(bytes.fromhex("90 FF 80 74 CA F0"))
    add(f"pwm_epwm0_count:  {epwm0}")
    add(f"pwm_00c2_count:   {c2}")
    add(f"pwm_00ca_count:   {ca}")

    # USB identity from device descriptor
    m = re.search(re.escape(b"\x12\x01\x10\x01"), blob)
    if m:
        d = blob[m.start():m.start() + 18]
        vid = d[8] | (d[9] << 8)
        pid = d[10] | (d[11] << 8)
        bcd = d[12] | (d[13] << 8)
        add(f"usb_vid_pid:       {vid:04X}:{pid:04X}")
        add(f"usb_bcd_device:    {bcd:04X}")

    text = "\n".join(manifest) + "\n"
    if args.out:
        args.out.write_text(text)
        print(f"manifest written to {args.out}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
