# SMK84 Configurator — Non-Negotiable Invariants

Source: `docs/specs/SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md` (spec §31/§32 + final instruction).

These are the hard rules for the entire configurator implementation.
Every implementation choice is checked against them. If a choice conflicts
with any statement below, **the choice is wrong**.

## The eight invariants

```text
NEVER ERASE OR PROGRAM 0xEE00-0xEFFF.          (redirect sector, incl. 0xEFFC)
NEVER ERASE OR PROGRAM 0xF000-0xFFFF.          (bootloader/recovery region)
NEVER ACCEPT A RAW FLASH ADDRESS FROM THE HOST. (object-level ops only)
NEVER WRITE FLASH FROM AN INTERRUPT.            (main loop only)
NEVER RUN CUSTOM ANIMATION BYTECODE IN THE PWM ISR.
ALWAYS KEEP A COMPILED DEFAULT KEYMAP AND RGB FALLBACK.
ALWAYS KEEP PHYSICAL RECOVERY INDEPENDENT OF USER CONFIGURATION.
ALWAYS KEEP THE USB/RGB RUNTIME BEHAVIOR VERIFIED ON THE BENCH UNCHANGED.
```

## Flash layout (proposed, milestone M5)

```text
0x0000-0xDDFF  application code            (linker cap 0xDE00, was 0xEC00)
0xDE00-0xE5FF  configuration slot A, 2048 B
0xE600-0xEDFF  configuration slot B, 2048 B
0xEE00-0xEFFF  application redirect sector  IMMUTABLE
0xF000-0xFFFF  bootloader/recovery region   IMMUTABLE
```

- 512-byte sectors. Slot boundaries are sector-aligned.
- `config_range_allowed()` must fail closed on every erase/program call.
- SSP erase/program primitives stay private to `config_store.c` (never exported).
- `0xEFFC` redirect slot is written ONLY by the sinowisp flash tool.

## Protocol invariants

- Report ID 8 (vendor usage page 0xFF60) on the extra HID interface via EP0.
- 31-byte protocol payload; multi-packet SET_REPORT/GET_REPORT must work
  (EP0 max packet = 8 bytes).
- Explicit byte serialization only — no packed C structs across USB.
- Validate length + offset BEFORE any memcpy. `offset + length <= object_size`.
- ISR: copy to bounded mailbox, set flag, return. No flash/parse/keymap/VM/ISP.
- One outstanding transaction; BUSY on collision.
- COMMIT_STAGE: require all keys released (CFG_STATUS_KEYS_HELD).
- Commit marker (0xA5) programmed LAST; old slot retained until verified.

## VM invariants

- Bytecode is data, never native 8051 code. No code pointers, no function calls.
- No backward jumps / unbounded loops / recursion. Bounded: 512 B bytecode,
  16 params, 16 stack depth, 24 ops/LED, 8 events, 20 Hz.
- Validate fully before executing. No memory load/store instructions.
- PWM ISR never interprets bytecode; main loop generates frames.

## CI gates (every build)

- app HEX must not contain bytes at/above 0xDE00
- slot constants sector-aligned, no overlap, below 0xEE00
- protected-address unit tests run
- no generic host-controlled flash address in protocol code
- static XRAM <= 3072 B
- delay ABI test does not skip
- recovery image: no PWM/RGB enable
- USB descriptors valid (EP1 8 B, EP2 16 B, ID 5 ISP 5-B, NKRO stock-format)
- RGB max duty 2550 < 2560

## Status

- Spec archived: `docs/specs/SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md`
- Baseline commit: `8a599d4`
- Implementation: milestone M0 done (this file); M1..M8 pending per spec §30.
- Hardware gates: RGB plane colors, LED map (`led: null` until measured),
  VM perf numbers, all of Part XII acceptance (needs the bench unit).
