#!/usr/bin/env bash
# Restore the verified stock RK84 firmware via sinowisp EP0.
#
# REFUSES to run unless the stock backup MD5 is exactly:
#   4ca60eb0799b5ee1b4247056df8ec1f0
#
# Usage:
#   ./restore-stock.sh [--backup <path>] [--sinowisp <path>]
#
# If the keyboard is already in ISP (0603:1020), the app-side jump is
# skipped; otherwise enter-isp is attempted first.
set -euo pipefail

BACKUP="${BACKUP:-../via-lite/backup/rk68-mac-backup.bin}"
SINOWISP="${SINOWISP:-./rk68-sinowisp-macos-ep0}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backup) BACKUP="${2:?}"; shift 2 ;;
        --sinowisp) SINOWISP="${2:?}"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -f "$BACKUP" ]]; then
    echo "ERROR: stock backup not found: $BACKUP" >&2
    exit 3
fi

MD5="$(md5 -q "$BACKUP" 2>/dev/null || md5sum "$BACKUP" | awk '{print $1}')"
echo "backup:      $BACKUP"
echo "backup md5:  $MD5"

if [[ "$MD5" != "4ca60eb0799b5ee1b4247056df8ec1f0" ]]; then
    echo "ERROR: stock backup MD5 mismatch" >&2
    echo "expected 4ca60eb0799b5ee1b4247056df8ec1f0" >&2
    echo "REFUSING to flash. Fix BACKUP and retry." >&2
    exit 4
fi
echo "backup:      VERIFIED (stock image)"

echo "--- entering ISP (skipped if already in ISP) ---"
"$SINOWISP" enter-isp --normal-pid 0x0059 --normal-iface 1 \
    || echo "enter-isp: failed (may already be in ISP; continuing)"
sleep 2

echo "--- writing stock image ---"
"$SINOWISP" write --yes "$BACKUP"

echo "--- done. Verify:"
echo "  system_profiler SPUSBDataType | grep -i keyboard"
echo "  expect 258a:0059 'RK Bluetooth Keyboar'"
