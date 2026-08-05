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

test("keymap write + read round-trip (full staging workflow)", async () => {
  const { c } = fresh();
  await c.connect();
  const map = new Uint16Array(2 * 6 * 16);
  for (let i = 0; i < map.length; i++) map[i] = (i * 7 + 3) & 0xffff;
  await c.writeKeymap(map); // BEGIN -> WRITE -> VALIDATE -> APPLY
  const back = await c.readKeymap(2, 6, 16);
  assert.deepEqual([...back], [...map]);
});

test("writeChunk without BEGIN -> NOT_STAGED (M3-04 staging)", async () => {
  const { t, c } = fresh();
  await c.connect();
  const data = new Uint8Array(8).fill(0x5a);
  await assert.rejects(
    () => c.writeChunk(ObjectId.KEYMAP, 0, data),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.NOT_STAGED);
      return true;
    },
  );
});

test("applyStage without BEGIN/VALIDATE -> NOT_STAGED (M3-04 staging)", async () => {
  const { c } = fresh();
  await c.connect();
  await assert.rejects(
    () => c.applyStage(ObjectId.KEYMAP),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.NOT_STAGED);
      return true;
    },
  );
});

test("staged write + abort leaves live store untouched (M3-04)", async () => {
  const { t, c } = fresh();
  await c.connect();
  const liveBefore = t.getObject(ObjectId.KEYMAP);
  assert.equal(liveBefore[0], 0); // zeroed store

  await c.beginStage(ObjectId.KEYMAP);
  const data = new Uint8Array(4).fill(0x5a);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  await c.abortStage(ObjectId.KEYMAP);

  const liveAfter = t.getObject(ObjectId.KEYMAP);
  assert.equal(liveAfter[0], 0, "aborted staged write must not reach live store");
});

test("staged write + validate + apply reaches live store (M3-04)", async () => {
  const { t, c } = fresh();
  await c.connect();
  await c.beginStage(ObjectId.KEYMAP);
  const data = new Uint8Array(4).fill(0x5a);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  await c.validateStage(ObjectId.KEYMAP);
  await c.applyStage(ObjectId.KEYMAP);

  const live = t.getObject(ObjectId.KEYMAP);
  assert.equal(live[0], 0x5a);
  assert.equal(live[1], 0x5a);
});

test("write chunk respects 24-byte limit across object boundary (staged)", async () => {
  const { t, c } = fresh();
  await c.connect();
  assert.equal(t.getObject(ObjectId.KEYMAP).length, 384);
  const data = new Uint8Array(300).fill(0x5a);
  await c.beginStage(ObjectId.KEYMAP);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  // stage is not live yet
  assert.equal(t.getObject(ObjectId.KEYMAP)[0], 0, "live store untouched before apply");
  await c.validateStage(ObjectId.KEYMAP);
  await c.applyStage(ObjectId.KEYMAP);
  // re-fetch AFTER apply (apply replaces the live array)
  const obj = t.getObject(ObjectId.KEYMAP);
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
