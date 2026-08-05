/**
 * SMK84 configuration protocol — wire constants.
 *
 * Mirrors spec §7 (SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md).
 * MUST stay byte-identical with the firmware implementation
 * (src/keyboards/royalkludge-rk84/config_protocol.h).
 */

/** Report ID 8, vendor usage page 0xFF60, extra HID interface via EP0. */
export const REPORT_ID_SMK84_CONFIG = 8;

/** Protocol payload size after the report ID byte (31 bytes). */
export const CONFIG_REPORT_DATA_SIZE = 31;

/** Maximum payload bytes inside a request/response (bytes 7..30). */
export const CONFIG_PAYLOAD_MAX = 24;

/** Protocol version (major.minor packed: major << 8 | minor). */
export const PROTOCOL_VERSION = 0x0100;

/** Feature report used for the transport (vendor-defined). */
export const FEATURE_REPORT_ID = REPORT_ID_SMK84_CONFIG;

/** Command codes — spec §7.4. */
export enum Cmd {
  GET_PROTOCOL_INFO = 0x01,
  GET_DEVICE_INFO = 0x02,
  GET_CAPABILITIES = 0x03,
  GET_STATUS = 0x04,

  READ_OBJECT = 0x10,
  BEGIN_STAGE = 0x11,
  WRITE_CHUNK = 0x12,
  VALIDATE_STAGE = 0x13,
  APPLY_STAGE = 0x14,
  COMMIT_STAGE = 0x15,
  ABORT_STAGE = 0x16,
  RESET_DEFAULTS = 0x17,

  GET_DIAGNOSTICS = 0x20,

  ARM_BOOTLOADER = 0x70,
  ENTER_BOOTLOADER = 0x71,
}

/** Response bit — command echoed with this bit set. */
export const CMD_RESPONSE_BIT = 0x80;

/** Status codes — spec §7.3. */
export enum Status {
  OK = 0,
  BAD_COMMAND,
  BAD_VERSION,
  BAD_LENGTH,
  BAD_OFFSET,
  BAD_OBJECT,
  BAD_KEYCODE,
  BAD_ANIMATION,
  STACK_OVERFLOW,
  BUDGET_EXCEEDED,
  BUSY,
  KEYS_HELD,
  NOT_STAGED,
  CRC_MISMATCH,
  FLASH_VERIFY_FAILED,
  PROTECTED_ADDRESS,
  NOT_SUPPORTED,
  INTERNAL_ERROR,
}

/** Object IDs — spec §7.5. */
export enum ObjectId {
  KEYMAP = 0x01,
  RGB_CONFIG = 0x02,
  RGB_STATIC = 0x03,
  ANIMATION_PROGRAM = 0x04,
  ANIMATION_PARAMS = 0x05,

  DEVICE_INFO = 0x80,
  LED_MAP = 0x81,
  DIAGNOSTICS = 0x82,
}

/** Request flags (byte 2). */
export enum Flags {
  NONE = 0x00,
  /** Host wants the cached response for a repeated transaction. */
  CACHE_OK = 0x01,
  /** Request originates from a preview (RAM-only) session. */
  PREVIEW = 0x02,
}

/**
 * Object sizes (bytes). Firmware enforces these; the host uses them to
 * bound offsets before sending. Kept here and in config_protocol.h.
 */
export const OBJECT_SIZES: Readonly<Record<number, number>> = {
  [ObjectId.KEYMAP]: 2 * 6 * 16 * 2, // 384: layers x rows x cols x u16
  [ObjectId.RGB_CONFIG]: 32,
  [ObjectId.RGB_STATIC]: 3 * 126, // 378
  [ObjectId.ANIMATION_PROGRAM]: 512,
  [ObjectId.ANIMATION_PARAMS]: 64,
};

/** Response status name helper (for UI/logging). */
export function statusName(s: Status): string {
  return Status[s] ?? `UNKNOWN(${s})`;
}

/** Command name helper. */
export function cmdName(c: Cmd): string {
  return Cmd[c] ?? `CMD_UNKNOWN(${c})`;
}

/**
 * RK84 custom keycodes (mirrors kbdef.h: SAFE_RANGE = 0x5200 + enum).
 * Used by the mock's keymap validation (R1).
 */
export const SAFE_RANGE = 0x5200;
export const RGB_BRI_UP = SAFE_RANGE;
export const RGB_BRI_DN = SAFE_RANGE + 1;
