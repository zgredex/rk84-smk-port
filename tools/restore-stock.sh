#!/usr/bin/env bash
# Restore the verified stock RK84 firmware via sinowisp EP0.
#
# REFUSES unless the stock backup MD5 is exactly 4ca60eb0...
#
# enter-isp failure handling: if enter-isp fails, the wrapper verifies
# the ISP bootloader (0603:1020) is actually present before continuing;
# otherwise it stops and preserves logs.
#
# Usage:
#   ./restore-stock.sh [--backup <path>] [--sinowisp <path>]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BACKUP="${BACKUP:-$REPO_ROOT/../via-lite/backup/rk68-mac-backup.bin}"
SINOWISP="${SINOWISP:-./rk68-sinowisp-macos-ep0}"
EXPECT_MD5="4ca60eb0799b5ee1b4247056df8ec1f0"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backup) BACKUP="${2:?}"; shift 2 ;;
        --sinowisp) SINOWISP="${2:?}"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

TS="$(date +%Y-%m-%dT%H:%M:%S%z)"
LOGDIR="restore-${TS}"
mkdir -p "$LOGDIR"

fail() { echo "ERROR: $*" >&2; exit 3; }

[[ -f "$BACKUP" ]] || fail "stock backup not found: $BACKUP"
[[ -x "$SINOWISP" ]] || fail "sinowisp not executable: $SINOWISP"

MD5="$(md5 -q "$BACKUP" 2>/dev/null || md5sum "$BACKUP" | awk '{print $1}')"
SHA="$(shasum -a 256 "$BACKUP" 2>/dev/null | awk '{print $1}')"
echo "backup:      $BACKUP"       | tee "$LOGDIR/session.txt"
echo "backup md5:  $MD5"          | tee -a "$LOGDIR/session.txt"
echo "backup sha256: $SHA"        | tee -a "$LOGDIR/session.txt"

if [[ "$MD5" != "$EXPECT_MD5" ]]; then
    fail "stock backup MD5 mismatch (got $MD5, want $EXPECT_MD5) — refusing to flash"
fi
echo "backup:      VERIFIED (stock image)" | tee -a "$LOGDIR/session.txt"

isp_present() {
    # true when the ISP bootloader 0603:1020 is enumerated
    ioreg -p IOUSB -l 2>/dev/null | grep -qiE "idVendor.*0x0603|0603.*1020" \
        || system_profiler SPUSBDataType 2>/dev/null | grep -qiE "0603|1020"
}

echo "--- enter-isp ---" | tee -a "$LOGDIR/session.txt"
if "$SINOWISP" enter-isp --normal-pid 0x0059 --normal-iface 1 >> "$LOGDIR/enter-isp.log" 2>&1; then
    echo "enter-isp: OK" | tee -a "$LOGDIR/session.txt"
else
    echo "enter-isp: command failed — checking for ISP bootloader" | tee -a "$LOGDIR/session.txt"
    sleep 2
    if isp_present; then
        echo "ISP bootloader (0603:1020) present — continuing" | tee -a "$LOGDIR/session.txt"
    else
        echo "ERROR: ISP bootloader NOT found after enter-isp failure" | tee -a "$LOGDIR/session.txt"
        echo "Logs preserved in $LOGDIR/. Replug the keyboard and retry." >&2
        exit 4
    fi
fi

echo "--- writing stock image ---" | tee -a "$LOGDIR/session.txt"
"$SINOWISP" write --yes "$BACKUP" | tee "$LOGDIR/write.log"

echo "--- done. Verify:" | tee -a "$LOGDIR/session.txt"
echo "  system_profiler SPUSBDataType | grep -i keyboard" | tee -a "$LOGDIR/session.txt"
echo "  expect 258a:0059 'RK Bluetooth Keyboar'" | tee -a "$LOGDIR/session.txt"
echo "logs: $LOGDIR/"
