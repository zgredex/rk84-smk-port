# RK84 — RESCUE / RETURN-TO-STOCK

**Read before flashing anything.**

This is the verified escape procedure. It is the primary recovery path and
must remain independent of the application's normal behavior. The ROM
bootloader + sinowisp EP0 flash tool are the central safety mechanism;
application-level escapes (Feature report ID 5, Esc+Space chord) are
fallbacks that only work when the application runs far enough to reach them.

## Verified stock image

```text
stock app image:  rk68-mac-backup.bin   (not committed to this repo)
size              61,440 bytes
MD5               4ca60eb0799b5ee1b4247056df8ec1f0
normal VID:PID    258a:0059
ISP VID:PID       0603:1020
```

The backup image lives in the local workspace (`via-lite/backup/`), not in
this repository. Never commit or overwrite it.

## Verify the stock backup BEFORE every restore

macOS:

```bash
md5 ../via-lite/backup/rk68-mac-backup.bin
```

Linux:

```bash
md5sum ../via-lite/backup/rk68-mac-backup.bin
```

Required result:

```text
4ca60eb0799b5ee1b4247056df8ec1f0
```

If the checksum differs, stop: the backup is not the verified stock
image and must not be used for recovery.

## Non-negotiable recovery layers

1. ROM ISP/bootloader (0xF000-0xFFFF) is never touched — flash writes
   only cover 0x0000-0xEFFF.
2. The 0xEFFC application redirect is under sinowisp control (it writes
   the new firmware's reset vector there on every flash).
3. Runtime Feature report ID 5 with payload `05 75` enters ISP (SMK
   `isp_jump()` = stock sequence: CLR IE.7; B=0xA5; A=0x5A; LJMP 0xFF00).
4. Esc + Space held at power-on jumps to the ROM bootloader (checked at
   the very start of user_init(), before USB init).
5. An experimental image is recoverable even if matrix, RGB, main-loop
   or normal USB-report logic fails (the bootloader is reachable without
   the application).

## Normal runtime path (app running)

```bash
# 1. Enter ISP (sends Feature SET_REPORT ID 5 payload 05 75; the app
#    jumps to the ROM bootloader, which re-enumerates as 0603:1020)
./rk68-sinowisp-macos-ep0 \
    enter-isp \
    --normal-pid 0x0059 \
    --normal-iface 1

# 2. Restore stock app
./rk68-sinowisp-macos-ep0 \
    write \
    --yes \
    ../via-lite/backup/rk68-mac-backup.bin
```

## Already-in-ISP / app-bricked path

`enter-isp` first checks whether the ISP bootloader (0603:1020) is already
present; if it is, the app-side jump is skipped. A bricked application
never blocks re-flashing:

```bash
./rk68-sinowisp-macos-ep0 \
    write \
    --yes \
    ../via-lite/backup/rk68-mac-backup.bin
```

## macOS USB wedge note

After an ISP reboot the whole macOS USB stack can wedge (0 devices in
ioreg). This is HOST-side, not a dead board. Physically unplug and replug
the keyboard (or hub) before assuming anything. Then `list` and continue.

## Rules (NON-NEGOTIABLE)

- NEVER flash at 0xF000+ (bootloader). This repo's builds use
  `--code-size 0xec00`; the first-stage cap is 0xBC00.
- NEVER overwrite the two backup .bin files.
- Always `list` before `write`.
- After an ISP reboot, if USB is dead: replug FIRST, don't panic.
- The Esc+Space chord requires two positive samples 30 ms apart; it is an
  application-level fallback, not a replacement for the ROM/sinowisp path.

## Verified history (bench unit)

- Restored to stock multiple times via the commands above, always
  "Verification succeeded", board back as 258a:0059 "RK Bluetooth
  Keyboar".
- A "completely dead" probe build (no USB, no heartbeat) was recovered
  via the app-bricked path — the bootloader was reachable without the
  application.
