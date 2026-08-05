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
import type { DeviceTransport } from "./transport.js";

/** Error modes for save/commit fault simulation. */
export type MockFault =
  | "none"
  | "flash-verify-fail"
  | "keys-held"
  | "protected-address"
  | "busy";

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

  /** Simulation controls. */
  simulateKeysHeld = false;
  /** If set, BEGIN_STAGE/WRITE_CHUNK reject with BAD_OBJECT. */
  rejectUnknownObjects = true;

  constructor() {
    for (const [id, size] of Object.entries(OBJECT_SIZES)) {
      this.store.set(Number(id), new Uint8Array(size));
    }
    // device info is a read-only object
    this.store.set(0x80, new Uint8Array(64));
    this.store.set(0x81, new Uint8Array(126)); // led map placeholder
    this.store.set(0x82, new Uint8Array(64)); // diagnostics
    // Firmware-accurate keymap defaults: the locked Fn cell (5,9) is
    // MO(1) = 0x5221 on the base layer (R1 — validation compares
    // against this). Other cells stay KC_NO (valid).
    const km = this.store.get(ObjectId.KEYMAP);
    if (km) {
      const fnIdx = (5 * 16 + 9) * 2; // base layer, (row5, col9)
      km[fnIdx] = 0x21;
      km[fnIdx + 1] = 0x52;
    }
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

  /** Direct object access for tests (reads the LIVE store). */
  getObject(id: number): Uint8Array {
    const o = this.store.get(id);
    if (!o) throw new Error(`no object ${id}`);
    return o;
  }

  /** R1 (audit): firmware-equivalent keymap validation — same SMK
   * predicate ranges the firmware allowlist uses (basic, System,
   * Consumer, modifier, MO(1), RGB custom) + the locked Fn cell
   * (5,9) which must keep its default value. */
  private validateKeymap(raw: Uint8Array): Status {
    const size = 2 * 6 * 16 * 2;
    if (raw.length !== size) return Status.BAD_LENGTH;

    const isBasic = (c: number) => c >= 0x04 && c <= 0xa4;
    const isSystem = (c: number) => c >= 0xa5 && c <= 0xa7;
    const isConsumer = (c: number) => c >= 0xa8 && c <= 0xc2;
    const isModifier = (c: number) => c >= 0xe0 && c <= 0xe7;
    const isMomentary = (c: number) => c >= 0x5220 && c <= 0x523f;
    const allowed = (c: number) =>
      c === 0x00 /* KC_NO */ ||
      c === 0x01 /* KC_TRANSPARENT */ ||
      isBasic(c) || isSystem(c) || isConsumer(c) || isModifier(c) ||
      isMomentary(c) ||
      c === RGB_BRI_UP || c === RGB_BRI_DN;

    for (let cell = 0; cell < size / 2; cell++) {
      const code = raw[cell * 2] | (raw[cell * 2 + 1] << 8);
      const pos = cell % (6 * 16);
      const row = Math.floor(pos / 16);
      const col = pos % 16;
      if (row === 5 && col === 9) {
        // locked Fn: must keep its compiled default (keymaps[][][]
        // layer-major; the live store holds the default at boot)
        const live = this.store.get(ObjectId.KEYMAP);
        const defaultCode = live ? live[cell * 2] | (live[cell * 2 + 1] << 8) : 0;
        if (code !== defaultCode) return Status.BAD_KEYCODE;
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

    // M3-04: exact-request cache — identical full report replays the
    // cached response (no CACHE_OK flag; firmware compares the whole
    // 31-byte payload). A different request is processed fresh.
    if (
      this.lastResponse !== null &&
      this.lastRequest !== null &&
      this.lastRequest.length === payload.length &&
      this.lastRequest.every((b, i) => b === payload[i])
    ) {
      return this.lastResponse;
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
        const obj = this.store.get(req.objectId);
        if (!obj) return fail(Status.BAD_OBJECT);
        if (req.offset + req.payload.length > obj.length && req.payload.length > 0) {
          return fail(Status.BAD_OFFSET);
        }
        const start = req.offset;
        const end = Math.min(obj.length, start + req.payload.length);
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
        // R1: stage buffer only — never the live store. The active
        // check comes FIRST (firmware parity: NOT_STAGED, not
        // BAD_OBJECT, when no stage is open).
        if (this.stageObject !== req.objectId || this.stagedData === null) {
          return fail(Status.NOT_STAGED);
        }
        if (req.offset + req.payload.length > this.stagedData.length) {
          return fail(Status.BAD_OFFSET);
        }
        this.stagedData.set(req.payload, req.offset);
        this.stageValidated = false;
        return ok();
      }

      case Cmd.VALIDATE_STAGE: {
        // R1: real keymap validation, like firmware.
        if (this.stageObject !== req.objectId || this.stagedData === null) {
          return fail(Status.NOT_STAGED);
        }
        const status = this.validateKeymap(this.stagedData);
        if (status !== Status.OK) return fail(status);
        this.stageValidated = true;
        return ok();
      }

      case Cmd.APPLY_STAGE: {
        if (this.fault === "busy") return fail(Status.BUSY);
        // active/validated checks FIRST (firmware parity)
        if (this.stageObject !== req.objectId || this.stagedData === null) {
          return fail(Status.NOT_STAGED);
        }
        if (!this.stageValidated) return fail(Status.NOT_STAGED);
        // copy stage to live, then discard the stage
        this.store.set(req.objectId, new Uint8Array(this.stagedData));
        this.stageObject = null;
        this.stagedData = null;
        this.stageValidated = false;
        return ok();
      }

      case Cmd.COMMIT_STAGE: {
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
        return ok();

      case Cmd.GET_DIAGNOSTICS:
        return ok(new Uint8Array(16));

      case Cmd.ARM_BOOTLOADER:
        return fail(Status.NOT_SUPPORTED);

      case Cmd.ENTER_BOOTLOADER:
        return fail(Status.NOT_SUPPORTED);

      default:
        return fail(Status.BAD_COMMAND);
    }
  }
}
