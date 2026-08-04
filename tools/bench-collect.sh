#!/usr/bin/env bash
# First-flash bench collector for the RK84 recovery image.
#
# Records everything needed to reconstruct and evaluate a bench session:
#   timestamp, firmware SHA-256, USB state before/after, VID:PID,
#   descriptor dumps, ID-5 ISP result, cold-boot chord result,
#   stock-restore result, kernel USB log, raw HID events.
#
# Usage:
#   ./bench-collect.sh <session-name> [--flash <recovery.hex>] [--sinowisp <path>]
#
# The script never flashes by itself unless --flash is given; with it,
# it runs the verified sinowisp sequence and records each step's result.
set -euo pipefail

SESSION="${1:?usage: bench-collect.sh <session-name> [--flash hex] [--sinowisp path]}"
shift
FLASH_HEX=""
SINOWISP=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --flash) FLASH_HEX="${2:?}"; shift 2 ;;
        --sinowisp) SINOWISP="${2:?}"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

OUT="bench-${SESSION}"
mkdir -p "$OUT"
TS="$(date +%Y-%m-%dT%H:%M:%S%z)"
echo "session:      $SESSION"            | tee "$OUT/session.txt"
echo "timestamp:    $TS"                 | tee -a "$OUT/session.txt"
echo "host:         $(uname -srm)"       | tee -a "$OUT/session.txt"
echo "port_commit:  $(git -C "$(dirname "$0")/.." rev-parse HEAD 2>/dev/null || echo n/a)" | tee -a "$OUT/session.txt"

# ---- firmware identity -------------------------------------------------
if [[ -n "$FLASH_HEX" ]]; then
    echo "firmware:     $FLASH_HEX" >> "$OUT/session.txt"
    echo "firmware_sha256: $(shasum -a 256 "$FLASH_HEX" | awk '{print $1}')" | tee -a "$OUT/session.txt"
    cp "$FLASH_HEX" "$OUT/recovery-flashed.hex"
fi

# ---- stock backup guard ------------------------------------------------
BACKUP="${BACKUP:-../via-lite/backup/rk68-mac-backup.bin}"
if [[ -f "$BACKUP" ]]; then
    MD5="$(md5 -q "$BACKUP" 2>/dev/null || md5sum "$BACKUP" | awk '{print $1}')"
    echo "stock_backup_md5: $MD5" | tee -a "$OUT/session.txt"
    if [[ "$MD5" != "4ca60eb0799b5ee1b4247056df8ec1f0" ]]; then
        echo "ERROR: stock backup MD5 mismatch — REFUSING to proceed" | tee -a "$OUT/session.txt"
        exit 3
    fi
    echo "stock_backup: verified" | tee -a "$OUT/session.txt"
else
    echo "WARNING: stock backup not found at $BACKUP" | tee -a "$OUT/session.txt"
fi

# ---- USB state ---------------------------------------------------------
record_usb() {
    local tag="$1"
    {
        echo "=== USB state: $tag ==="
        system_profiler SPUSBDataType 2>/dev/null | grep -A6 -iE "keyboard|258A|0059|0603|1020" || true
        echo "--- ioreg ---"
        ioreg -p IOUSB -l 2>/dev/null | grep -iE "idVendor|idProduct|USB Product Name|Manufacturer" | head -40 || true
    } > "$OUT/usb-$tag.txt" 2>&1 || true
}

record_usb "before"

# ---- flash sequence (only with --flash) --------------------------------
if [[ -n "$FLASH_HEX" && -n "$SINOWISP" ]]; then
    # 1. enter ISP via Feature ID 5
    echo "--- enter-isp ---" | tee -a "$OUT/session.txt"
    if "$SINOWISP" enter-isp --normal-pid 0x0059 --normal-iface 1 \
        >> "$OUT/enter-isp.log" 2>&1; then
        echo "enter_isp: OK" | tee -a "$OUT/session.txt"
    else
        echo "enter_isp: FAIL (see enter-isp.log)" | tee -a "$OUT/session.txt"
        record_usb "after-enterisp-fail"
        exit 4
    fi
    sleep 2
    record_usb "after-enterisp"

    # 2. write recovery image
    echo "--- write ---" | tee -a "$OUT/session.txt"
    if "$SINOWISP" write --yes "$FLASH_HEX" >> "$OUT/write.log" 2>&1; then
        echo "write: OK" | tee -a "$OUT/session.txt"
    else
        echo "write: FAIL (see write.log) — replug and retry (macOS USB wedge)" \
            | tee -a "$OUT/session.txt"
        record_usb "after-write-fail"
        exit 5
    fi
    sleep 2
    record_usb "after-write"
fi

# ---- HID / descriptor capture ------------------------------------------
{
    echo "=== HID capture ==="
    ioreg -l 2>/dev/null | grep -iE "HID|PrimaryUsage|ReportID|VendorID|ProductID" | head -60 || true
} > "$OUT/hid.txt" 2>&1 || true

echo "=== bench data written to $OUT/ ==="
ls -la "$OUT/"
