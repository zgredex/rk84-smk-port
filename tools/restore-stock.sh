#!/usr/bin/env bash
# Restore the verified stock RK84 firmware via sinowisp EP0.
#
# REFUSES unless the stock backup MD5 is exactly 4ca60eb0...
#
# ISP verification is strong: after enter-isp (regardless of exit
# status), one USB device node must show idVendor=0x0603 AND
# idProduct=0x1020 together (same node). After the write, one node
# must show 258a:0059. Uses system_profiler -json, not token greps.
#
# Usage:
#   ./restore-stock.sh [--backup <path>] [--sinowisp <path>]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BACKUP="${BACKUP:-$REPO_ROOT/../via-lite/backup/rk68-mac-backup.bin}"
SINOWISP="${SINOWISP:-$REPO_ROOT/rk68-sinowisp-macos-ep0}"
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

# ---------------------------------------------------------------------
# device_present <vid_hex> <pid_hex> : one USB node has BOTH ids
# Uses system_profiler -json; parses with python3 (no token greps).
# ---------------------------------------------------------------------
device_present() {
    local want_vid="$1" want_pid="$2"
    system_profiler SPUSBDataType -json 2>/dev/null | python3 -c "
import json, sys
want_vid, want_pid = '$want_vid', '$want_pid'

def walk(node):
    if isinstance(node, dict):
        vid = node.get('vendor_id') or ''
        pid = node.get('product_id') or ''
        if vid.lower() == want_vid and pid.lower() == want_pid:
            return True
        for v in node.values():
            if walk(v):
                return True
    elif isinstance(node, list):
        return any(walk(v) for v in node)
    return False

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(1)
sys.exit(0 if walk(data) else 1)
"
}

echo "--- enter-isp ---" | tee -a "$LOGDIR/session.txt"
set +e
"$SINOWISP" enter-isp --normal-pid 0x0059 --normal-iface 1 >> "$LOGDIR/enter-isp.log" 2>&1
enter_rc=$?
set -e

# poll for the ISP node (up to ~15 s)
isp_ok=0
for _ in $(seq 1 30); do
    if device_present "0x0603" "0x1020"; then
        isp_ok=1
        break
    fi
    sleep 0.5
done

if [[ $isp_ok -eq 1 ]]; then
    echo "ISP bootloader (0603:1020): verified on one USB node" | tee -a "$LOGDIR/session.txt"
else
    if [[ $enter_rc -eq 0 ]]; then
        echo "ERROR: enter-isp succeeded but ISP node 0603:1020 NOT found" | tee -a "$LOGDIR/session.txt"
    else
        echo "ERROR: enter-isp failed AND ISP node 0603:1020 NOT found" | tee -a "$LOGDIR/session.txt"
    fi
    echo "Logs preserved in $LOGDIR/. Replug the keyboard and retry." >&2
    exit 4
fi

echo "--- writing stock image ---" | tee -a "$LOGDIR/session.txt"
"$SINOWISP" write --yes "$BACKUP" | tee "$LOGDIR/write.log"

# poll for the stock node (up to ~15 s)
echo "--- waiting for stock enumeration (258a:0059) ---" | tee -a "$LOGDIR/session.txt"
stock_ok=0
for _ in $(seq 1 30); do
    if device_present "0x258a" "0x0059"; then
        stock_ok=1
        break
    fi
    sleep 0.5
done

if [[ $stock_ok -eq 1 ]]; then
    echo "stock enumeration (258a:0059): verified" | tee -a "$LOGDIR/session.txt"
    echo "--- done. Restore verified." | tee -a "$LOGDIR/session.txt"
    echo "logs: $LOGDIR/"
else
    echo "ERROR: stock node 258a:0059 NOT found after write (30s)" | tee -a "$LOGDIR/session.txt"
    echo "Replug and re-run restore-stock.sh." >&2
    exit 5
fi
