#!/usr/bin/env bash
# Apply the RK84 patches to a pinned carlossless/smk checkout.
#
# Usage:
#   SMK_DIR=/path/to/smk ./apply-to-smk.sh
#
# Requires the checkout to be at the exact pinned upstream commit.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMK_DIR="${SMK_DIR:-${1:-.}}"
PATCH_DIR="$(dirname "$SCRIPT_DIR")/patches"

expected=08f4d0253389551b9ae9aad2464e2d7cacaf662e
actual=$(git -C "$SMK_DIR" rev-parse HEAD)

if [[ "$actual" != "$expected" ]]; then
    echo "Wrong SMK revision: $actual" >&2
    echo "Expected: $expected" >&2
    echo "Checkout the pinned commit first: git checkout $expected" >&2
    exit 1
fi

# The checkout must be clean (no previous patch application residue).
if ! git -C "$SMK_DIR" diff --quiet ||
   ! git -C "$SMK_DIR" diff --cached --quiet; then
    echo "SMK checkout is not clean" >&2
    echo "Run: git -C '$SMK_DIR' reset --hard $expected && git -C '$SMK_DIR' clean -fd" >&2
    exit 1
fi

board_dst="$SMK_DIR/src/keyboards/royalkludge-rk84"
if [[ -e "$board_dst" ]]; then
    echo "Board destination already exists: $board_dst" >&2
    echo "Remove it first, or reset the checkout." >&2
    exit 1
fi

# 1. Meson: board entry, scoped USB identity defines, no RK84 flash target.
git -C "$SMK_DIR" apply --check "$PATCH_DIR/0001-rk84-meson.patch"
git -C "$SMK_DIR" apply "$PATCH_DIR/0001-rk84-meson.patch"

# 2. Framework: scoped stock NKRO, dual-report gating, sticky-key fix,
#    USB identity defaults + endpoint asserts.
git -C "$SMK_DIR" apply --check "$PATCH_DIR/0002-rk84-framework.patch"
git -C "$SMK_DIR" apply "$PATCH_DIR/0002-rk84-framework.patch"

# 3. Board: copy the royalkludge-rk84 board definition into the tree.
cp -R "$SCRIPT_DIR/../src/keyboards/royalkludge-rk84" \
      "$SMK_DIR/src/keyboards/"

echo "Patches applied. Build with:"
echo "  meson setup build-rk84 --buildtype=release"
echo "  ninja -C build-rk84 royalkludge-rk84_default_smk.hex"
echo "Then check the image:"
echo "  python3 $SCRIPT_DIR/check-hex-bounds.py build-rk84/royalkludge-rk84_default_smk.hex"
