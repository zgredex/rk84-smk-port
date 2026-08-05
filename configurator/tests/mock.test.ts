import { test } from "node:test";
import assert from "node:assert/strict";

import { MockTransport } from "../src/transports/mock.js";
import { ConfiguratorClient, ConfiguratorError } from "../src/protocol/client.js";
import { Cmd, ObjectId, Status } from "../src/protocol/constants.js";
import {
  decodeRequest,
  decodeResponse,
  encodeRequest,
  encodeResponse,
  unwrapReport,
  wrapReport,
} from "../src/protocol/packet.js";
import type { DeviceTransport } from "../src/transports/transport.js";

function fresh() {
  const t = new MockTransport();
  const c = new ConfiguratorClient(t);
  return { t, c };
}

/** Transport that strips the response bit from every reply — used to
 * prove the client rejects a bare command echo (R3). */
class CorruptResponseTransport implements DeviceTransport {
  private inner = new MockTransport();
  async connect(): Promise<void> { await this.inner.connect(); }
  async disconnect(): Promise<void> { await this.inner.disconnect(); }
  isConnected(): boolean { return this.inner.isConnected(); }
  async transact(report: Uint8Array): Promise<Uint8Array> {
    const reply = await this.inner.transact(report);
    const payload = unwrapReport(reply);
    const resp = decodeResponse(payload);
    const out = encodeResponse({
      ...resp,
      command: resp.command & 0x7f, // strip the response bit
    });
    return wrapReport(out);
  }
}

/** Transport that injects one unexpected success byte into replies for
 * a specific command — proves the client rejects over-long successful
 * responses for zero-length commands (Q3 / P2). */
class ExtraPayloadTransport implements DeviceTransport {
  constructor(
    private readonly inner: DeviceTransport,
    private readonly targetCommand: Cmd,
  ) {}

  async connect(): Promise<void> { return this.inner.connect(); }
  async disconnect(): Promise<void> { return this.inner.disconnect(); }
  isConnected(): boolean { return this.inner.isConnected(); }

  async transact(report: Uint8Array): Promise<Uint8Array> {
    const request = decodeRequest(unwrapReport(report));
    const reply = await this.inner.transact(report);
    if (request.command !== this.targetCommand) return reply;
    const response = decodeResponse(unwrapReport(reply));
    return wrapReport(encodeResponse({
      ...response,
      status: Status.OK,
      payload: new Uint8Array([0xaa]),
    }));
  }
}

test("connect + protocol info", async () => {
  const { t, c } = fresh();
  await c.connect();
  assert.ok(c.isConnected());
  const info = await c.getProtocolInfo();
  assert.deepEqual(info, { major: 1, minor: 0 });
});

test("client rejects response without response bit (R3)", async () => {
  const transport = new CorruptResponseTransport();
  const c = new ConfiguratorClient(transport);
  await c.connect();
  await assert.rejects(
    () => c.getProtocolInfo(),
    /response command/i,
  );
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
  // R1: the map must contain only VALID keycodes (firmware rejects
  // arbitrary values). Zeroed store = all KC_NO (valid); edit one cell.
  const map = new Uint16Array(2 * 6 * 16); // all KC_NO
  map[0] = 0x06; // KC_C
  map[1] = 0x0a; // KC_G
  map[100] = 0x00a5; // KC_SYSTEM_POWER (valid System usage)
  // preserve the locked Fn cells: base (5,9) = MO(1) 0x5221; Fn-layer
  // (5,9) = KC_TRANSPARENT 0x0001 (P4c — matches the compiled model)
  map[5 * 16 + 9] = 0x5221;
  map[1 * 6 * 16 + 5 * 16 + 9] = 0x0001;
  await c.writeKeymap(map); // BEGIN -> WRITE -> VALIDATE -> APPLY
  const back = await c.readKeymap(2, 6, 16);
  assert.deepEqual([...back], [...map]);
});

test("writeKeymap with invalid keycode -> BAD_KEYCODE (R1)", async () => {
  const { c } = fresh();
  await c.connect();
  const map = new Uint16Array(2 * 6 * 16); // all KC_NO
  map[2] = 0xffff; // invalid
  await assert.rejects(
    () => c.writeKeymap(map),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_KEYCODE);
      return true;
    },
  );
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
  const firstByte = liveBefore[0]; // real compiled default (ESC = 0x29)

  await c.beginStage(ObjectId.KEYMAP);
  const data = new Uint8Array(4).fill(0x5a);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  await c.abortStage(ObjectId.KEYMAP);

  const liveAfter = t.getObject(ObjectId.KEYMAP);
  assert.equal(liveAfter[0], firstByte,
               "aborted staged write must not reach live store");
  assert.equal(liveAfter[1], liveBefore[1],
               "byte 1 unchanged after abort");
});

test("staged write + validate + apply reaches live store (M3-04)", async () => {
  const { t, c } = fresh();
  await c.connect();
  await c.beginStage(ObjectId.KEYMAP);
  const data = new Uint8Array(4); // KC_NO x2 = valid
  data[0] = 0x04; // KC_A
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  await c.validateStage(ObjectId.KEYMAP);
  await c.applyStage(ObjectId.KEYMAP);

  const live = t.getObject(ObjectId.KEYMAP);
  assert.equal(live[0], 0x04);
  assert.equal(live[1], 0x00);
});

test("write chunk respects 24-byte limit across object boundary (staged)", async () => {
  const { t, c } = fresh();
  await c.connect();
  assert.equal(t.getObject(ObjectId.KEYMAP).length, 384);
  const data = new Uint8Array(300); // all KC_NO = valid
  data[0] = 0x04; // KC_A at cell 0 (bytes 0-1)
  data[2] = 0x05; // KC_B at cell 1 (bytes 2-3)
  // preserve the locked Fn defaults: base (5,9) = MO(1) 0x5221 at
  // bytes 178-179; Fn-layer (5,9) = KC_TRANSPARENT 0x0001 at 370-371
  data[178] = 0x21;
  data[179] = 0x52;
  data[370] = 0x01;
  data[371] = 0x00;
  await c.beginStage(ObjectId.KEYMAP);
  await c.writeChunk(ObjectId.KEYMAP, 0, data);
  // stage is not live yet (live store still holds the compiled default)
  assert.notEqual(t.getObject(ObjectId.KEYMAP)[0], 0x04,
                  "live store untouched before apply");
  await c.validateStage(ObjectId.KEYMAP);
  await c.applyStage(ObjectId.KEYMAP);
  // re-fetch AFTER apply (apply replaces the live array)
  const obj = t.getObject(ObjectId.KEYMAP);
  assert.deepEqual([...obj.slice(0, 300)], [...data]);
  // untouched tail keeps the COMPILED default (KC_TRANSPARENT at
  // byte 300 in the real layout)
  const freshMock = new MockTransport();
  const compiledTail = freshMock.getObject(ObjectId.KEYMAP)[300];
  assert.equal(obj[300], compiledTail);
});

test("commit with keys held -> KEYS_HELD (future-storage mode)", async () => {
  const { t, c } = fresh();
  t.mode = "future-storage";
  await c.connect();
  t.simulateKeysHeld = true;
  await assert.rejects(() => c.commitStage(), (e: unknown) => {
    assert.ok(e instanceof ConfiguratorError);
    assert.equal((e as ConfiguratorError).status, Status.KEYS_HELD);
    return true;
  });
});

test("same txid with different request -> BAD_COMMAND (P3)", async () => {
  const t = new MockTransport();
  await t.connect();

  const make = (command: number) =>
    wrapReport(
      encodeRequest({
        command,
        transactionId: 42, // FIXED txid for both requests
        flags: 0,
        objectId: 0,
        offset: 0,
        payload: new Uint8Array(),
      }),
    );

  const first = decodeResponse(unwrapReport(await t.transact(make(Cmd.GET_PROTOCOL_INFO))));
  assert.equal(first.status, Status.OK);

  const collision = decodeResponse(unwrapReport(await t.transact(make(Cmd.GET_STATUS))));
  assert.equal(collision.status, Status.BAD_COMMAND);
});

test("VALIDATE with wrong object -> BAD_OBJECT, not NOT_STAGED (P4a)", async () => {
  const { t, c } = fresh();
  await c.connect();
  await c.beginStage(ObjectId.KEYMAP);
  await assert.rejects(
    () => c.validateStage(0x40 as ObjectId), // RGB_STATIC — wrong object
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_OBJECT);
      return true;
    },
  );
  // stage still usable
  await c.validateStage(ObjectId.KEYMAP);
  await c.applyStage(ObjectId.KEYMAP);
  assert.ok(true, "stage intact after wrong-object attempt");
});

test("READ_OBJECT preserves BAD_OBJECT instead of BAD_LENGTH (P1)", async () => {
  const { c } = fresh();
  await c.connect();
  await assert.rejects(
    () => c.readObject(0x40 as ObjectId, 0, 4), // RGB_STATIC — not KEYMAP
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_OBJECT);
      return true;
    },
  );
});

test("READ_OBJECT preserves BAD_OFFSET instead of BAD_LENGTH (P1)", async () => {
  const { c } = fresh();
  await c.connect();
  await assert.rejects(
    () => c.readObject(ObjectId.KEYMAP, 384, 4), // offset past end
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_OFFSET);
      return true;
    },
  );
});

test("over-long WRITE_CHUNK success response rejected (Q3/P2)", async () => {
  const inner = new MockTransport();
  const transport = new ExtraPayloadTransport(inner, Cmd.WRITE_CHUNK);
  const c = new ConfiguratorClient(transport);
  await c.connect();
  await c.beginStage(ObjectId.KEYMAP);
  await assert.rejects(
    () => c.writeChunk(ObjectId.KEYMAP, 0, new Uint8Array([0x04, 0x00])),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_LENGTH);
      return true;
    },
  );
});

test("over-long COMMIT_STAGE success response rejected (Q3/P2)", async () => {
  const transport = new ExtraPayloadTransport(new MockTransport(), Cmd.COMMIT_STAGE);
  const c = new ConfiguratorClient(transport);
  await c.connect();
  await assert.rejects(
    () => c.commitStage(),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_LENGTH);
      return true;
    },
  );
});

test("over-long RESET_DEFAULTS success response rejected (Q3/P2)", async () => {
  const transport = new ExtraPayloadTransport(new MockTransport(), Cmd.RESET_DEFAULTS);
  const c = new ConfiguratorClient(transport);
  await c.connect();
  await assert.rejects(
    () => c.resetDefaults(),
    (e: unknown) => {
      assert.ok(e instanceof ConfiguratorError);
      assert.equal((e as ConfiguratorError).status, Status.BAD_LENGTH);
      return true;
    },
  );
});

test("getObject returns a copy (Q3/P4b)", () => {
  const mock = new MockTransport();
  const first = mock.getObject(ObjectId.KEYMAP);
  first[0] ^= 0xff;
  const second = mock.getObject(ObjectId.KEYMAP);
  assert.notEqual(first[0], second[0], "mutating a getObject result must not alter the live store");
});

test("constructor clones compiled defaults (Q3/P4b)", () => {
  const defaults = new Uint8Array(384);
  const mock = new MockTransport(defaults);
  defaults[0] = 0xff;
  assert.equal(mock.getObject(ObjectId.KEYMAP)[0], 0,
               "mutating the constructor input must not alter compiledDefaults");
});

test("constructor rejects wrong default length (Q3/Q1)", () => {
  assert.throws(
    () => new MockTransport(new Uint8Array(100)),
    /compiled keymap has 100 bytes; expected 384/,
  );
});

test("commit in strict M3 mode -> NOT_SUPPORTED (N2)", async () => {
  const { c } = fresh();
  await c.connect();
  await assert.rejects(() => c.commitStage(), (e: unknown) => {
    assert.ok(e instanceof ConfiguratorError);
    assert.equal((e as ConfiguratorError).status, Status.NOT_SUPPORTED);
    return true;
  });
});

test("commit with flash verify fault -> FLASH_VERIFY_FAILED (future-storage)", async () => {
  const { t, c } = fresh();
  t.mode = "future-storage";
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
