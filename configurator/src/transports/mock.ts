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
  Status,
} from "../protocol/constants.js";
import {
  decodeRequest,
  encodeResponse,
  unwrapReport,
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
  private lastTxId = -1;
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

  /** Direct object access for tests. */
  getObject(id: number): Uint8Array {
    const o = this.store.get(id);
    if (!o) throw new Error(`no object ${id}`);
    return o;
  }

  async transact(report: Uint8Array): Promise<Uint8Array> {
    if (!this.connected) throw new Error("not connected");
    const payload = unwrapReport(report);
    const req: RequestPacket = decodeRequest(payload);

    // one outstanding transaction
    if (this.busy && !(req.flags & Flags.CACHE_OK)) {
      return this.reply(req, Status.BUSY);
    }

    // cache: identical transaction returns cached response
    if (
      req.transactionId === this.lastTxId &&
      this.lastResponse !== null &&
      (req.flags & Flags.CACHE_OK)
    ) {
      return this.lastResponse;
    }

    this.busy = true;
    try {
      const resp = this.handle(req);
      const out = encodeResponse(resp);
      this.lastResponse = wrapReport(out);
      this.lastTxId = req.transactionId;
      return this.lastResponse!;
    } finally {
      this.busy = false;
    }
  }

  private reply(req: RequestPacket, status: Status): Uint8Array {
    const resp: ResponsePacket = {
      command: req.command,
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
      command: req.command,
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
        d[22] = 0x1f; // capability bits low
        d[23] = 0x00;
        return ok(d);
      }

      case Cmd.GET_CAPABILITIES: {
        const c = new Uint8Array(4);
        c[0] = 0b00011111; // dynamicKeymap | staticRgb | builtIn | customVm | reactive
        c[1] = 0; // macros off
        c[2] = 0; // wireless off
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

      case Cmd.BEGIN_STAGE:
        if (this.rejectUnknownObjects && !this.store.has(req.objectId)) {
          return fail(Status.BAD_OBJECT);
        }
        return ok();

      case Cmd.WRITE_CHUNK: {
        if (req.offset + req.payload.length > (this.store.get(req.objectId)?.length ?? 0)) {
          return fail(Status.BAD_OFFSET);
        }
        const obj = this.store.get(req.objectId);
        if (!obj) return fail(Status.BAD_OBJECT);
        obj.set(req.payload, req.offset);
        return ok();
      }

      case Cmd.VALIDATE_STAGE:
        return ok();

      case Cmd.APPLY_STAGE:
        if (this.fault === "busy") return fail(Status.BUSY);
        return ok();

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
