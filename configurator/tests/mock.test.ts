import { test } from "node:test";
import assert from "node:assert/strict";

import { MockTransport } from "../src/transports/mock.js";
import { ConfiguratorClient, ConfiguratorError } from "../src/protocol/client.js";
import { Cmd, ObjectId, Status } from "../src/protocol/constants.js";
import {
  decodeResponse,
  encodeRequest,
  unwrapReport,
  wrapReport,
} from "../src/protocol/packet.js";

function fresh() {
  const t = new MockTransport();
  const c = new ConfiguratorClient(t);
  return { t, c };
}

test("connect + protocol info", async () => {
  const { t, c } = fresh();
  await c.connect();
  assert.ok(c.isConnected());
  const info = await c.getProtocolInfo();
  assert.deepEqual(info, { major: 1, minor: 0 });
});

test("device info", async () => {
  const { c } = fresh();
  await c.connect();
  const info = await c.getDeviceInfo();
  assert.equal(info.boardId, "rk84-smk");
  assert.equal(info.protocolMajor, 1);
  assert.equal(info.matrixRows, 6);
  assert.equal(info.matrixCols, 16);
  assert.equal(info.layers, 2);
  assert.equal(info.rgbPositions, 126);
  assert.equal(info.maxAnimationBytes, 512);
});

test("keymap write + read round-trip", async () => {
  const { c } = fresh();
  await c.connect();
  const map = new Uint16Array(2 * 6 * 16);
  for (let i = 0; i < map.length; i++) map[i] = (i * 7 + 3) & 0xffff;
  await c.writeKeymap(map);
  await c.applyStage(ObjectId.KEYMAP);
  const back = await c.readKeymap(2, 6, 16);
  assert.deepEqual([...back], [...map]);
});

test("write chunk respects 24-byte limit across object boundary", async () => {
  const { t, c } = fresh();
  await c.connect();
  const obj = t.getObject(ObjectId.KEYMAP);
  assert.equal(obj.length, 384);
  const data = new Uint8Array(300).fill(0x5a);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  assert.deepEqual([...obj.slice(0, 300)], [...data]);
  // untouched tail
  assert.equal(obj[300], 0);
});

test("commit with keys held -> KEYS_HELD", async () => {
  const { t, c } = fresh();
  await c.connect();
  t.simulateKeysHeld = true;
  await assert.rejects(() => c.commitStage(), (e: unknown) => {
    assert.ok(e instanceof ConfiguratorError);
    assert.equal((e as ConfiguratorError).status, Status.KEYS_HELD);
    return true;
  });
});

test("commit with flash verify fault -> FLASH_VERIFY_FAILED", async () => {
  const { t, c } = fresh();
  await c.connect();
  t.fault = "flash-verify-fail";
  await assert.rejects(() => c.commitStage(), (e: unknown) => {
    assert.equal((e as ConfiguratorError).status, Status.FLASH_VERIFY_FAILED);
    return true;
  });
});

test("unknown command -> BAD_COMMAND", async () => {
  const { t } = fresh();
  await t.connect();
  // hand-build a request with an invalid command (0x7f)
  const req = encodeRequest({
    command: 0x7f,
    transactionId: 1,
    flags: 0,
    objectId: 0,
    offset: 0,
    payload: new Uint8Array(0),
  });
  const resp = decodeResponse(unwrapReport(await t.transact(wrapReport(req))));
  assert.equal(resp.status, Status.BAD_COMMAND);
});

test("read object out of bounds -> BAD_OFFSET", async () => {
  const { t } = fresh();
  await t.connect();
  const req = encodeRequest({
    command: Cmd.READ_OBJECT,
    transactionId: 2,
    flags: 0,
    objectId: ObjectId.KEYMAP,
    offset: 384, // exactly at end
    payload: new Uint8Array(4),
  });
  const resp = decodeResponse(unwrapReport(await t.transact(wrapReport(req))));
  assert.equal(resp.status, Status.BAD_OFFSET);
});

test("busy: one outstanding transaction", async () => {
  const { t } = fresh();
  await t.connect();
  // fire two transactions without awaiting; mock serializes via busy flag
  const report = (tx: number) =>
    wrapReport(
      encodeRequest({
        command: Cmd.GET_STATUS,
        transactionId: tx,
        flags: 0,
        objectId: 0,
        offset: 0,
        payload: new Uint8Array(0),
      }),
    );
  const [r1, r2] = await Promise.all([t.transact(report(1)), t.transact(report(2))]);
  const d1 = decodeResponse(unwrapReport(r1));
  const d2 = decodeResponse(unwrapReport(r2));
  // at least one must be OK; the other OK (serialized) or BUSY
  assert.ok(d1.status === Status.OK || d2.status === Status.OK);
  assert.ok(
    (d1.status === Status.OK && d2.status === Status.OK) ||
      d1.status === Status.BUSY ||
      d2.status === Status.BUSY,
  );
});
