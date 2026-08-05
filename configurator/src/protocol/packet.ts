/**
 * SMK84 protocol packet codec — explicit byte serialization.
 *
 * Spec §7.1 (request) / §7.2 (response). No packed structs: the wire
 * format is always little-endian, manually assembled. Must stay
 * byte-identical with the firmware parser.
 */

import {
  CMD_RESPONSE_BIT,
  CONFIG_PAYLOAD_MAX,
  CONFIG_REPORT_DATA_SIZE,
  Flags,
} from "./constants.js";

export interface RequestPacket {
  command: number;
  transactionId: number;
  flags: number;
  objectId: number;
  offset: number; // u16
  payload: Uint8Array; // <= 24 bytes
}

export interface ResponsePacket {
  command: number; // echo (may have CMD_RESPONSE_BIT)
  transactionId: number;
  status: number;
  objectId: number;
  offset: number;
  payload: Uint8Array;
}

export class ProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ProtocolError";
  }
}

export function readU16LE(p: Uint8Array, off: number): number {
  return p[off] | (p[off + 1] << 8);
}

export function writeU16LE(p: Uint8Array, off: number, v: number): void {
  p[off] = v & 0xff;
  p[off + 1] = (v >>> 8) & 0xff;
}

/** Encode a request into the 31-byte protocol payload (no report ID). */
export function encodeRequest(req: RequestPacket): Uint8Array {
  const out = new Uint8Array(CONFIG_REPORT_DATA_SIZE);
  out[0] = req.command & 0xff;
  out[1] = req.transactionId & 0xff;
  out[2] = req.flags & 0xff;
  out[3] = req.objectId & 0xff;
  writeU16LE(out, 4, req.offset);
  out[6] = req.payload.length & 0xff;
  if (req.payload.length > CONFIG_PAYLOAD_MAX) {
    throw new ProtocolError(
      `payload ${req.payload.length} exceeds max ${CONFIG_PAYLOAD_MAX}`);
  }
  out.set(req.payload, 7);
  return out;
}

/**
 * Decode a request payload. Throws ProtocolError on structural
 * violations (bad length, oversized payload) so callers never feed
 * host-controlled lengths into copies.
 */
export function decodeRequest(data: Uint8Array): RequestPacket {
  if (data.length !== CONFIG_REPORT_DATA_SIZE) {
    throw new ProtocolError(
      `request length ${data.length} != ${CONFIG_REPORT_DATA_SIZE}`);
  }
  const payloadLen = data[6];
  if (payloadLen > CONFIG_PAYLOAD_MAX) {
    throw new ProtocolError(
      `payload length ${payloadLen} exceeds max ${CONFIG_PAYLOAD_MAX}`);
  }
  return {
    command: data[0],
    transactionId: data[1],
    flags: data[2],
    objectId: data[3],
    offset: readU16LE(data, 4),
    payload: data.slice(7, 7 + payloadLen),
  };
}

/** Encode a response into the 31-byte protocol payload. */
export function encodeResponse(resp: ResponsePacket): Uint8Array {
  const out = new Uint8Array(CONFIG_REPORT_DATA_SIZE);
  out[0] = resp.command & 0xff;
  out[1] = resp.transactionId & 0xff;
  out[2] = resp.status & 0xff;
  out[3] = resp.objectId & 0xff;
  writeU16LE(out, 4, resp.offset);
  out[6] = resp.payload.length & 0xff;
  if (resp.payload.length > CONFIG_PAYLOAD_MAX) {
    throw new ProtocolError(
      `response payload ${resp.payload.length} exceeds max ${CONFIG_PAYLOAD_MAX}`);
  }
  out.set(resp.payload, 7);
  return out;
}

/** Decode a response payload (31 bytes, no report ID). */
export function decodeResponse(data: Uint8Array): ResponsePacket {
  if (data.length !== CONFIG_REPORT_DATA_SIZE) {
    throw new ProtocolError(
      `response length ${data.length} != ${CONFIG_REPORT_DATA_SIZE}`);
  }
  const payloadLen = data[6];
  if (payloadLen > CONFIG_PAYLOAD_MAX) {
    throw new ProtocolError(
      `response payload length ${payloadLen} exceeds max ${CONFIG_PAYLOAD_MAX}`);
  }
  return {
    command: data[0],
    transactionId: data[1],
    status: data[2],
    objectId: data[3],
    offset: readU16LE(data, 4),
    payload: data.slice(7, 7 + payloadLen),
  };
}

/** Build the full 32-byte report (report ID + 31-byte payload). */
export function wrapReport(payload: Uint8Array, reportId = 8): Uint8Array {
  const out = new Uint8Array(CONFIG_REPORT_DATA_SIZE + 1);
  out[0] = reportId;
  out.set(payload, 1);
  return out;
}

/** Strip the report ID byte, returning the 31-byte payload. */
export function unwrapReport(report: Uint8Array): Uint8Array {
  if (report.length !== CONFIG_REPORT_DATA_SIZE + 1) {
    throw new ProtocolError(
      `report length ${report.length} != ${CONFIG_REPORT_DATA_SIZE + 1}`);
  }
  return report.slice(1);
}

/** Echo a command with the response bit set. */
export function withResponseBit(command: number): number {
  return (command & 0x7f) | CMD_RESPONSE_BIT;
}

export { Flags };
