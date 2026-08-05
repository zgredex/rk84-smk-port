/**
 * ConfiguratorClient — the high-level API the UI uses. Wraps a
 * DeviceTransport, speaks the SMK84 protocol, and hides packet
 * mechanics. Spec §5 (shared UI, multiple transports) + §16/§17
 * workflows.
 *
 * All methods are transport-agnostic; the client works against
 * MockTransport, WebHIDTransport, or NativeHIDTransport.
 */

import {
  Cmd,
  CMD_RESPONSE_BIT,
  Flags,
  ObjectId,
  Status,
} from "./constants.js";
import {
  decodeResponse,
  encodeRequest,
  unwrapReport,
  wrapReport,
  type RequestPacket,
} from "./packet.js";
import type { DeviceTransport } from "../transports/transport.js";

export interface DeviceInfo {
  boardId: string;
  protocolMajor: number;
  protocolMinor: number;
  matrixRows: number;
  matrixCols: number;
  layers: number;
  rgbElectricalRows: number;
  rgbSourceCols: number;
  rgbPositions: number;
  maxAnimationBytes: number;
}

export class ConfiguratorError extends Error {
  constructor(
    public readonly status: Status,
    message: string,
  ) {
    super(message);
    this.name = "ConfiguratorError";
  }
}

export class ConfiguratorClient {
  private txCounter = 0;

  constructor(private transport: DeviceTransport) {}

  async connect(): Promise<void> {
    await this.transport.connect();
  }

  async disconnect(): Promise<void> {
    await this.transport.disconnect();
  }

  isConnected(): boolean {
    return this.transport.isConnected();
  }

  /** Run a request/response transaction against the device.
   * M3-03/N6 (audit): validates EVERY reply against the request before
   * accepting status/payload — echoed command + response bit, txid,
   * object, offset, payload length (exact when expectedResponseLength
   * is given), and the 24-byte wire cap. */
  private async transact(
    command: Cmd,
    objectId = 0,
    offset = 0,
    payload: Uint8Array = new Uint8Array(0),
    flags = Flags.NONE,
    expectedResponseLength?: number,
  ): Promise<Uint8Array> {
    const txid = this.txCounter++ & 0xff;
    const req: RequestPacket = {
      command,
      transactionId: txid,
      flags,
      objectId,
      offset,
      payload,
    };
    const report = wrapReport(encodeRequest(req));
    const reply = await this.transport.transact(report);
    const resp = decodeResponse(unwrapReport(reply));

    // identity validation: command echoed WITH the response bit (R3 —
    // the bit is required; a bare echo is a protocol error)
    const expectedCommand = (command & 0x7f) | CMD_RESPONSE_BIT;
    if (resp.command !== expectedCommand) {
      throw new ConfiguratorError(
        resp.status,
        `response command 0x${resp.command.toString(16)} != expected 0x${expectedCommand.toString(16)}`,
      );
    }
    if (resp.transactionId !== txid) {
      throw new ConfiguratorError(
        resp.status,
        `response txid ${resp.transactionId} != request txid ${txid}`,
      );
    }
    if (resp.objectId !== objectId) {
      throw new ConfiguratorError(
        resp.status,
        `response object ${resp.objectId} != request object ${objectId}`,
      );
    }
    if (resp.offset !== offset) {
      throw new ConfiguratorError(
        resp.status,
        `response offset ${resp.offset} != request offset ${offset}`,
      );
    }
    if (resp.payload.length > 24) {
      throw new ConfiguratorError(
        Status.BAD_LENGTH,
        `response payload ${resp.payload.length} bytes exceeds wire cap 24`,
      );
    }
    // P1 (audit): a non-OK status must surface AS THAT STATUS — error
    // responses have a zero-length payload, so the exact-length check
    // below would otherwise mask BAD_OBJECT/BAD_OFFSET/BUSY/... with
    // BAD_LENGTH. Exact lengths apply ONLY to successful responses.
    if (resp.status !== Status.OK) {
      throw new ConfiguratorError(resp.status, `cmd 0x${command.toString(16)} failed`);
    }
    // N6/P2 (audit): exact response payload length when the command has
    // a fixed-size reply — a truncated (or, for zero-length commands,
    // an over-long) payload must never be accepted.
    if (expectedResponseLength !== undefined &&
        resp.payload.length !== expectedResponseLength) {
      throw new ConfiguratorError(
        Status.BAD_LENGTH,
        `response payload ${resp.payload.length} bytes; ` +
        `expected ${expectedResponseLength}`,
      );
    }
    return resp.payload;
  }

  async getProtocolInfo(): Promise<{ major: number; minor: number }> {
    const p = await this.transact(Cmd.GET_PROTOCOL_INFO, 0, 0, new Uint8Array(), Flags.NONE, 2);
    return { major: p[0] ?? 0, minor: p[1] ?? 0 };
  }

  async getDeviceInfo(): Promise<DeviceInfo> {
    const d = await this.transact(Cmd.GET_DEVICE_INFO, 0, 0, new Uint8Array(), Flags.NONE, 24);
    const nameBytes = d.slice(0, 12);
    let end = nameBytes.indexOf(0);
    if (end === -1) end = 12;
    const boardId = new TextDecoder().decode(nameBytes.slice(0, end));
    return {
      boardId,
      protocolMajor: d[12] ?? 0,
      protocolMinor: d[13] ?? 0,
      matrixRows: d[14] ?? 0,
      matrixCols: d[15] ?? 0,
      layers: d[16] ?? 0,
      rgbElectricalRows: d[17] ?? 0,
      rgbSourceCols: d[18] ?? 0,
      rgbPositions: d[19] ?? 0,
      maxAnimationBytes: (d[20] ?? 0) | ((d[21] ?? 0) << 8),
    };
  }

  /** Read an object (bounded by firmware). */
  async readObject(objectId: ObjectId, offset = 0, length = 24): Promise<Uint8Array> {
    // chunked read to respect the 24-byte payload limit
    const out = new Uint8Array(length);
    let pos = 0;
    while (pos < length) {
      const n = Math.min(24, length - pos);
      const chunk = await this.transact(
        Cmd.READ_OBJECT, objectId, offset + pos, new Uint8Array(n),
        Flags.NONE, n,
      );
      out.set(chunk, pos);
      pos += n; // exact length is enforced (N6)
    }
    return out;
  }

  /** Begin staging an object: snapshots the live object into the
   * firmware stage buffer. M3-03: REQUIRED before writeChunk. */
  async beginStage(objectId: ObjectId): Promise<void> {
    await this.transact(Cmd.BEGIN_STAGE, objectId, 0, new Uint8Array(), Flags.NONE, 0);
  }

  /** Write an object chunk (stage buffer only; never live). */
  async writeChunk(objectId: ObjectId, offset: number, data: Uint8Array): Promise<void> {
    let pos = 0;
    while (pos < data.length) {
      const n = Math.min(24, data.length - pos);
      await this.transact(
        Cmd.WRITE_CHUNK, objectId, offset + pos, data.slice(pos, pos + n),
        Flags.NONE, 0,
      );
      pos += n;
    }
  }

  /** Validate the staged object (allowlist + locked cells). */
  async validateStage(objectId: ObjectId): Promise<void> {
    await this.transact(Cmd.VALIDATE_STAGE, objectId, 0, new Uint8Array(), Flags.NONE, 0);
  }

  /** Apply the validated stage to the live map. */
  async applyStage(objectId: ObjectId): Promise<void> {
    await this.transact(Cmd.APPLY_STAGE, objectId, 0, new Uint8Array(), Flags.NONE, 0);
  }

  /** Persist the staged profile to flash (A/B atomic). */
  async commitStage(): Promise<void> {
    await this.transact(Cmd.COMMIT_STAGE, 0, 0, new Uint8Array(), Flags.NONE, 0);
  }

  /** Abort staged changes (live map untouched). */
  async abortStage(objectId: ObjectId): Promise<void> {
    await this.transact(Cmd.ABORT_STAGE, objectId, 0, new Uint8Array(), Flags.NONE, 0);
  }

  /** Restore compiled defaults (applies live; commit separately). */
  async resetDefaults(): Promise<void> {
    await this.transact(Cmd.RESET_DEFAULTS, 0, 0, new Uint8Array(), Flags.NONE, 0);
  }

  // ---- keymap helpers (spec §16) ----

  async readKeymap(layers: number, rows: number, cols: number): Promise<Uint16Array> {
    const size = layers * rows * cols * 2;
    const raw = await this.readObject(ObjectId.KEYMAP, 0, size);
    const out = new Uint16Array(layers * rows * cols);
    for (let i = 0; i < out.length; i++) {
      out[i] = raw[i * 2] | (raw[i * 2 + 1] << 8);
    }
    return out;
  }

  /** Full staged keymap upload: BEGIN -> WRITE xN -> VALIDATE -> APPLY.
   * M3-03 (audit): the correct firmware staging workflow. R8 (audit):
   * validates input length and aborts the stage on any failure so the
   * live map is never left partially updated. */
  async writeKeymap(data: Uint16Array): Promise<void> {
    if (data.length !== 2 * 6 * 16) {
      throw new ConfiguratorError(
        Status.BAD_LENGTH,
        `keymap has ${data.length} cells; expected ${2 * 6 * 16}`,
      );
    }
    const raw = new Uint8Array(data.length * 2);
    for (let i = 0; i < data.length; i++) {
      raw[i * 2] = data[i] & 0xff;
      raw[i * 2 + 1] = (data[i] >> 8) & 0xff;
    }
    await this.beginStage(ObjectId.KEYMAP);
    try {
      await this.writeChunk(ObjectId.KEYMAP, 0, raw);
      await this.validateStage(ObjectId.KEYMAP);
      await this.applyStage(ObjectId.KEYMAP);
    } catch (error) {
      try {
        await this.abortStage(ObjectId.KEYMAP);
      } catch {
        /* Preserve the original protocol error. */
      }
      throw error;
    }
  }
}
