#!/usr/bin/env python3
"""Generate a build manifest tying a firmware artifact to its source state.

Outputs (all required fields; exits nonzero if any is missing):
  - port commit (this repo) + pinned SMK commit
  - SDCC / meson / ninja versions
  - stage + RK84 build options (via `meson introspect --buildoptions`)
  - HEX/IHX SHA-256
  - written_byte_count / lowest / highest / image_span
  - PWM-signature counts (recovery invariant)
  - USB VID:PID + bcdDevice (from the device descriptor)

NOTE: memory usage (XRAM/IDATA) is NOT reported — map-file parsing is
not implemented; do not claim it.

Usage:
    python3 make-manifest.py <hex> <ihx> [--build-dir <dir>] [--out <file>]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PINNED_SMK = "08f4d0253389551b9ae9aad2464e2d7cacaf662e"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hexlib import hex_extent, parse_hex  # noqa: E402


def sh(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True).stdout.strip()
    except Exception:
        return ""


def meson_options(build_dir: Path) -> list[str]:
    """RK84-relevant build options via meson introspect."""
    try:
        out = subprocess.run(
            ["meson", "introspect", str(build_dir), "--buildoptions"],
            capture_output=True, text=True, check=True,
        ).stdout
        opts = json.loads(out)
    except Exception:
        return []
    wanted = []
    for o in opts:
        name = o.get("name", "")
        if name.startswith("rk84_") or name == "nkro":
            wanted.append(f"{name}={o.get('value')}")
    return sorted(wanted)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hex", type=Path)
    parser.add_argument("ihx", type=Path)
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    manifest: list[str] = []
    add = manifest.append

    # ---- source state (required) ----
    port_commit = sh(["git", "-C", str(REPO), "rev-parse", "HEAD"])
    if not port_commit:
        raise SystemExit("FAIL: port commit unavailable")
    add(f"port_commit:       {port_commit}")
    add(f"pinned_smk_commit: {PINNED_SMK}")

    # ---- toolchain (required) ----
    sdcc_v = sh(["sdcc", "--version"])
    if not sdcc_v:
        raise SystemExit("FAIL: sdcc version unavailable")
    add(f"sdcc:              {sdcc_v.splitlines()[0]}")
    add(f"meson:             {sh(['meson', '--version'])}")
    add(f"ninja:             {sh(['ninja', '--version'])}")

    # ---- stage + options (required) ----
    opts = meson_options(args.build_dir) if args.build_dir else []
    stage = "unknown"
    for o in opts:
        if o.startswith("rk84_stage="):
            stage = o.split("=", 1)[1]
    if stage == "unknown":
        raise SystemExit("FAIL: rk84_stage not resolvable (need --build-dir)")
    add(f"stage:             {stage}")
    add(f"build_options:    " + (", ".join(opts) if opts else "(none)"))

    # ---- artifact identity (required) ----
    hex_sha = hashlib.sha256(args.hex.read_bytes()).hexdigest()
    ihx_sha = hashlib.sha256(args.ihx.read_bytes()).hexdigest()
    add(f"hex_sha256:        {hex_sha}")
    add(f"ihx_sha256:        {ihx_sha}")

    written, lowest, highest = hex_extent(args.hex)
    if highest < 0:
        raise SystemExit("FAIL: no data records in hex")
    add(f"written_byte_count: {written}")
    add(f"lowest_written_address: 0x{lowest:04X}")
    add(f"highest_written_address: 0x{highest:04X}")
    add(f"image_span:        0x{highest - lowest + 1:04X} ({highest - lowest + 1} bytes)")

    # ---- PWM signatures (required) ----
    blob = parse_hex(args.ihx)
    epwm0 = blob.count(bytes.fromhex("43 A9 02"))
    c2 = blob.count(bytes.fromhex("90 FF 80 74 C2 F0"))
    ca = blob.count(bytes.fromhex("90 FF 80 74 CA F0"))
    add(f"pwm_epwm0_count:  {epwm0}")
    add(f"pwm_00c2_count:   {c2}")
    add(f"pwm_00ca_count:   {ca}")

    # ---- USB identity (required) ----
    m = re.search(re.escape(b"\x12\x01\x10\x01"), blob)
    if not m:
        raise SystemExit("FAIL: device descriptor not found")
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
