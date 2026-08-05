# RK84 RGB Physical Validation — Session Log

Date: 2026-08-05
Keyboard returned; RGB diagnostic firmware flashed and validated.

## Flash event

- Device: RK Bluetooth Keyboar (258a:0059, vendor iface 1)
- enter-isp: OK (SET_REPORT feature id 0x05 on iface 1; benign macOS
  error 0xe00002ed observed as before — entry succeeds via re-enum)
- ISP: 0603:1020, interface 0
- Wrote: /tmp/smk-verify/build-flash/RK84-RGB-DIAGNOSTIC.bin
  (61,440 B, sha256 ec62a6c0fbaee595…)
- Verify: 30/30 pages read back, verification succeeded
- Reboot: device re-enumerated as 258a:0059

## Hardware checklist results

1. Enumerate + type ............ PASS (258a:0059, iface1 vendor HID)
2. Three-stripe pattern ........ PASS (visible on all rows)
3. Stripe colors ................ **RED / GREEN / BLUE (L to R)**
   - plane 0 = RED, plane 1 = GREEN, plane 2 = BLUE
   - LED order is RGB; plane index IS the color component
4. Fn+Up / Fn+Down brightness ... PASS (steps; RGB_BRI_UP/DN 0x7E40/41
   wired through dynamic keymap path on real hardware)
5. Suspend blanks RGB .......... PASS on disconnect (RGB off on unplug;
   returned on replug — bus-reset hook clears suspended flag)
6. Key press wakes host ........ NOT TESTED (real host sleep deferred)
7. RGB returns after resume .... PASS on reconnect; real sleep deferred
8. Esc+Space recovery .......... DEFERRED (user: later date)
   + stock restore ............. DEFERRED (same gate)

## Model update

tests/rgb_model.py docstring now records the physical confirmation:
three-stripe probe = plane p at col p on every row; colors RED/GREEN/
BLUE; cross-plane zeros at stripe columns; cells outside cols 0-2 zero
in all planes (15/15 rgb-model tests still pass).

## Next steps (when the user is ready)

- Real host suspend test (Mac sleep -> RGB off -> key wakes -> RGB back)
- Esc+Space recovery chord -> 0603:1020
- Stock restore via tools/restore-stock.sh (MD5 gate 4ca60eb0…)
- Then: measured phase tables (PWM0/PWM00CON duty per plane) -> Flash B
  full-board RGB acceptance
- Then M4/M5 milestone firmware (A/B persistence, built-in effects)
