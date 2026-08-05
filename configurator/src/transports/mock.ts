/**
 * MockTransport — in-browser/memory device used for tests and for
 * running the whole app without hardware (spec §5.2).
 *
 * It implements the same packet semantics as the firmware:
 *  - validates length/offset before any access;
 *  - echoes commands with the response bit;
 *  - maintains the object store with identical sizes;
 *  - enforces one-outstanding-transaction (BUSY);
 *  - simulates flash save failure and key-held conditions.
 *
 * The object sizes MUST match config_protocol.h and OBJECT_SIZES.
 */

import {
  Cmd,
  Flags,
  MO_1,
  OBJECT_SIZES,
  ObjectId,
  RGB_BRI_DN,
  RGB_BRI_UP,
  Status,
} from "../protocol/constants.js";
import {
  decodeRequest,
  encodeResponse,
  unwrapReport,
  withResponseBit,
  wrapReport,
  type RequestPacket,
  type ResponsePacket,
} from "../protocol/packet.js";
import { RK84_DEFAULT_KEYMAP } from "../generated/rk84-default-keymap.js";
import type { DeviceTransport } from "./transport.js";

/** Error modes for save/commit fault simulation. */
export type MockFault =
  | "none"
  | "flash-verify-fail"
  | "keys-held"
  | "protected-address"
  | "busy";

/** N2 (audit): strict M3 firmware behavior by default; persistence
 * simulation only behind the explicit "future-storage" mode. */
export type MockMode = "m3" | "future-storage";

/** Encode a u16 keymap into the 384-byte LE byte array (Q1). */
function encodeKeymap(values: Uint16Array): Uint8Array {
  const bytes = new Uint8Array(values.length * 2);
  for (let i = 0; i < values.length; i++) {
    bytes[i * 2] = values[i] & 0xff;
    bytes[i * 2 + 1] = values[i] >>> 8;
  }
  return bytes;
}

export class MockTransport implements DeviceTransport {
  /** Object store: objectId -> Uint8Array (size from OBJECT_SIZES). */
  private store = new Map<number, Uint8Array>();
  private connected = false;
  private busy = false;
  /** M3-04/R1 (audit): real staging state — BEGIN snapshots live into
   * a SINGLE staged object, WRITE mutates it only, VALIDATE checks the
   * keycodes, APPLY copies to live, ABORT discards. Mirrors firmware
   * config_protocol.c. Exactly one object can be staged at a time. */
  private stageObject: number | null = null;
  private stagedData: Uint8Array | null = null;
  private stageValidated = false;
  /** Exact-request cache (audit F6/M3-04): the complete request is
   * compared, not just the transaction ID. */
  private lastRequest: Uint8Array | null = null;
  private lastResponse: Uint8Array | null = null;

  /** Programmable fault for commit/apply testing. */
  fault: MockFault = "none";

  /** N2: strict M3 behavior (COMMIT -> NOT_SUPPORTED, keymap-only
   * objects) unless explicitly set to the future-storage mode.
   * Public so tests can toggle it. */
  mode: MockMode = "m3";
  /** Compiled keymap defaults (N2): the immutable baseline that
   * RESET_DEFAULTS restores and locked-Fn validation compares against. */
  private readonly compiledDefaults: Uint8Array;

  /** Simulation controls. */
  simulateKeysHeld = false;
  /** If set, BEGIN_STAGE/WRITE_CHUNK reject with BAD_OBJECT. */
  rejectUnknownObjects = true;

  constructor(compiledDefaults?: Uint8Array) {
    for (const [id, size] of Object.entries(OBJECT_SIZES)) {
      this.store.set(Number(id), new Uint8Array(size));
    }
    // device info is a read-only object
    this.store.set(0x80, new Uint8Array(64));
    this.store.set(0x81, new Uint8Array(126)); // led map placeholder
    this.store.set(0x82, new Uint8Array(64)); // diagnostics
    // Q1/S2 (audit): the mock's compiled defaults MUST be the REAL RK84
    // layout (generated from layout.c via keymap_fixture.py), not a
    // synthetic blank map. The protocol object is exactly 384 bytes —
    // validate the SELECTED source uniformly (caller-provided AND the
    // generated built-in), so a malformed generated constant can never
    // silently replace the KEYMAP store with a differently sized object.
    const expected = OBJECT_SIZES[ObjectId.KEYMAP];
    const source = compiledDefaults ?? encodeKeymap(RK84_DEFAULT_KEYMAP);
    if (source.length !== expected) {
      throw new RangeError(
        `compiled keymap has ${source.length} bytes; ` +
        `expected ${expected}`,
      );
    }
    // P4b: clone the selected defaults — the "immutable" baseline must
    // not be externally mutable.
    this.compiledDefaults = new Uint8Array(source);
    this.store.set(ObjectId.KEYMAP, new Uint8Array(this.compiledDefaults));
  }

  async connect(): Promise<void> {
    this.connected = true;
  }

  async disconnect(): Promise<void> {
    this.connected = false;
  }

  isConnected(): boolean {
    return this.connected;
  }

  /** P4a (audit): firmware staging status precedence — no stage is
   * NOT_STAGED; an active stage for a DIFFERENT object is BAD_OBJECT;
   * a matching stage is OK. Mirrors config_protocol.c exactly. */
  private stageStateFor(objectId: number): Status {
    if (this.stageObject === null || this.stagedData === null) {
      return Status.NOT_STAGED;
    }
    if (this.stageObject !== objectId) {
      return Status.BAD_OBJECT;
    }
    return Status.OK;
  }

  /** Direct object access for tests (reads a COPY of the LIVE store —
   * P4b: never expose the internal mutable array). */
  getObject(id: number): Uint8Array {
    const o = this.store.get(id);
    if (!o) throw new Error(`no object ${id}`);
    return new Uint8Array(o);
  }

  /** P4b (audit): immutable compiled default for a cell — the locked
   * Fn validation compares against THIS, never the mutable live map. */
  private compiledKeycode(cell: number): number {
    return (
      this.compiledDefaults[cell * 2] |
      (this.compiledDefaults[cell * 2 + 1] << 8)
    );
  }

  /** R1/N1 (audit): firmware-equivalent keymap validation — same SMK
   * predicate ranges the firmware allowlist uses. N1: SAFE_RANGE =
   * QK_USER = 0x7E40 (NOT 0x5200 = QK_TO); only MO(1) = 0x5221 is
   * allowed, not the whole momentary range. */
  private validateKeymap(raw: Uint8Array): Status {
    const size = 2 * 6 * 16 * 2;
    if (raw.length !== size) return Status.BAD_LENGTH;

    const isBasic = (c: number) => c >= 0x04 && c <= 0xa4;
    const isSystem = (c: number) => c >= 0xa5 && c <= 0xa7;
    const isConsumer = (c: number) => c >= 0xa8 && c <= 0xc2;
    const isModifier = (c: number) => c >= 0xe0 && c <= 0xe7;
    const allowed = (c: number) =>
      c === 0x00 /* KC_NO */ ||
      c === 0x01 /* KC_TRANSPARENT */ ||
      isBasic(c) || isSystem(c) || isConsumer(c) || isModifier(c) ||
      c === MO_1 ||
      c === RGB_BRI_UP || c === RGB_BRI_DN;

    for (let cell = 0; cell < size / 2; cell++) {
      const code = raw[cell * 2] | (raw[cell * 2 + 1] << 8);
      const pos = cell % (6 * 16);
      const row = Math.floor(pos / 16);
      const col = pos % 16;
      if (row === 5 && col === 9) {
        // P4b: locked Fn must keep its COMPILED default (immutable) —
        // never compares against the mutable live map.
        if (code !== this.compiledKeycode(cell)) return Status.BAD_KEYCODE;
        continue;
      }
      if (!allowed(code)) return Status.BAD_KEYCODE;
    }
    return Status.OK;
  }

  async transact(report: Uint8Array): Promise<Uint8Array> {
    if (!this.connected) throw new Error("not connected");
    const payload = unwrapReport(report);
    const req: RequestPacket = decodeRequest(payload);

    // one outstanding transaction
    if (this.busy) {
      return this.reply(req, Status.BUSY);
    }

    // P3/N5 (audit, spec §7.6): three-state cache — identical full
    // request replays; a reused transaction ID with DIFFERENT contents
    // is a protocol error (BAD_COMMAND); different txid is fresh.
    if (
      this.lastResponse !== null &&
      this.lastRequest !== null
    ) {
      const identical =
        this.lastRequest.length === payload.length &&
        this.lastRequest.every((b, i) => b === payload[i]);
      if (identical) {
        return this.lastResponse;
      }
      // protocol payload: byte 0 = command, byte 1 = transaction ID
      const sameTxid = this.lastRequest[1] === payload[1];
      if (sameTxid) {
        const collision = this.reply(req, Status.BAD_COMMAND);
        this.lastRequest = new Uint8Array(payload);
        this.lastResponse = collision;
        return collision;
      }
    }

    this.busy = true;
    try {
      const resp = this.handle(req);
      const out = encodeResponse(resp);
      this.lastResponse = wrapReport(out);
      this.lastRequest = new Uint8Array(payload);
      return this.lastResponse!;
    } finally {
      this.busy = false;
    }
  }

  private reply(req: RequestPacket, status: Status): Uint8Array {
    const resp: ResponsePacket = {
      command: withResponseBit(req.command),
      transactionId: req.transactionId,
      status,
      objectId: req.objectId,
      offset: req.offset,
      payload: new Uint8Array(0),
    };
    return wrapReport(encodeResponse(resp));
  }

  private handle(req: RequestPacket): ResponsePacket {
    const base = {
      // R3: the response MUST carry the response bit, like firmware.
      command: withResponseBit(req.command),
      transactionId: req.transactionId,
      objectId: req.objectId,
      offset: req.offset,
    };
    const ok = (payload: Uint8Array = new Uint8Array(0)): ResponsePacket =>
      ({ ...base, status: Status.OK, payload });

    const fail = (status: Status): ResponsePacket =>
      ({ ...base, status, payload: new Uint8Array(0) });

    switch (req.command) {
      case Cmd.GET_PROTOCOL_INFO: {
        return ok(new Uint8Array([0x01, 0x00])); // major 1, minor 0
      }

      case Cmd.GET_DEVICE_INFO: {
        // Fixed 24-byte record (wire payload max). Layout:
        //   0..11  board id (12 chars, NUL-padded)
        //   12     protocol major
        //   13     protocol minor
        //   14     matrix rows
        //   15     matrix cols
        //   16     layers
        //   17     rgb electrical rows
        //   18     rgb source cols
        //   19     rgb positions (126)
        //   20..21 max animation bytes (u16 LE)
        //   22..23 capability bits (u16 LE)
        const d = new Uint8Array(24);
        const name = "rk84-smk";
        for (let i = 0; i < name.length && i < 12; i++) d[i] = name.charCodeAt(i);
        d[12] = 0x01; // protocol major
        d[13] = 0x00; // protocol minor
        d[14] = 6; // matrix rows
        d[15] = 16; // matrix cols
        d[16] = 2; // layers
        d[17] = 6; // rgb electrical rows
        d[18] = 21; // rgb source cols
        d[19] = 126; // rgb positions
        d[20] = 0x00; // max animation bytes low (512)
        d[21] = 0x02;
        // R7: capability bits from the ONE shared source (must match
        // GET_CAPABILITIES).
        d[22] = 0x01; // dynamicKeymap only
        d[23] = 0x00;
        return ok(d);
      }

      case Cmd.GET_CAPABILITIES: {
        // M3-07 (audit): advertise ONLY what is implemented. M3 =
        // dynamic keymap + RAM staging. RGB/VM/macros/wireless are
        // future milestones and must NOT be advertised.
        const c = new Uint8Array(4);
        c[0] = 0x01; // dynamicKeymap only
        return ok(c);
      }

      case Cmd.GET_STATUS:
        return ok(new Uint8Array([this.fault === "none" ? 0 : 1]));

      case Cmd.READ_OBJECT: {
        // N2: M3 firmware supports ONLY the KEYMAP object.
        if (req.objectId !== ObjectId.KEYMAP) return fail(Status.BAD_OBJECT);
        const obj = this.store.get(req.objectId);
        if (!obj) return fail(Status.BAD_OBJECT);
        const length = req.payload.length || 24;
        if (req.offset + length > obj.length) {
          return fail(Status.BAD_OFFSET);
        }
        const start = req.offset;
        const end = Math.min(obj.length, start + length);
        return ok(obj.slice(start, end));
      }

      case Cmd.BEGIN_STAGE: {
        // R1: firmware accepts ONLY CFG_OBJECT_KEYMAP for staging.
        if (req.objectId !== ObjectId.KEYMAP) return fail(Status.BAD_OBJECT);
        const live = this.store.get(req.objectId);
        if (!live) return fail(Status.BAD_OBJECT);
        // snapshot the LIVE object into the single staged buffer
        this.stageObject = req.objectId;
        this.stagedData = new Uint8Array(live);
        this.stageValidated = false;
        return ok();
      }

      case Cmd.WRITE_CHUNK: {
        // P4a: keymap-only object gate (firmware parity), then staging
        // status precedence (NOT_STAGED vs BAD_OBJECT).
        if (req.objectId !== ObjectId.KEYMAP) return fail(Status.BAD_OBJECT);
        const stageStatus = this.stageStateFor(req.objectId);
        if (stageStatus !== Status.OK) return fail(stageStatus);
        if (req.offset + req.payload.length > this.stagedData!.length) {
          return fail(Status.BAD_OFFSET);
        }
        this.stagedData!.set(req.payload, req.offset);
        this.stageValidated = false;
        return ok();
      }

      case Cmd.VALIDATE_STAGE: {
        // P4a: firmware precedence, then real keymap validation.
        const stageStatus = this.stageStateFor(req.objectId);
        if (stageStatus !== Status.OK) return fail(stageStatus);
        const status = this.validateKeymap(this.stagedData!);
        if (status !== Status.OK) return fail(status);
        this.stageValidated = true;
        return ok();
      }

      case Cmd.APPLY_STAGE: {
        if (this.fault === "busy") return fail(Status.BUSY);
        // P4a: active/identity checks FIRST (firmware parity)
        const stageStatus = this.stageStateFor(req.objectId);
        if (stageStatus !== Status.OK) return fail(stageStatus);
        if (!this.stageValidated) return fail(Status.NOT_STAGED);
        // copy stage to live, then discard the stage
        this.store.set(req.objectId, new Uint8Array(this.stagedData!));
        this.stageObject = null;
        this.stagedData = null;
        this.stageValidated = false;
        return ok();
      }

      case Cmd.COMMIT_STAGE: {
        // N2: M3 firmware returns NOT_SUPPORTED; persistence faults
        // only exist behind the explicit future-storage mode.
        if (this.mode === "m3") return fail(Status.NOT_SUPPORTED);
        if (this.simulateKeysHeld) return fail(Status.KEYS_HELD);
        switch (this.fault) {
          case "flash-verify-fail":
            return fail(Status.FLASH_VERIFY_FAILED);
          case "protected-address":
            return fail(Status.PROTECTED_ADDRESS);
          default:
            return ok();
        }
      }

      case Cmd.ABORT_STAGE:
        // R1: discard the staged object; live store untouched.
        this.stageObject = null;
        this.stagedData = null;
        this.stageValidated = false;
        return ok();

      case Cmd.RESET_DEFAULTS:
        // N2: restore the immutable compiled defaults (like firmware
        // loading keymaps[][]).
        this.store.set(ObjectId.KEYMAP, new Uint8Array(this.compiledDefaults));
        this.stageObject = null;
        this.stagedData = null;
        this.stageValidated = false;
        return ok();

      case Cmd.GET_DIAGNOSTICS:
        return ok(new Uint8Array(8));

      case Cmd.ARM_BOOTLOADER:
        return fail(Status.NOT_SUPPORTED);

      case Cmd.ENTER_BOOTLOADER:
        return fail(Status.NOT_SUPPORTED);

      default:
        return fail(Status.BAD_COMMAND);
    }
  }
}
