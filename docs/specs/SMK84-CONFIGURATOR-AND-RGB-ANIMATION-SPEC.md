# SMK RK84 Configurator and Uploadable RGB Animation Engine

## Implementation specification for another AI or developer

**Repository:** `zgredex/rk84-smk-port`  
**Specification baseline:** commit `8a599d44e38c61e7506b1c18fe1fd55f158eb943`  
**Pinned upstream SMK:** `08f4d0253389551b9ae9aad2464e2d7cacaf662e`  
**Status date:** 2026-08-05  
**Status:** architecture and implementation plan; configurator protocol and animation VM are not implemented yet.

---

## 1. Purpose

The goal is a small VIA-like utility tailored specifically to SMK and the Royal Kludge RK84.

After installing configurator-enabled SMK firmware once, the user must be able to perform ordinary customization without reflashing:

- edit the base and Fn keymaps;
- preview changes immediately;
- save keymaps persistently to the keyboard;
- configure RGB brightness, colors, speed, and effects;
- create custom procedural RGB animations;
- preview custom animations over USB without flash writes;
- save a selected custom animation to keyboard flash;
- export and import profiles on the computer;
- restore compiled defaults;
- inspect diagnostics;
- deliberately enter the existing recovery/ISP path.

This is inspired by VIA's useful concepts, but it is **not required to implement the VIA protocol or support VIA keyboard definitions exactly**.

The project should reuse only the important architectural ideas:

1. a HID configuration channel;
2. a keyboard definition describing geometry and capabilities;
3. live firmware-owned configuration;
4. explicit persistent saving;
5. a host application that can operate without recompiling firmware.

---

## 2. Truth labels used in this document

Another AI must not confuse confirmed repository facts with design proposals.

### CONFIRMED

Verified in the existing repository, compiled output, or earlier physical testing.

### PROPOSED

The recommended design to implement. It is not yet present in firmware.

### UNKNOWN / HARDWARE GATE

Cannot be considered true until a physical RK84 is available and the relevant test passes.

---

# Part I — Confirmed project state

## 3. Current firmware facts

### 3.1 USB keyboard status — CONFIRMED

The wired USB-full firmware has been physically tested and works as a keyboard.

The existing implementation includes:

- the RK84 16-column × 6-row matrix;
- 84 populated key positions;
- base and Fn layers;
- keyboard, modifier, Consumer, and System reports;
- 6KRO handling and optional NKRO infrastructure;
- lock LED reception;
- USB configuration-state gating;
- suspend/resume handling;
- remote wake;
- state resynchronization after reset/configuration/resume;
- reports blocked before `SET_CONFIGURATION(1)`;
- recovery/ISP entry preserved.

Do not regress this runtime while adding configurator support.

### 3.2 RGB status — PARTLY CONFIRMED

The RGB scheduler and renderer are implemented offline, but the final phase/color/physical LED mapping still requires hardware validation.

Current design:

- 21 PWM source columns;
- 6 electrical RGB rows;
- 3 component planes;
- 19 scheduler phases:
  - phase 0: matrix-only/blank;
  - phases 1–18: six rows × three components;
- RGB and matrix counters advance independently;
- USB suspend and unconfigured states blank RGB while matrix scanning remains active;
- brightness maximum is 5;
- duty formula is:

```c
duty = source * brightness * 2;
```

Maximum:

```text
255 × 5 × 2 = 2550
```

PWM period:

```text
2560
```

Therefore maximum duty remains below the period.

### 3.3 RGB hardware mapping — UNKNOWN / HARDWARE GATE

Do not assume the following until measured:

- raw plane 1 is green;
- raw plane 2 is blue;
- every arithmetic phase maps to the expected physical row;
- every electrical 21 × 6 position has an installed LED;
- source column index equals a particular visible key;
- all 126 electrical RGB positions correspond to physical keys.

Raw plane 0 at one measured position was previously observed as red. That does not prove the complete global mapping.

The configurator keyboard definition must permit `led: null` or an unknown mapping until hardware measurement is complete.

### 3.4 Delay bug — CONFIRMED FIXED

The naked `delay_us()` implementation previously clobbered R6/R7. `delay_ms()` kept its millisecond counter in those registers, so every positive `delay_ms(N)` collapsed to approximately one millisecond.

The fix preserves caller R6, R7, and ACC. A compiled-assembly regression test exists and is run after actual firmware builds.

Do not revert this ABI fix.

### 3.5 XRAM — CONFIRMED

The SH68F90A target is linked with:

```text
--xram-size 0x1000
```

Therefore total XRAM is:

```text
4096 bytes
```

At repository baseline `8a599d4`, current static XDATA usage is reported as:

```text
1207 bytes
```

CI now rejects an RK84 build whose static XDATA exceeds:

```text
3072 bytes
```

The deliberate 1 KiB reserve is for future growth, runtime safety, and configurator/animation state.

Known existing allocations include:

```text
RGB framebuffer:          3 × 126 × 1 = 378 bytes
Active-key release cache: 6 × 16 × 2  = 192 bytes
USB scratch buffer:                       512 bytes
Matrix current/previous arrays:            32 bytes
Reports, USB state, keyboard state:       additional small allocations
```

### 3.6 Dynamic keymap cost — CONFIRMED CALCULATION

Two layers, 6 rows, 16 columns, 16-bit SMK keycodes:

```text
2 × 6 × 16 × 2 = 384 bytes
```

Projected static XDATA after adding one complete runtime keymap:

```text
1207 + 384 = 1591 bytes
```

This leaves substantial room below the 3072-byte project budget.

### 3.7 Flash sectors and protected top region — CONFIRMED

The relevant flash organization used by the current SMK SH68F90A code is:

- 512-byte flash sectors;
- top 4 KiB owned by the bootloader/recovery path;
- application reset-vector redirect stored at `0xEFFC`;
- the sector containing `0xEFFC` begins at `0xEE00`;
- that sector must never be erased;
- the top bootloader region begins at `0xF000`;
- current application code cap is `0xEC00`;
- current simple settings helper uses sector `0xEC00–0xEDFF`.

These ranges are non-negotiable:

```text
0xEE00–0xEFFF  application redirect sector — NEVER ERASE OR PROGRAM
0xF000–0xFFFF  bootloader/recovery-owned region — NEVER ERASE OR PROGRAM
```

The configurator must not provide firmware-update or arbitrary-flash facilities in its first implementation.

---

# Part II — Non-negotiable bootloader and recovery safety

## 4. The bootloader must remain unharmed

This is the highest-priority requirement.

A keymap, RGB setting, animation, malformed HID packet, interrupted save, application bug, or hostile local program must not be able to erase or program:

```text
0xEE00–0xEFFF
0xF000–0xFFFF
```

The configurator protocol must never accept a raw flash address from the host.

### 4.1 Never expose a generic flash-write command

**Do not implement this:**

```c
void protocol_write_flash(uint16_t address, const uint8_t *data, uint16_t len);
```

**Implement object-level operations instead:**

```c
enum config_object {
    CFG_OBJECT_KEYMAP,
    CFG_OBJECT_RGB_CONFIG,
    CFG_OBJECT_RGB_STATIC,
    CFG_OBJECT_ANIMATION_PROGRAM,
    CFG_OBJECT_ANIMATION_PARAMS,
};
```

The firmware translates objects into a fixed, compiled storage layout. The host never selects a physical address.

### 4.2 Make the low-level SSP writer private

The SSP erase/program primitive must remain private to the configuration-storage implementation.

```c
/* config_store.c only */
static bool ssp_erase_sector(uint16_t sector_base);
static bool ssp_program_byte(uint16_t address, uint8_t value);
```

Do not export either function in a public header.

### 4.3 Mandatory address whitelist

Every erase and program call must pass a range check even though callers use fixed slot addresses.

```c
#define CFG_SLOT_A_START 0xDE00u
#define CFG_SLOT_A_END   0xE5FFu
#define CFG_SLOT_B_START 0xE600u
#define CFG_SLOT_B_END   0xEDFFu

static bool config_range_allowed(uint16_t address, uint16_t length)
{
    uint32_t start = address;
    uint32_t end;

    if (length == 0) {
        return false;
    }

    end = start + (uint32_t)length - 1u;

    if (end > 0xFFFFu) {
        return false;
    }

    return (start >= CFG_SLOT_A_START && end <= CFG_SLOT_A_END) ||
           (start >= CFG_SLOT_B_START && end <= CFG_SLOT_B_END);
}
```

Every low-level operation must fail closed:

```c
if (!config_range_allowed(address, 1)) {
    config_fault_set(CONFIG_FAULT_PROTECTED_ADDRESS);
    return false;
}
```

### 4.4 Compile-time assertions

```c
_Static_assert((CFG_SLOT_A_START & 0x01FFu) == 0,
               "slot A must be sector-aligned");
_Static_assert((CFG_SLOT_B_START & 0x01FFu) == 0,
               "slot B must be sector-aligned");
_Static_assert(CFG_SLOT_A_END < CFG_SLOT_B_START,
               "configuration slots overlap");
_Static_assert(CFG_SLOT_B_END < 0xEE00u,
               "configuration overlaps redirect sector");
```

### 4.5 Lower the application code cap before allocating slots

The proposed A/B layout requires changing the linker code cap from:

```text
0xEC00
```

to:

```text
0xDE00
```

Proposed safe map:

```text
0x0000–0xDDFF  application code
0xDE00–0xE5FF  configuration slot A, 2048 bytes
0xE600–0xEDFF  configuration slot B, 2048 bytes
0xEE00–0xEFFF  immutable application redirect sector
0xF000–0xFFFF  immutable bootloader/recovery region
```

The existing code size is far below `0xDE00`, but CI must prove this on every build.

Do not start writing slot A or B until:

1. the linker cap is changed;
2. the generated HEX is checked against `0xDE00`;
3. no firmware section overlaps the slots;
4. tests verify the slot constants;
5. the old `0xEC00` single-record writer is removed, replaced, or explicitly migrated.

### 4.6 Do not leave two independent flash writers

The current simple settings implementation uses `0xEC00`.

That address lies inside proposed slot B. Therefore the implementation must choose one of these:

- migrate old small settings into the new profile record; or
- reserve a different dedicated sector and adjust the full layout.

Do not allow both the legacy helper and the new A/B store to erase the same sector.

### 4.7 Preserve physical recovery independent of keymaps

Recovery entry must not depend on user-remappable keycodes.

Bad:

```c
if (resolved_keycode == KC_ESC && resolved_other == KC_SPACE) {
    enter_isp();
}
```

Good:

```c
if (physical_matrix_position_pressed(ESC_ROW, ESC_COL) &&
    physical_matrix_position_pressed(SPACE_ROW, SPACE_COL)) {
    enter_isp();
}
```

A corrupt keymap must not remove recovery.

### 4.8 No self-flashing in v1

The configurator may upload configuration data only.

It must not:

- upload a firmware image;
- rewrite application code;
- rewrite vectors;
- rewrite the redirect;
- rewrite the bootloader;
- patch arbitrary code;
- expose SSP operations.

Firmware updates continue through the already verified recovery/ISP workflow.

### 4.9 Bootloader jump must be deliberate

A future `ENTER_BOOTLOADER` command may request the existing ISP path, but it must not write flash itself.

Use a two-step guarded command:

```text
ARM_BOOTLOADER
  -> device returns random/session nonce valid briefly

ENTER_BOOTLOADER(nonce, confirmation_magic)
  -> accepted only if nonce matches and no flash transaction is active
```

Recommended additional requirement:

- physical Esc+Space held; or
- explicit confirmation in the native application.

Never enter bootloader from a single accidental packet.

### 4.10 Required CI gates

CI must fail if:

- application HEX contains bytes at or above `0xDE00`;
- slot constants are not sector-aligned;
- a configuration slot reaches `0xEE00`;
- protected-address tests do not run;
- generic host-controlled flash addresses appear in protocol code;
- static XRAM exceeds 3072 bytes;
- the compiled delay ABI test skips;
- recovery image initializes PWM/RGB;
- descriptor validation fails.

---

# Part III — Product architecture

## 5. Application architecture

Recommended project name:

```text
SMK84 Configurator
```

Recommended structure:

```text
rk84-smk-port/
├── configurator/
│   ├── package.json
│   ├── src/
│   │   ├── app/
│   │   ├── components/
│   │   ├── definitions/
│   │   │   └── rk84.json
│   │   ├── generated/
│   │   │   └── keycodes.json
│   │   ├── protocol/
│   │   ├── simulator/
│   │   ├── transports/
│   │   │   ├── mock.ts
│   │   │   ├── webhid.ts
│   │   │   └── native.ts
│   │   └── vm/
│   └── tests/
├── docs/
│   ├── SMK84-CONFIG-PROTOCOL.md
│   └── SMK84-RGB-VM.md
├── src/keyboards/royalkludge-rk84/
│   ├── config_protocol.c
│   ├── config_protocol.h
│   ├── config_store.c
│   ├── config_store.h
│   ├── dynamic_keymap.c
│   ├── dynamic_keymap.h
│   ├── rgb_engine.c
│   ├── rgb_engine.h
│   ├── rgb_vm.c
│   └── rgb_vm.h
└── tests/
    ├── c_harness/
    ├── test_config_protocol.py
    ├── test_config_store.py
    ├── test_dynamic_keymap.py
    └── test_rgb_vm.py
```

### 5.1 Shared UI, multiple transports

```ts
export interface DeviceTransport {
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  transact(request: Uint8Array): Promise<Uint8Array>;
  isConnected(): boolean;
}
```

Implementations:

```ts
export class MockTransport implements DeviceTransport {}
export class WebHIDTransport implements DeviceTransport {}
export class NativeHIDTransport implements DeviceTransport {}
```

The rest of the application must not know which transport is active.

### 5.2 Mock transport is mandatory

The keyboard is currently unavailable.

The entire application should remain usable in mock mode:

- keyboard renderer;
- key selection;
- layer editing;
- keycode validation;
- RGB preview;
- animation graph/script compiler;
- VM simulator;
- profile import/export;
- protocol requests;
- errors and retries;
- fake flash save/failure simulation.

Mock behavior must use the same object sizes, protocol packet format, validation rules, and error codes as firmware.

### 5.3 Browser and native delivery

A WebHID transport is useful but cannot be the only delivery method.

As of this specification date, WebHID remains a limited-availability API. It works in Chromium-derived desktop browsers but is not supported in Firefox or Safari. It also requires a secure context such as HTTPS.

Therefore:

- build a web version for Chromium/WebHID;
- build a native desktop version for Linux, macOS, and Windows;
- share the React/TypeScript UI and protocol code;
- implement native HID access through the desktop backend;
- do not force users to change their normal browser.

A Tauri wrapper is a suitable architecture, but the exact native HID library may be selected during implementation.

---

# Part IV — USB configuration transport

## 6. Do not disturb proven keyboard endpoints

The first implementation should avoid adding a new interrupt endpoint.

Recommended transport:

> A vendor-defined HID Feature Report on the existing extra HID interface, transferred over EP0.

Advantages:

- keyboard EP1 remains unchanged;
- Consumer/System/NKRO EP2 behavior remains unchanged;
- no interrupt OUT endpoint is required;
- the host can use standard HID APIs;
- configuration traffic is naturally request/response;
- existing USB identity may be retained.

### 6.1 Provisional report ID

Current known IDs:

```text
1  System
2  Consumer
5  ISP
6  NKRO
7  Console
```

Proposed:

```text
8  SMK84 configuration
```

### 6.2 Recommended report size

Use a logical 32-byte report:

```text
1 byte report ID
31 bytes protocol payload
```

The control endpoint has an 8-byte maximum packet, so the USB implementation must correctly support a multi-packet HID `SET_REPORT` and `GET_REPORT`.

Do not assume the complete report arrives in one EP0 packet.

A possible descriptor fragment:

```c
#define REPORT_ID_SMK84_CONFIG 8
#define SMK84_CONFIG_REPORT_DATA_SIZE 31

HID_RI_USAGE_PAGE(16, 0xFF60),
HID_RI_USAGE(8, 0x01),
HID_RI_COLLECTION(8, 0x01),
    HID_RI_REPORT_ID(8, REPORT_ID_SMK84_CONFIG),
    HID_RI_USAGE(8, 0x02),
    HID_RI_LOGICAL_MINIMUM(8, 0x00),
    HID_RI_LOGICAL_MAXIMUM(16, 0x00FF),
    HID_RI_REPORT_SIZE(8, 8),
    HID_RI_REPORT_COUNT(8, SMK84_CONFIG_REPORT_DATA_SIZE),
    HID_RI_FEATURE(
        8,
        HID_IOF_DATA |
        HID_IOF_VARIABLE |
        HID_IOF_ABSOLUTE
    ),
HID_RI_END_COLLECTION(0),
```

Verify the exact descriptor macros and total lengths in the compiled descriptor tests.

### 6.3 ISR rule

The USB interrupt handler may:

- receive bytes;
- validate report ID and exact length;
- copy one packet into a bounded mailbox;
- set a pending flag;
- return a previously prepared response.

It must not:

- erase flash;
- program flash;
- parse large objects;
- mutate the active keymap;
- run the animation VM;
- allocate memory;
- wait in long loops;
- enter ISP directly.

Recommended mailbox:

```c
typedef struct {
    uint8_t data[31];
    uint8_t length;
    uint8_t pending;
} config_mailbox_t;

static __xdata volatile config_mailbox_t config_rx;
static __xdata config_mailbox_t config_tx;
```

The main loop processes requests:

```c
void config_protocol_task(void)
{
    if (!config_rx.pending) {
        return;
    }

    /* Copy or claim atomically, then release the ISR mailbox. */
    config_process_request();
}
```

### 6.4 One outstanding transaction in v1

The host must serialize requests.

Firmware may respond `BUSY` when a request is already pending.

This is simpler and safer than a queue on a 4 KiB MCU.

---

## 7. Protocol packet format

The report ID is handled by HID. The logical protocol payload is 31 bytes.

### 7.1 Request

```text
byte 0      command
byte 1      transaction ID
byte 2      flags
byte 3      object ID
byte 4      offset low
byte 5      offset high
byte 6      payload length, 0–24
byte 7–30   payload, maximum 24 bytes
```

### 7.2 Response

```text
byte 0      command echoed with response bit, or exact echoed command
byte 1      transaction ID echoed
byte 2      status
byte 3      object ID echoed
byte 4      offset low
byte 5      offset high
byte 6      response length, 0–24
byte 7–30   response payload
```

Do not use C packed structs directly across USB without explicit byte serialization. MCU endianness and compiler packing must not leak into the protocol.

Helpers:

```c
static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
```

### 7.3 Status codes

```c
typedef enum {
    CFG_STATUS_OK = 0,
    CFG_STATUS_BAD_COMMAND,
    CFG_STATUS_BAD_VERSION,
    CFG_STATUS_BAD_LENGTH,
    CFG_STATUS_BAD_OFFSET,
    CFG_STATUS_BAD_OBJECT,
    CFG_STATUS_BAD_KEYCODE,
    CFG_STATUS_BAD_ANIMATION,
    CFG_STATUS_STACK_OVERFLOW,
    CFG_STATUS_BUDGET_EXCEEDED,
    CFG_STATUS_BUSY,
    CFG_STATUS_KEYS_HELD,
    CFG_STATUS_NOT_STAGED,
    CFG_STATUS_CRC_MISMATCH,
    CFG_STATUS_FLASH_VERIFY_FAILED,
    CFG_STATUS_PROTECTED_ADDRESS,
    CFG_STATUS_NOT_SUPPORTED,
    CFG_STATUS_INTERNAL_ERROR,
} config_status_t;
```

### 7.4 Commands

```c
typedef enum {
    CFG_CMD_GET_PROTOCOL_INFO    = 0x01,
    CFG_CMD_GET_DEVICE_INFO      = 0x02,
    CFG_CMD_GET_CAPABILITIES     = 0x03,
    CFG_CMD_GET_STATUS           = 0x04,

    CFG_CMD_READ_OBJECT          = 0x10,
    CFG_CMD_BEGIN_STAGE          = 0x11,
    CFG_CMD_WRITE_CHUNK          = 0x12,
    CFG_CMD_VALIDATE_STAGE       = 0x13,
    CFG_CMD_APPLY_STAGE          = 0x14,
    CFG_CMD_COMMIT_STAGE         = 0x15,
    CFG_CMD_ABORT_STAGE          = 0x16,
    CFG_CMD_RESET_DEFAULTS       = 0x17,

    CFG_CMD_GET_DIAGNOSTICS      = 0x20,

    CFG_CMD_ARM_BOOTLOADER       = 0x70,
    CFG_CMD_ENTER_BOOTLOADER     = 0x71,
} config_command_t;
```

### 7.5 Object IDs

```c
typedef enum {
    CFG_OBJECT_KEYMAP            = 0x01,
    CFG_OBJECT_RGB_CONFIG        = 0x02,
    CFG_OBJECT_RGB_STATIC        = 0x03,
    CFG_OBJECT_ANIMATION_PROGRAM = 0x04,
    CFG_OBJECT_ANIMATION_PARAMS  = 0x05,

    CFG_OBJECT_DEVICE_INFO       = 0x80,
    CFG_OBJECT_LED_MAP           = 0x81,
    CFG_OBJECT_DIAGNOSTICS       = 0x82,
} config_object_t;
```

### 7.6 Transaction behavior

- Transaction IDs are selected by the host.
- The firmware should cache the most recent completed response.
- If the exact same transaction is repeated, return the cached response instead of repeating a non-idempotent operation.
- A new command with an old transaction ID is an error.
- `COMMIT_STAGE` must be safe against retry.
- Reads may be retried freely.
- The host verifies command, transaction ID, object, offset, and length in every response.

### 7.7 Validation of every packet

Before reading payload:

```c
if (request_length != 31) {
    return CFG_STATUS_BAD_LENGTH;
}

if (payload_length > 24) {
    return CFG_STATUS_BAD_LENGTH;
}

if ((uint32_t)offset + payload_length > object_size) {
    return CFG_STATUS_BAD_OFFSET;
}
```

Never use host-controlled lengths in `memcpy` before bounds checking.

---

# Part V — Dynamic keymap

## 8. Runtime layout

Two layers:

```c
#define RK84_DYNAMIC_LAYERS 2
#define RK84_MATRIX_ROWS    6
#define RK84_MATRIX_COLS    16

static __xdata uint16_t dynamic_keymap
    [RK84_DYNAMIC_LAYERS]
    [RK84_MATRIX_ROWS]
    [RK84_MATRIX_COLS];
```

Cost:

```text
384 bytes
```

Compiled defaults remain in code flash:

```c
extern const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS];
```

### 8.1 Lookup API

Do not let generic matrix code know storage details.

```c
uint16_t dynamic_keymap_get(
    uint8_t layer,
    uint8_t row,
    uint8_t col
);

config_status_t dynamic_keymap_set(
    uint8_t layer,
    uint8_t row,
    uint8_t col,
    uint16_t keycode
);
```

### 8.2 Boot fallback

At startup:

1. load newest valid A/B profile;
2. validate schema and keymap;
3. copy keymap to XRAM;
4. if no valid profile exists, copy compiled defaults;
5. recovery chord remains physical and independent.

Never use an invalid record partially.

### 8.3 Press/release correctness

The current active-key cache must remain.

When a key is pressed:

```c
qcode = dynamic_keymap_resolve(row, col);
active_keycode[row][col] = qcode;
```

When released:

```c
qcode = active_keycode[row][col];
active_keycode[row][col] = KC_NO;
```

This guarantees that editing a map or changing layers while a key is held does not release the wrong keycode.

### 8.4 Do not write keycodes in USB ISR

A 16-bit keycode store may not be atomic on an 8051.

Process `SET` or staged writes in the main loop.

For a single live write, either:

- disable interrupts only for the two-byte store; or
- apply changes at a matrix-frame boundary.

Do not disable interrupts for a whole keymap copy.

### 8.5 Keycode allowlist

The configurator must not expose every QMK code.

Generate a catalogue from the exact pinned SMK `keycodes.h`.

Each item should contain:

```json
{
  "value": 40,
  "name": "KC_ENTER",
  "label": "Enter",
  "category": "basic",
  "supported": true
}
```

Initially support:

- basic keyboard usages;
- modifiers;
- function keys;
- navigation;
- Consumer/media keys implemented by SMK;
- System keys implemented by SMK;
- transparent;
- no-key;
- `MO(1)`;
- RK84 RGB custom controls.

Reject:

- unknown keycodes;
- unsupported QMK-only ranges;
- invalid layer numbers;
- arbitrary custom values.

### 8.6 Locked and hidden positions

For v1:

- preserve the known Fn position at matrix `row 5, col 9`;
- hide electrical positions with no physical key;
- keep phantom positions uneditable;
- reject writes to locked positions in firmware, not only UI.

The Fn key may become movable later only after the layer engine and Fn-first processing are generalized and tested.

### 8.7 Live edit versus persistent save

Expected workflow:

```text
Edit in application
    -> send staged keymap
    -> validate
    -> apply to XRAM
    -> test live
    -> explicit Save to keyboard
```

Do not write flash after every key click.

---

# Part VI — RGB data and renderer

## 9. Persistent RGB objects

### 9.1 Global configuration

Example:

```c
typedef struct {
    uint8_t enabled;
    uint8_t brightness;       /* 0–5 */
    uint8_t effect_id;
    uint8_t speed;
    uint8_t direction;
    uint8_t color1[3];
    uint8_t color2[3];
    uint8_t reactive_decay;
    uint8_t reserved[5];
} rgb_config_v1_t;
```

The exact serialized layout must be explicit and versioned. Do not serialize compiler padding.

### 9.2 Static per-position colors

Current electrical framebuffer size:

```text
3 × 126 = 378 bytes
```

A simple persisted order:

```text
plane 0: 126 bytes
plane 1: 126 bytes
plane 2: 126 bytes
```

This matches the renderer and avoids conversion on the MCU.

The application can show only physical keys while preserving zeros for unpopulated electrical positions.

### 9.3 Physical LED definition

Example keyboard-definition item:

```json
{
  "row": 0,
  "col": 0,
  "x": 0,
  "y": 0,
  "w": 1,
  "h": 1,
  "label": "Esc",
  "led": null
}
```

`led` remains `null` until measured.

After hardware mapping:

```json
{
  "row": 0,
  "col": 0,
  "led": {
    "electricalRow": 0,
    "sourceColumn": 0,
    "componentOrder": ["r", "g", "b"]
  }
}
```

Do not hardcode guessed geometry into the animation VM.

### 9.4 RGB ISR restrictions

The PWM ISR must remain small and deterministic.

It may:

- blank the previous phase;
- scan one matrix column;
- read already-generated framebuffer values;
- write PWM duties;
- enable one sink;
- advance phase counters;
- clear its interrupt flag.

It must not:

- interpret custom bytecode;
- calculate HSV;
- calculate distances;
- parse USB commands;
- write flash;
- allocate memory;
- loop over animation events;
- generate an entire frame.

### 9.5 Suspend behavior

Keep existing behavior:

```text
unconfigured -> RGB sinks off
USB suspend  -> RGB sinks off
resume/configured -> RGB may return
```

PWM0/matrix scanning must continue as required for remote wake.

### 9.6 Maximum brightness invariant

Never generate duty equal to or above 2560.

```c
uint16_t rgb_duty(uint8_t source, uint8_t brightness)
{
    if (brightness > 5) {
        brightness = 5;
    }

    return ((uint16_t)source * brightness) << 1;
}
```

Add tests:

```c
_Static_assert(255u * 5u * 2u < 2560u,
               "RGB maximum duty exceeds PWM period");
```

---

# Part VII — Animation system

## 10. No reflashing for animations

Firmware is flashed once with:

- the configuration protocol;
- RGB renderer;
- built-in effects;
- animation virtual machine;
- validator;
- persistent configuration store.

After that, the VIA-like application uploads animations as data.

Normal workflow:

```text
Create animation
    -> compile to SMK RGB bytecode
    -> upload to preview RAM
    -> validate on keyboard
    -> preview live
    -> Save to keyboard
```

No firmware rebuild or ISP flashing is involved.

---

## 11. Animation categories

### 11.1 Built-in effects

Implement efficient native versions first:

- static color;
- breathing;
- rainbow;
- rainbow wave;
- two-color gradient;
- key-reactive fade;
- ripple;
- heat/fade typing effect.

The application changes parameters through the protocol.

Built-ins provide:

- known-good fallback;
- low CPU usage;
- immediate useful functionality;
- test references for the custom VM.

### 11.2 Custom procedural animations

The application compiles either:

- a visual node graph; or
- a small shader-like script

into safe SMK RGB bytecode.

The bytecode is configuration data, not native 8051 code.

### 11.3 Frame-based animations

Raw frame uploads may be considered later, but they are not the primary format.

One raw full RGB frame:

```text
126 × 3 = 378 bytes
```

Examples:

```text
10 frames  = 3780 bytes
30 frames  = 11340 bytes
100 frames = 37800 bytes
```

Do not store long raw frame sequences in XRAM.

Possible future frame formats:

- palette-indexed frames;
- run-length encoding;
- delta frames;
- sparse LED updates;
- host streaming while connected.

---

## 12. RGB virtual machine

### 12.1 Security model

The VM is a sandbox.

Uploaded programs must not access:

- arbitrary XRAM;
- internal RAM;
- SFRs;
- GPIO;
- PWM registers;
- USB registers;
- flash;
- the stack pointer;
- interrupt flags;
- the bootloader;
- code pointers.

Never upload:

- C code;
- SDCC object files;
- 8051 machine code;
- function pointers;
- native plugins.

### 12.2 v1 program model

Use a straight-line stack VM.

Recommended v1 restrictions:

- no backward jumps;
- no unbounded loops;
- no recursion;
- no function calls;
- bounded program length;
- statically validated stack depth;
- bounded instruction count;
- deterministic output;
- fixed-point/integer math only.

A program is evaluated for each physical/electrical LED position.

### 12.3 Inputs

Suggested VM inputs:

```text
TIME8             wrapping animation time
TIME16            wider animation time
LED_INDEX         logical/electrical LED index
LED_X             normalized x coordinate
LED_Y             normalized y coordinate
LED_ROW           physical/electrical row
LED_COL           source column
KEY_HELD          whether associated key is held
KEY_AGE           time since last press
EVENT_COUNT        number of active reactive events
NEAREST_EVENT_AGE
NEAREST_EVENT_DISTANCE
PARAM_0..PARAM_15 user parameters
```

### 12.4 Operations

Suggested v1 opcodes:

```text
PUSH_U8
PUSH_U16
LOAD_INPUT
LOAD_PARAM

ADD
SUB
MUL8
MUL_Q8_8
ABS
MIN
MAX
CLAMP8

SAW8
TRIANGLE8
SINE8
DISTANCE8
MIX8
FADE8

HSV_TO_RGB
SET_R
SET_G
SET_B
OUTPUT_RGB
END
```

No general memory load/store instructions.

### 12.5 Numeric representation

Use integer/fixed-point math.

Suggested:

- colors and normalized channels: `0..255`;
- coordinates: `0..255`;
- phase: wrapping `uint8_t`;
- longer time: `uint16_t`;
- selected multiplication: Q8.8 fixed point;
- saturating operations where overflow would otherwise produce surprising colors.

Do not use floating point on the MCU.

### 12.6 Program header

```c
typedef struct {
    uint8_t  magic[4];          /* "SRGB" */
    uint8_t  format_version;    /* 1 */
    uint8_t  flags;
    uint16_t bytecode_length;
    uint16_t max_ops_per_led;
    uint8_t  required_stack;
    uint8_t  parameter_count;
    uint32_t bytecode_crc32;
} rgb_program_header_v1_t;
```

Serialize fields manually in little-endian form.

### 12.7 v1 limits

Conservative initial limits:

```text
Maximum bytecode:          512 bytes
Maximum parameters:         16 bytes or 16 scalar values
Maximum stack depth:        16 values
Maximum operations/LED:     24
Maximum active events:       8
Target animation rate:      20 Hz initially
```

These are design limits, not proven performance numbers. Benchmark on hardware before increasing them.

At 126 electrical positions:

```text
126 × 24 × 20 = 60,480 VM operations per second
```

The real cycle cost depends on opcode implementation and must be measured.

If an effect misses its frame deadline:

- keep the last completed framebuffer;
- increment a diagnostic counter;
- optionally lower frame rate;
- never delay the PWM ISR.

### 12.8 Validator

Validation occurs both in the application and firmware.

Firmware validator must verify:

- correct magic;
- supported version;
- bytecode length within limit;
- CRC;
- only known opcodes;
- every immediate operand present;
- stack never underflows;
- stack never exceeds declared or firmware maximum;
- instruction count within budget;
- `OUTPUT_RGB`/`END` semantics valid;
- no invalid input or parameter index;
- no unsupported flags;
- no trailing malformed bytes.

Do not execute before validation succeeds.

### 12.9 Example conceptual programs

Breathing:

```text
LOAD_INPUT TIME8
LOAD_PARAM SPEED
MUL8
SINE8
LOAD_PARAM COLOR_R
MUL8
SET_R
...
OUTPUT_RGB
END
```

Rainbow wave:

```text
LOAD_INPUT TIME8
LOAD_PARAM SPEED
MUL8
LOAD_INPUT LED_X
LOAD_PARAM SPACING
MUL8
ADD
LOAD_PARAM SATURATION
LOAD_PARAM BRIGHTNESS
HSV_TO_RGB
OUTPUT_RGB
END
```

Ripple:

```text
LOAD_INPUT NEAREST_EVENT_DISTANCE
LOAD_INPUT NEAREST_EVENT_AGE
LOAD_PARAM SPEED
MUL8
SUB
ABS
LOAD_PARAM WIDTH
FADE8
LOAD_PARAM COLOR
MIX8
OUTPUT_RGB
END
```

The exact byte encoding must be documented separately and shared by C and TypeScript tests.

---

## 13. Reactive event state

Do not store previous complete frames.

Use a bounded event queue.

```c
#define RGB_EVENT_MAX 8

typedef struct {
    uint8_t  led;
    uint16_t age;
    uint8_t  strength;
    uint8_t  palette_index;
    uint8_t  flags;
} rgb_event_t;
```

Eight six-byte events:

```text
48 bytes
```

On physical key press:

1. translate matrix position to LED index;
2. append/replace an event;
3. animation task ages events;
4. expired events are removed.

If no LED mapping exists for a key, do not generate a bogus event.

---

## 14. Framebuffer strategy

### 14.1 One existing active framebuffer

Current active RGB state:

```text
378 bytes
```

This is enough for static colors and generated animation output.

### 14.2 Do not precompute many complete frames

Never allocate:

```c
uint8_t animation_frames[10][3][126];
```

That would cost:

```text
3780 bytes
```

and nearly exhaust all XRAM.

### 14.3 Preview program buffer

Recommended v1 preview bytecode buffer:

```c
#define RGB_PREVIEW_PROGRAM_MAX 512
static __xdata uint8_t rgb_preview_program[RGB_PREVIEW_PROGRAM_MAX];
```

Projected XDATA:

```text
current static XDATA          1207
dynamic keymap                 384
preview program                512
event/runtime state           ~128
optional row scratch            63
                              ----
approximately                 2294 bytes
```

This remains below the 3072-byte budget.

The actual `.mem` file is the source of truth. Do not rely only on this estimate.

### 14.4 Tearing options

A single framebuffer may briefly contain parts of two generated frames while the main loop updates it.

Initial options:

1. accept at most one-refresh tearing and measure visibility;
2. generate one row into a 63-byte scratch buffer and copy rows at controlled times;
3. add a second full 378-byte framebuffer if XRAM remains comfortably below budget.

Do not add triple buffering.

Do not calculate animation pixels inside the PWM ISR merely to avoid tearing.

---

# Part VIII — Persistent configuration store

## 15. A/B slots

Proposed slots:

```text
slot A: 0xDE00–0xE5FF, 2048 bytes
slot B: 0xE600–0xEDFF, 2048 bytes
```

Each slot contains one complete active profile.

Approximate v1 payload:

```text
dynamic keymap             384 bytes
static RGB data            378 bytes
global RGB config           32 bytes
animation program          512 bytes
animation parameters        64 bytes
header/section metadata     ~64 bytes
                           ----
approximately             1434 bytes
```

This fits within 2048 bytes.

The computer may store unlimited profiles. The first keyboard implementation stores one active profile redundantly in A/B form.

### 15.1 Slot header

Example:

```c
typedef struct {
    uint8_t  magic[4];          /* "S84C" */
    uint16_t schema_version;
    uint16_t header_length;
    uint16_t payload_length;
    uint16_t flags;
    uint32_t sequence;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint8_t  reserved[8];
} config_slot_header_v1_t;
```

Do not trust compiler packing. Serialize manually.

### 15.2 Commit marker

Reserve one byte at the end of the slot:

```text
slot_end = 0xA5 only after complete verification
```

Load a slot only when:

- magic matches;
- schema supported;
- lengths valid;
- commit marker present;
- header CRC valid;
- payload CRC valid;
- every section validates.

### 15.3 Save algorithm

1. Reject or defer save while another transaction is active.
2. Prefer requiring all keys released.
3. Blank RGB sinks.
4. Select the older or invalid slot.
5. Never erase the currently newest valid slot.
6. Erase every sector in the inactive slot.
7. Program header and payload.
8. Read back every programmed byte or verify CRC from flash.
9. Program commit marker last.
10. Re-read and validate the complete new slot.
11. Mark it active in RAM.
12. Resume RGB and force USB report resynchronization if needed.
13. Retain the old slot until the next successful save.

Power failure at any point before step 9 leaves the previous slot valid.

### 15.4 Save while keys are held

Recommended v1 behavior:

```text
COMMIT_STAGE -> CFG_STATUS_KEYS_HELD
```

The application tells the user to release all keys and retries.

This avoids lost transitions while flash operations temporarily inhibit interrupts.

### 15.5 Wear

Do not save automatically after every edit.

Operations:

```text
Preview/apply -> RAM only
Save          -> flash
```

Skip a write when the newly serialized profile is byte-identical to the active stored profile.

Expose save count/sequence diagnostics.

---

# Part IX — Configurator behavior

## 16. Keymap page

Features:

- physical RK84 layout;
- Layer 0 and Layer 1 tabs;
- click key to select;
- searchable keycode list;
- categories;
- locked Fn shown distinctly;
- hidden phantom positions;
- undo/redo in the application;
- Apply, Revert, Save;
- unsaved and unapplied state indicators;
- import/export profile.

The keyboard does not need on-device undo history.

## 17. RGB page

Before hardware mapping is complete:

- mock preview;
- global color;
- brightness;
- effect;
- speed;
- direction;
- raw electrical diagnostic mode.

After mapping is confirmed:

- per-key painting;
- selection;
- row fill;
- gradient tools;
- palette;
- reactive preview;
- LED diagnostics.

Do not present guessed per-key positions as accurate.

## 18. Animation editor

### 18.1 Visual mode

Nodes may include:

```text
Time
LED X/Y
Key Held
Key Age
Nearest Event Distance
Nearest Event Age
Constant
Parameter
Add/Subtract/Multiply
Sine/Triangle/Saw
Clamp
HSV
Mix
Output
```

### 18.2 Script mode

Example source language:

```c
rgb effect(Led led, Time time, Params p)
{
    u8 hue = time.phase * p.speed + led.x * p.spacing;
    return hsv(hue, p.saturation, p.brightness);
}
```

This language is compiled on the computer into safe VM bytecode.

It is not sent as source and is never compiled on the MCU.

### 18.3 Shared simulator

Implement the VM twice:

- C firmware interpreter;
- TypeScript simulator.

Use identical golden test vectors:

```json
{
  "program": "...",
  "inputs": {
    "time": 17,
    "ledX": 64,
    "ledY": 128
  },
  "expected": [12, 200, 44]
}
```

A mismatch fails CI.

### 18.4 Preview and save

Preview:

```text
BEGIN_STAGE ANIMATION_PROGRAM
WRITE_CHUNK...
VALIDATE_STAGE
APPLY_STAGE
```

This loads the program into XRAM and causes no flash write.

Save:

```text
COMMIT_STAGE
```

This atomically writes the complete profile to the inactive slot.

---

# Part X — Profile and definition formats

## 19. Keyboard definition

Example:

```json
{
  "format": "smk-keyboard-definition",
  "version": 1,
  "id": "royalkludge-rk84-smk",
  "name": "Royal Kludge RK84",
  "usb": {
    "vendorId": 9610,
    "productId": 89,
    "configReportId": 8,
    "usagePage": 65376,
    "usage": 1
  },
  "matrix": {
    "rows": 6,
    "cols": 16,
    "layers": 2
  },
  "rgb": {
    "electricalRows": 6,
    "sourceColumns": 21,
    "positions": 126,
    "mappingStatus": "unverified"
  },
  "capabilities": {
    "dynamicKeymap": true,
    "staticRgb": true,
    "builtInAnimations": true,
    "customAnimationVm": true,
    "macros": false,
    "wirelessConfiguration": false
  },
  "lockedKeys": [
    {
      "row": 5,
      "col": 9,
      "reason": "Fn key fixed in protocol v1"
    }
  ],
  "layout": []
}
```

Do not rely only on VID/PID to identify protocol compatibility. Query protocol and board ID after connection.

## 20. User profile

Example:

```json
{
  "format": "smk84-profile",
  "version": 1,
  "keyboard": "royalkludge-rk84-smk",
  "firmwareProtocol": {
    "major": 1,
    "minor": 0
  },
  "keymap": {
    "layers": []
  },
  "rgb": {
    "enabled": true,
    "brightness": 4,
    "effect": "custom",
    "static": [],
    "animation": {
      "format": "smk-rgb-vm",
      "version": 1,
      "name": "Blue Ripple",
      "bytecodeBase64": "",
      "parameters": []
    }
  }
}
```

Import validation must reject:

- wrong keyboard;
- unsupported schema;
- invalid keycodes;
- invalid array lengths;
- bytecode over limit;
- invalid CRC;
- values outside brightness/effect ranges.

---

# Part XI — Diagnostics

## 21. Device information

`GET_DEVICE_INFO` should return:

- board ID;
- firmware version;
- Git commit prefix if available;
- protocol major/minor;
- matrix rows/columns;
- layer count;
- RGB electrical geometry;
- maximum animation size;
- maximum parameters;
- maximum events;
- flash schema;
- active slot;
- active sequence;
- XRAM budget/build measurement where practical;
- capability bits.

## 22. Runtime counters

Expose read-only diagnostics:

- malformed packet count;
- unknown command count;
- transaction retry count;
- flash verification failures;
- protected-address attempts;
- invalid profile boots;
- fallback-to-default count;
- VM validation failures;
- VM deadline misses;
- VM instruction-budget aborts;
- event queue overflow count;
- RGB frame count;
- USB reset/suspend/resume counts.

Do not expose arbitrary memory.

---

# Part XII — Testing requirements

## 23. Protocol tests

Test:

- all valid commands;
- every invalid command value;
- all object IDs;
- zero lengths;
- maximum lengths;
- offset overflow;
- `offset + length` integer overflow;
- truncated packets;
- extra packets;
- duplicate transaction IDs;
- retries of `COMMIT_STAGE`;
- command during busy state;
- command during suspend;
- malformed bootloader requests.

Fuzz random 31-byte packets through a host-compiled C harness.

Expected property:

> No packet may write outside its object, write flash unexpectedly, hang the main loop, or reach protected flash.

## 24. Flash model tests

Simulate power loss after every programmed byte and sector erase.

For every interruption point, next boot must produce one of:

- previous valid profile;
- new complete valid profile;
- compiled defaults if neither slot has ever been valid.

It must never load a partial mixture.

Test sequence-number wraparound explicitly.

## 25. Protected-region tests

Unit tests must call the internal range checker with:

```text
0x0000
0xDDFF
0xDE00
0xE5FF
0xE600
0xEDFF
0xEE00
0xEFFC
0xF000
0xFFFF
```

Verify only slot A/B ranges pass.

Add tests for:

- zero length;
- 16-bit wrap;
- range starting valid but ending protected;
- erase address not sector-aligned;
- slot overlap.

## 26. Dynamic keymap tests

Test:

- default load;
- valid stored load;
- invalid CRC fallback;
- every row/column/layer;
- locked-position rejection;
- unsupported-keycode rejection;
- held key then live remap then release;
- held Fn chord then layer edit then release;
- simultaneous Fn and ordinary key;
- reset to defaults;
- apply without save;
- save then reboot.

## 27. RGB/VM tests

Test:

- validator stack analysis;
- all opcodes;
- immediate truncation;
- unknown opcode;
- operation limit;
- parameter bounds;
- every LED coordinate;
- maximum brightness duty;
- event queue behavior;
- deterministic random input;
- timeout/deadline behavior;
- TypeScript/C golden-vector equivalence;
- static, breathing, rainbow, reactive fade, and ripple references.

## 28. USB regression tests

Configurator support must not change:

- EP1 keyboard report size;
- EP2 report behavior;
- Consumer/System reports;
- NKRO descriptor;
- lock LED handling;
- configuration gate;
- suspend blanking;
- remote wake;
- reset/config resync;
- ISP feature report;
- recovery build PWM invariant.

## 29. Hardware acceptance later

When a keyboard is available:

1. confirm recovery before any configurator build;
2. flash without touching bootloader or redirect;
3. enumerate and type;
4. connect through native HID utility;
5. connect through WebHID in a supported browser;
6. read device information;
7. apply a harmless keymap edit;
8. verify held-key release correctness;
9. unplug before Save and confirm edit is lost;
10. Save, unplug, and confirm persistence;
11. interrupt a save and confirm old profile survives;
12. test reset to defaults;
13. validate RGB map;
14. preview built-in effects;
15. preview custom bytecode;
16. save custom animation;
17. suspend/resume;
18. enter recovery deliberately;
19. restore stock firmware through existing recovery workflow;
20. verify bootloader and redirect remain intact.

Do not call the configurator hardware-complete until these pass.

---

# Part XIII — Implementation order

## 30. Recommended milestones

### Milestone 0 — freeze specifications

Create and review:

- protocol specification;
- flash map;
- profile schema;
- keyboard definition;
- VM instruction set;
- bootloader safety invariants.

### Milestone 1 — application with mock device

Implement:

- keyboard renderer;
- keymap editor;
- profile import/export;
- RGB simulator;
- mock transport;
- protocol encoder/decoder.

No firmware change required.

### Milestone 2 — dynamic keymap runtime

Implement:

- one 384-byte runtime map;
- compiled fallback;
- validation;
- active-key cache integration;
- host C tests.

No flash writes yet.

### Milestone 3 — HID protocol

Implement:

- report descriptor;
- multi-packet Feature Report;
- ISR mailbox;
- main-loop parser;
- read-only commands first;
- mock/firmware protocol parity.

### Milestone 4 — RAM-only apply

Implement:

- staged keymap writes;
- apply/revert;
- RGB parameter preview;
- no persistence.

### Milestone 5 — A/B persistent storage

First:

- lower code cap to `0xDE00`;
- add protected-region tests;
- remove/replace legacy `0xEC00` writer.

Then implement:

- slot serialization;
- CRC;
- commit marker;
- power-loss model;
- explicit Save.

### Milestone 6 — built-in RGB effects

Implement:

- static;
- breathing;
- rainbow;
- wave;
- reactive fade;
- ripple;
- app controls;
- mock and C models.

### Milestone 7 — custom animation VM

Implement:

- validator;
- interpreter;
- 512-byte preview buffer;
- TypeScript simulator;
- visual/script compiler;
- preview;
- persistent save.

### Milestone 8 — physical validation

Do not combine initial protocol, flash store, and VM hardware testing into one first flash. Validate incrementally.

---

# Part XIV — Do and do not

## 31. Do

- Preserve recovery at every stage.
- Treat `0xEE00–0xFFFF` as immutable.
- Use fixed object IDs, not raw addresses.
- Keep SSP functions private.
- Validate every length and offset.
- Perform flash operations only in the main loop.
- Keep the PWM ISR deterministic.
- Keep compiled keymap and animation fallbacks.
- Use one active keymap and one active framebuffer.
- Store undo history and unlimited profiles on the computer.
- Make preview RAM-only.
- Make Save explicit.
- Use A/B atomic records.
- Verify flash after writing.
- Program the commit marker last.
- Require key release before Save in v1.
- Generate keycode metadata from pinned SMK headers.
- Keep mock transport first-class.
- Implement both WebHID and native HID transports.
- Differential-test the TypeScript and C animation VMs.
- Continue enforcing the 3072-byte XRAM budget.
- Inspect the `.mem`, `.map`, HEX, and compiled assembly after major changes.
- Label every hardware assumption clearly.

## 32. Do not

- Do not clone the entire VIA application.
- Do not claim VIA protocol compatibility unless actually implemented and tested.
- Do not expose arbitrary memory access.
- Do not expose arbitrary flash addresses.
- Do not implement self-flashing in v1.
- Do not erase `0xEE00–0xEFFF`.
- Do not program `0xEFFC`.
- Do not erase or program `0xF000–0xFFFF`.
- Do not make recovery depend on remappable keycodes.
- Do not perform flash writes from USB ISR.
- Do not run bytecode in PWM ISR.
- Do not upload native 8051 code.
- Do not permit unbounded VM loops.
- Do not trust app-side validation alone.
- Do not automatically save every edit.
- Do not store many raw animation frames in XRAM.
- Do not duplicate the complete keymap for undo.
- Do not duplicate RGB state unless a measured need justifies double buffering.
- Do not guess the physical LED map.
- Do not assume every one of 126 electrical positions is populated.
- Do not remove existing USB suspend/config gates.
- Do not increase maximum brightness beyond the proven duty invariant.
- Do not call the system hardware-complete while no keyboard is available.

---

# Part XV — Definition of success

The project is successful when:

1. configurator-enabled SMK firmware is flashed once through the safe existing process;
2. the bootloader and redirect sector remain untouched;
3. the keyboard remains a fully working wired USB keyboard;
4. the app can read and edit two complete keymap layers;
5. edits can be previewed without flash writes;
6. explicit Save survives unplugging;
7. interrupted Save falls back safely;
8. RGB can be configured through the app;
9. built-in animations need no reflashing;
10. custom animations can be created, validated, previewed, uploaded, and saved without reflashing;
11. invalid packets and invalid bytecode cannot escape their sandbox;
12. Firefox/macOS/Linux/Windows users have a native utility path;
13. recovery remains available even with corrupt user configuration;
14. CI proves code, XRAM, flash boundaries, protocol validation, VM behavior, and recovery invariants.

The intended user experience is:

```text
Flash SMK configurator firmware once
    -> use SMK84 Configurator for all ordinary customization
    -> never reflash for keymaps, colors, effects, or custom animations
```

---

# References

Repository baseline:

- https://github.com/zgredex/rk84-smk-port/commit/8a599d44e38c61e7506b1c18fe1fd55f158eb943

VIA application:

- https://github.com/the-via/app
- https://github.com/the-via/keyboards

VIA command implementation studied for architectural reference:

- https://github.com/the-via/app/blob/main/src/utils/keyboard-api.ts

WebHID availability and security-context requirements:

- https://developer.mozilla.org/en-US/docs/Web/API/WebHID_API
- https://developer.mozilla.org/en-US/docs/Web/API/Navigator/hid

---

## Final instruction to the implementing AI

Before writing code, repeat these invariants in the implementation plan:

```text
NEVER ERASE OR PROGRAM 0xEE00–0xEFFF.
NEVER ERASE OR PROGRAM 0xF000–0xFFFF.
NEVER ACCEPT A RAW FLASH ADDRESS FROM THE HOST.
NEVER WRITE FLASH FROM AN INTERRUPT.
NEVER RUN CUSTOM ANIMATION BYTECODE IN THE PWM ISR.
ALWAYS KEEP A COMPILED DEFAULT KEYMAP AND RGB FALLBACK.
ALWAYS KEEP PHYSICAL RECOVERY INDEPENDENT OF USER CONFIGURATION.
```

If an implementation choice conflicts with one of those statements, the implementation choice is wrong.
