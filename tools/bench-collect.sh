#!/usr/bin/env bash
# First-flash bench collector for the RK84 recovery image.
#
# HARD PREFLIGHT when --flash is given:
#   - --sinowisp required and executable
#   - firmware file exists
#   - stock backup exists AND its MD5 matches 4ca60eb0...
#   - check-hex-bounds / check-recovery-no-pwm / check-usb-descriptors
#     all pass on the image
#   - a matching manifest exists (stage=recovery, sha256 matches,
#     PWM counts 0/0/0, VID:PID 258A:0059)
#
# Without a verified backup the collector REFUSES to flash.
#
# Usage:
#   ./bench-collect.sh <session-name> --flash <recovery.hex> --sinowisp <path>
set -euo pipefail

SESSION="${1:?usage: bench-collect.sh <session-name> --flash <hex> --sinowisp <path>}"
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BACKUP="${BACKUP:-$REPO_ROOT/../via-lite/backup/rk68-mac-backup.bin}"
EXPECT_MD5="4ca60eb0799b5ee1b4247056df8ec1f0"

OUT="bench-${SESSION}"
mkdir -p "$OUT"
TS="$(date +%Y-%m-%dT%H:%M:%S%z)"
echo "session:      $SESSION"                  > "$OUT/session.txt"
echo "timestamp:    $TS"                      >> "$OUT/session.txt"
echo "host:         $(uname -srm)"            >> "$OUT/session.txt"
echo "port_commit:  $(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo n/a)" >> "$OUT/session.txt"

# =====================================================================
# PREFLIGHT (hard gates)
# =====================================================================
if [[ -n "$FLASH_HEX" ]]; then
    [[ -n "$SINOWISP" ]] || { echo "ERROR: --sinowisp required with --flash" >&2; exit 2; }
    [[ -x "$SINOWISP" ]] || { echo "ERROR: sinowisp not executable: $SINOWISP" >&2; exit 2; }
    [[ -f "$FLASH_HEX" ]] || { echo "ERROR: firmware not found: $FLASH_HEX" >&2; exit 2; }
    [[ -f "$BACKUP" ]] || { echo "ERROR: stock backup not found: $BACKUP" >&2; exit 2; }

    MD5="$(md5 -q "$BACKUP" 2>/dev/null || md5sum "$BACKUP" | awk '{print $1}')"
    echo "stock_backup_md5: $MD5" >> "$OUT/session.txt"
    if [[ "$MD5" != "$EXPECT_MD5" ]]; then
        echo "ERROR: stock backup MD5 mismatch (got $MD5, want $EXPECT_MD5)" >&2
        echo "REFUSING to flash." >&2
        exit 3
    fi
    echo "stock_backup: verified ($EXPECT_MD5)" >> "$OUT/session.txt"

    # run the three image checks
    python3 "$SCRIPT_DIR/check-hex-bounds.py" "$FLASH_HEX" --limit 0xBC00 \
        >> "$OUT/preflight.log" 2>&1 || { echo "ERROR: bounds check failed" >&2; exit 3; }
    python3 "$SCRIPT_DIR/check-recovery-no-pwm.py" "$FLASH_HEX" \
        >> "$OUT/preflight.log" 2>&1 || { echo "ERROR: recovery PWM check failed" >&2; exit 3; }
    python3 "$SCRIPT_DIR/check-usb-descriptors.py" "$FLASH_HEX" \
        >> "$OUT/preflight.log" 2>&1 || { echo "ERROR: descriptor check failed" >&2; exit 3; }

    # manifest match (if a MANIFEST.txt sits next to the hex)
    MANIFEST="$(dirname "$FLASH_HEX")/MANIFEST.txt"
    if [[ -f "$MANIFEST" ]]; then
        HEX_SHA="$(shasum -a 256 "$FLASH_HEX" | awk '{print $1}')"
        MAN_SHA="$(grep '^hex_sha256:' "$MANIFEST" | awk '{print $2}')"
        MAN_STAGE="$(grep '^stage:' "$MANIFEST" | awk '{print $2}')"
        MAN_PWM="$(grep '^pwm_epwm0_count:' "$MANIFEST" | awk '{print $2}')"
        MAN_VIDPID="$(grep '^usb_vid_pid:' "$MANIFEST" | awk '{print $2}')"
        ok=1
        [[ "$MAN_STAGE" == "recovery" ]] || { echo "manifest stage != recovery" >&2; ok=0; }
        [[ "$HEX_SHA" == "$MAN_SHA" ]] || { echo "manifest sha mismatch" >&2; ok=0; }
        [[ "$MAN_PWM" == "0" ]] || { echo "manifest pwm_epwm0 != 0" >&2; ok=0; }
        [[ "$MAN_VIDPID" == "258A:0059" ]] || { echo "manifest VID:PID mismatch" >&2; ok=0; }
        [[ $ok -eq 1 ]] || { echo "ERROR: manifest mismatch" >&2; exit 3; }
        echo "manifest: verified (stage=recovery, sha match, pwm 0, 258A:0059)" >> "$OUT/session.txt"
    else
        echo "WARNING: no MANIFEST.txt next to hex — skipping manifest gate" >> "$OUT/session.txt"
    fi

    echo "firmware:     $FLASH_HEX" >> "$OUT/session.txt"
    echo "firmware_sha256: $(shasum -a 256 "$FLASH_HEX" | awk '{print $1}')" >> "$OUT/session.txt"
    cp "$FLASH_HEX" "$OUT/recovery-flashed.hex"
    echo "PREFLIGHT: ALL GATES PASSED" | tee -a "$OUT/session.txt"
else
    echo "WARNING: no --flash given — collect-only mode (no flashing)" | tee -a "$OUT/session.txt"
fi

# =====================================================================
# USB state capture
# =====================================================================
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

# =====================================================================
# Flash sequence (only with --flash)
# =====================================================================
if [[ -n "$FLASH_HEX" ]]; then
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

# =====================================================================
# HID / descriptor capture
# =====================================================================
{
    echo "=== HID capture ==="
    ioreg -l 2>/dev/null | grep -iE "HID|PrimaryUsage|ReportID|VendorID|ProductID" | head -60 || true
} > "$OUT/hid.txt" 2>&1 || true

echo "=== bench data written to $OUT/ ==="
ls -la "$OUT/"
