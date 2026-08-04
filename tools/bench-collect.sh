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

    # manifest match (REQUIRED when flashing)
    MANIFEST="$(dirname "$FLASH_HEX")/MANIFEST.txt"
    [[ -f "$MANIFEST" ]] || { echo "ERROR: MANIFEST.txt required next to $FLASH_HEX" >&2; exit 3; }
    HEX_SHA="$(shasum -a 256 "$FLASH_HEX" | awk '{print $1}')"
    get_field() { grep "^$1:" "$MANIFEST" | head -1 | awk '{print $2}' || true; }
    MAN_SHA="$(get_field hex_sha256)"
    MAN_STAGE="$(get_field stage)"
    MAN_PWM0="$(get_field pwm_epwm0_count)"
    MAN_PWM_C2="$(get_field pwm_00c2_count)"
    MAN_PWM_CA="$(get_field pwm_00ca_count)"
    MAN_VIDPID="$(get_field usb_vid_pid)"
    MAN_PORT="$(get_field port_commit)"
    MAN_PINNED="$(get_field pinned_smk_commit)"
    MAN_HIGHEST="$(get_field highest_written_address)"
    CURRENT_COMMIT="$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null || echo n/a)"
    ok=1
    [[ -n "$MAN_SHA" ]] || { echo "manifest: hex_sha256 missing" >&2; ok=0; }
    [[ "$MAN_STAGE" == "recovery" ]] || { echo "manifest stage != recovery" >&2; ok=0; }
    [[ "$HEX_SHA" == "$MAN_SHA" ]] || { echo "manifest sha mismatch" >&2; ok=0; }
    [[ "$MAN_PWM0" == "0" ]] || { echo "manifest pwm_epwm0_count != 0" >&2; ok=0; }
    [[ "$MAN_PWM_C2" == "0" ]] || { echo "manifest pwm_00c2_count != 0" >&2; ok=0; }
    [[ "$MAN_PWM_CA" == "0" ]] || { echo "manifest pwm_00ca_count != 0" >&2; ok=0; }
    [[ "$MAN_VIDPID" == "258A:0059" ]] || { echo "manifest VID:PID mismatch" >&2; ok=0; }
    [[ "$MAN_PORT" == "$CURRENT_COMMIT" ]] || { echo "manifest port_commit ($MAN_PORT) != checked-out ($CURRENT_COMMIT)" >&2; ok=0; }
    [[ "$MAN_PINNED" == "08f4d0253389551b9ae9aad2464e2d7cacaf662e" ]] || { echo "manifest pinned_smk_commit mismatch" >&2; ok=0; }
    if [[ -n "$MAN_HIGHEST" ]]; then
        if python3 -c "exit(0 if int('$MAN_HIGHEST',16) < 0xBC00 else 1)" 2>/dev/null; then
            :
        else
            echo "manifest highest_written_address >= 0xBC00" >&2; ok=0
        fi
    else
        echo "manifest highest_written_address missing" >&2; ok=0
    fi
    [[ $ok -eq 1 ]] || { echo "ERROR: manifest mismatch (fields above)" >&2; exit 3; }
    echo "manifest: verified (stage=recovery, sha match, pwm 0/0/0, 258A:0059, commit $MAN_PORT)" >> "$OUT/session.txt"

    echo "firmware:     $FLASH_HEX" >> "$OUT/session.txt"
    echo "firmware_sha256: $(shasum -a 256 "$FLASH_HEX" | awk '{print $1}')" >> "$OUT/session.txt"
    cp "$FLASH_HEX" "$OUT/recovery-requested.hex"
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

# ---------------------------------------------------------------------
# device_present <vid_hex> <pid_hex> : one USB node has BOTH ids
# (same-node verification via system_profiler -json)
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

# =====================================================================
# Flash sequence (only with --flash)
# =====================================================================
if [[ -n "$FLASH_HEX" ]]; then
    echo "--- enter-isp ---" | tee -a "$OUT/session.txt"
    set +e
    # enter-isp takes no subcommand args; the normal-PID/interface are
    # TOP-LEVEL options (defaults 0x258a / 0x0059 / iface 1 are correct
    # for the RK84, so they are omitted unless overridden).
    "$SINOWISP" enter-isp \
        >> "$OUT/enter-isp.log" 2>&1
    enter_rc=$?
    set -e
    sleep 2

    if device_present "0x0603" "0x1020"; then
        echo "ISP bootloader (0603:1020): verified on one USB node" | tee -a "$OUT/session.txt"
    else
        echo "ERROR: ISP node 0603:1020 NOT found (enter-isp rc=$enter_rc)" | tee -a "$OUT/session.txt"
        record_usb "after-enterisp-fail"
        exit 4
    fi
    record_usb "after-enterisp"

    echo "--- write ---" | tee -a "$OUT/session.txt"
    if "$SINOWISP" write --yes "$FLASH_HEX" >> "$OUT/write.log" 2>&1; then
        echo "write: OK" | tee -a "$OUT/session.txt"
        # copy only after the write succeeded (failed sessions never
        # contain a file named as though it was flashed)
        cp "$FLASH_HEX" "$OUT/recovery-flashed.hex"
    else
        echo "write: FAIL (see write.log) — replug and retry (macOS USB wedge)" \
            | tee -a "$OUT/session.txt"
        record_usb "after-write-fail"
        exit 5
    fi

    # poll for the normal node (up to ~15 s); replug often takes longer
    # than a single fixed wait on macOS
    echo "--- waiting for normal enumeration (258a:0059) ---" | tee -a "$OUT/session.txt"
    normal_ok=0
    for _ in $(seq 1 30); do
        if device_present "0x258a" "0x0059"; then
            normal_ok=1
            break
        fi
        sleep 0.5
    done

    if [[ $normal_ok -eq 1 ]]; then
        echo "normal enumeration (258a:0059): verified after write" | tee -a "$OUT/session.txt"
    else
        echo "ERROR: normal node 258a:0059 NOT found after write (30s)" | tee -a "$OUT/session.txt"
        record_usb "after-write-nonormal"
        exit 6
    fi
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
