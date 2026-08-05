import { test } from "node:test";
import assert from "node:assert/strict";

import {
  CONFIG_PAYLOAD_MAX,
  CONFIG_REPORT_DATA_SIZE,
} from "../src/protocol/constants.js";
import {
  ProtocolError,
  decodeRequest,
  decodeResponse,
  encodeRequest,
  encodeResponse,
  unwrapReport,
  withResponseBit,
  wrapReport,
  writeU16LE,
  readU16LE,
  type RequestPacket,
  type ResponsePacket,
} from "../src/protocol/packet.js";

test("request round-trip", () => {
  const req: RequestPacket = {
    command: 0x12,
    transactionId: 0x42,
    flags: 0x01,
    objectId: 0x01,
    offset: 0x1234,
    payload: new Uint8Array([1, 2, 3]),
  };
  const enc = encodeRequest(req);
  assert.equal(enc.length, CONFIG_REPORT_DATA_SIZE);
  const dec = decodeRequest(enc);
  assert.equal(dec.command, 0x12);
  assert.equal(dec.transactionId, 0x42);
  assert.equal(dec.flags, 0x01);
  assert.equal(dec.objectId, 0x01);
  assert.equal(dec.offset, 0x1234);
  assert.deepEqual([...dec.payload], [1, 2, 3]);
});

test("response round-trip", () => {
  const resp: ResponsePacket = {
    command: 0x90,
    transactionId: 7,
    status: 0,
    objectId: 2,
    offset: 0x0abc,
    payload: new Uint8Array([9, 8, 7, 6]),
  };
  const enc = encodeResponse(resp);
  const dec = decodeResponse(enc);
  assert.equal(dec.command, 0x90);
  assert.equal(dec.transactionId, 7);
  assert.equal(dec.status, 0);
  assert.equal(dec.offset, 0x0abc);
  assert.deepEqual([...dec.payload], [9, 8, 7, 6]);
});

test("request rejects oversize payload", () => {
  const req: RequestPacket = {
    command: 0x10,
    transactionId: 0,
    flags: 0,
    objectId: 0,
    offset: 0,
    payload: new Uint8Array(CONFIG_PAYLOAD_MAX + 1),
  };
  assert.throws(() => encodeRequest(req), ProtocolError);
});

test("decode rejects wrong length", () => {
  assert.throws(() => decodeRequest(new Uint8Array(5)), ProtocolError);
  assert.throws(() => decodeResponse(new Uint8Array(5)), ProtocolError);
});

test("decode rejects oversized payload length field", () => {
  const data = new Uint8Array(CONFIG_REPORT_DATA_SIZE);
  data[6] = 200; // > 24
  assert.throws(() => decodeRequest(data), ProtocolError);
});

test("u16 LE helpers", () => {
  const b = new Uint8Array(2);
  writeU16LE(b, 0, 0xbeef);
  assert.deepEqual([...b], [0xef, 0xbe]);
  assert.equal(readU16LE(b, 0), 0xbeef);
  writeU16LE(b, 0, 0xffff);
  assert.equal(readU16LE(b, 0), 0xffff);
});

test("report wrap/unwrap", () => {
  const payload = new Uint8Array(CONFIG_REPORT_DATA_SIZE).fill(0xaa);
  const report = wrapReport(payload, 8);
  assert.equal(report.length, CONFIG_REPORT_DATA_SIZE + 1);
  assert.equal(report[0], 8);
  assert.deepEqual([...unwrapReport(report)], [...payload]);
  assert.throws(() => unwrapReport(new Uint8Array(3)), ProtocolError);
});

test("response bit", () => {
  assert.equal(withResponseBit(0x12), 0x92);
  assert.equal(withResponseBit(0x92), 0x92);
});
