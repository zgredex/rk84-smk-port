/* Host harness for the RK84 config protocol (configurator M3).
 *
 * Compiles the REAL config_protocol.c + dynamic_keymap.c against
 * stub headers and drives requests through the mailbox, asserting
 * spec §23 properties:
 *   - every valid command returns the right status;
 *   - invalid commands -> BAD_COMMAND;
 *   - out-of-bounds offsets -> BAD_OFFSET;
 *   - oversize payloads rejected;
 *   - cache/retry semantics;
 *   - no packet writes outside its object.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "harness_stubs.h"
#include "config_protocol.c"
#include "dynamic_keymap.c"

/* Compiled default keymap (stub): needed by the locked-Fn validation
 * in staging VALIDATE. Fn (5,9) = MO(1) on base, transparent on Fn
 * layer; other cells are arbitrary-but-distinct. */
const uint16_t keymaps[2][MATRIX_ROWS][MATRIX_COLS] = {
    {
        { 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
          0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 },
        { 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
          0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23 },
        { 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B,
          0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33 },
        { 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
          0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43 },
        { 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
          0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53 },
        { 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B,
          0x5C, MO(1), 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63 },
    },
    {
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          0x00AB, /* F8 -> media next track */
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
        { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
          KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT },
    },
};

static int failures = 0;
static int checks = 0;

#define CHECK(cond, name)                                                \
    do {                                                                 \
        checks++;                                                        \
        if (cond) {                                                      \
            printf("PASS: %s\n", name);                                  \
        } else {                                                         \
            printf("FAIL: %s\n", name);                                  \
            failures++;                                                  \
        }                                                                \
    } while (0)

/* Build a 32-byte report: [0]=report ID 8, [1..31]=payload. */
static void make_request(uint8_t *r, uint8_t cmd, uint8_t txid, uint8_t flags,
                         uint8_t object, uint16_t offset,
                         const uint8_t *payload, uint8_t plen)
{
    memset(r, 0, 32);
    r[0] = REPORT_ID_SMK84_CONFIG;
    r[1] = cmd;
    r[2] = txid;
    r[3] = flags;
    r[4] = object;
    r[5] = (uint8_t)offset;
    r[6] = (uint8_t)(offset >> 8);
    r[7] = plen;
    if (payload && plen) {
        memcpy(r + 8, payload, plen);
    }
}

/* Feed a report through the mailbox + task; returns response payload
 * pointer (config_tx.data + 1) and length (excluding report ID). */
static uint8_t *run_request(const uint8_t *report)
{
    config_rx_release();
    config_rx_append(report, 8);
    config_rx_append(report + 8, 8);
    config_rx_append(report + 16, 8);
    config_rx_append(report + 24, 8);
    if (!config_rx_pending()) {
        return NULL;
    }
    config_protocol_task();
    /* response: config_tx.data[0] = report ID, [1] = cmd|0x80, [2]=txid,
     * [3]=status, [4]=object, [5..6]=offset, [7]=len, [8..]=payload */
    return config_tx.data;
}

static void test_protocol_info(void)
{
    uint8_t r[32];
    make_request(r, CFG_CMD_GET_PROTOCOL_INFO, 1, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL, "get protocol info responds");
    CHECK(resp[3] == CFG_STATUS_OK, "protocol info status OK");
    CHECK(resp[8] == 0x01 && resp[9] == 0x00, "protocol version 1.0");
}

static void test_device_info(void)
{
    uint8_t r[32];
    make_request(r, CFG_CMD_GET_DEVICE_INFO, 2, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL, "get device info responds");
    CHECK(resp[3] == CFG_STATUS_OK, "device info status OK");
    CHECK(resp[7] == 24, "device info payload 24 bytes");
    CHECK(memcmp(resp + 8, "rk84-smk", 8) == 0, "board id correct");
    CHECK(resp[8 + 14] == 6 && resp[8 + 15] == 16,
          "matrix 6x16 in device info");
}

static void test_bad_command(void)
{
    uint8_t r[32];
    make_request(r, 0x7F, 3, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_COMMAND,
          "unknown command -> BAD_COMMAND");
}

static void test_read_keymap(void)
{
    /* reset defaults so the map has known content */
    uint8_t r[32];
    make_request(r, CFG_CMD_RESET_DEFAULTS, 4, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);

    /* read 4 bytes at offset 0 */
    make_request(r, CFG_CMD_READ_OBJECT, 5, 0, CFG_OBJECT_KEYMAP, 0, NULL, 4);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "read keymap OK");
    CHECK(resp[7] == 4, "read keymap returns 4 bytes");
}

static void test_read_out_of_bounds(void)
{
    uint8_t r[32];
    /* offset 383, len 4 -> 387 > 384 */
    make_request(r, CFG_CMD_READ_OBJECT, 6, 0, CFG_OBJECT_KEYMAP, 383, NULL, 4);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_OFFSET,
          "read beyond object -> BAD_OFFSET");
}

static void test_write_chunk(void)
{
    uint8_t r[32];
    uint8_t payload[4] = { 0x04, 0x00, 0x05, 0x00 }; /* KC_A, KC_B */

    /* staging: BEGIN -> WRITE -> VALIDATE -> APPLY */
    make_request(r, CFG_CMD_BEGIN_STAGE, 7, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "begin stage OK");

    make_request(r, CFG_CMD_WRITE_CHUNK, 7, 0, CFG_OBJECT_KEYMAP, 0, payload, 4);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "write chunk OK");

    make_request(r, CFG_CMD_VALIDATE_STAGE, 7, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "validate stage OK");

    make_request(r, CFG_CMD_APPLY_STAGE, 7, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "apply stage OK");

    /* read back: applied map at offset 0 = KC_A (0x04) */
    make_request(r, CFG_CMD_READ_OBJECT, 8, 0, CFG_OBJECT_KEYMAP, 0, NULL, 2);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "read back OK");
    CHECK(resp[8] == 0x04 && resp[9] == 0x00, "read back KC_A");
}

static void test_write_not_staged(void)
{
    uint8_t r[32];
    uint8_t payload[2] = { 0x04, 0x00 };
    make_request(r, CFG_CMD_WRITE_CHUNK, 9, 0, CFG_OBJECT_KEYMAP, 0, payload, 2);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_NOT_STAGED,
          "write without begin -> NOT_STAGED");
}

static void test_write_locked_fn_rejected(void)
{
    uint8_t r[32];
    /* Fn at (5,9): index = 5*16*2 + 9*2 = 178 */
    uint8_t payload[2] = { 0x04, 0x00 }; /* KC_A — wrong value for Fn */
    make_request(r, CFG_CMD_BEGIN_STAGE, 10, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 10, 0, CFG_OBJECT_KEYMAP, 178, payload, 2);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_KEYCODE,
          "write changed value to locked Fn rejected");
}

static void test_write_locked_fn_idempotent(void)
{
    uint8_t r[32];
    /* Fn at (5,9) = 178; base layer value is MO(1) = 0x5221 */
    uint8_t payload[2] = { 0x21, 0x52 }; /* MO(1) LE — the required value */
    make_request(r, CFG_CMD_BEGIN_STAGE, 11, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 11, 0, CFG_OBJECT_KEYMAP, 178, payload, 2);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK,
          "write unchanged value to locked Fn OK (idempotent)");
}

static void test_write_bad_keycode_rejected(void)
{
    uint8_t r[32];
    uint8_t payload[2] = { 0xFF, 0xFF }; /* 0xFFFF not allowed */
    make_request(r, CFG_CMD_BEGIN_STAGE, 12, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 12, 0, CFG_OBJECT_KEYMAP, 0, payload, 2);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_KEYCODE,
          "write invalid keycode rejected");
}

static void test_write_out_of_bounds(void)
{
    uint8_t r[32];
    uint8_t payload[4] = { 0, 0, 0, 0 };
    make_request(r, CFG_CMD_BEGIN_STAGE, 13, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 13, 0, CFG_OBJECT_KEYMAP, 382, payload, 4);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_OFFSET,
          "write beyond object -> BAD_OFFSET");
}

static void test_abort_stage(void)
{
    uint8_t r[32];
    /* M3-02 regression (audit): write KC_C over the default KC_A, then
     * abort. The live map must still hold the DEFAULT (KC_A). The old
     * test wrote KC_A over KC_A and could not detect a live write. */
    uint8_t payload[4] = { 0x06, 0x00, 0x07, 0x00 }; /* KC_C, KC_D */
    make_request(r, CFG_CMD_BEGIN_STAGE, 14, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 14, 0, CFG_OBJECT_KEYMAP, 0, payload, 4);
    run_request(r);
    /* abort discards the stage; the live map is untouched */
    make_request(r, CFG_CMD_ABORT_STAGE, 14, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "abort stage OK");

    /* read back at offset 0: must be the DEFAULT KC_A (0x04), NOT KC_C */
    make_request(r, CFG_CMD_READ_OBJECT, 15, 0, CFG_OBJECT_KEYMAP, 0, NULL, 2);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "read after abort OK");
    CHECK(resp[8] == 0x04 && resp[9] == 0x00,
          "aborted stage did not mutate live map (KC_C must NOT be live)");
}

static void test_valid_then_invalid_cell_nothing_live(void)
{
    uint8_t r[32];
    /* Write KC_C (valid) at offset 0, then 0xFFFF (invalid) at offset 2.
     * Neither may become live: the chunk is rejected, live map keeps
     * defaults. */
    uint8_t payload[4] = { 0x06, 0x00, 0xFF, 0xFF };
    make_request(r, CFG_CMD_BEGIN_STAGE, 21, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 21, 0, CFG_OBJECT_KEYMAP, 0, payload, 4);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_BAD_KEYCODE,
          "chunk with invalid cell rejected");

    make_request(r, CFG_CMD_READ_OBJECT, 22, 0, CFG_OBJECT_KEYMAP, 0, NULL, 2);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "read after reject OK");
    CHECK(resp[8] == 0x04 && resp[9] == 0x00,
          "valid cell before invalid did NOT become live (still KC_A)");
}

static void test_split_keycode_across_chunks(void)
{
    uint8_t r[32];
    /* A 16-bit keycode with a nonzero high byte: KC_SYSTEM_POWER = 0x00A5.
     * Chunk 1: low byte at offset 0. Chunk 2: high byte at offset 1. */
    uint8_t lo[1] = { 0xA5 };
    uint8_t hi[1] = { 0x00 };
    make_request(r, CFG_CMD_BEGIN_STAGE, 23, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    run_request(r);
    make_request(r, CFG_CMD_WRITE_CHUNK, 23, 0, CFG_OBJECT_KEYMAP, 0, lo, 1);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "chunk 1 (low byte) OK");
    make_request(r, CFG_CMD_WRITE_CHUNK, 23, 0, CFG_OBJECT_KEYMAP, 1, hi, 1);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK,
          "chunk 2 (high byte) validates completed cell OK");

    /* validate + apply, then read back the full keycode */
    make_request(r, CFG_CMD_VALIDATE_STAGE, 23, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "validate split keycode OK");
    make_request(r, CFG_CMD_APPLY_STAGE, 23, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "apply split keycode OK");

    make_request(r, CFG_CMD_READ_OBJECT, 24, 0, CFG_OBJECT_KEYMAP, 0, NULL, 2);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "read split keycode OK");
    CHECK(resp[8] == 0xA5 && resp[9] == 0x00,
          "split keycode reassembled (KC_SYSTEM_POWER 0x00A5)");
}

static void test_commit_not_supported_yet(void)
{
    uint8_t r[32];
    make_request(r, CFG_CMD_COMMIT_STAGE, 16, 0, CFG_OBJECT_KEYMAP, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_NOT_SUPPORTED,
          "commit -> NOT_SUPPORTED (M5 pending)");
}

static void test_bootloader_guarded(void)
{
    uint8_t r[32];
    make_request(r, CFG_CMD_ARM_BOOTLOADER, 17, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_NOT_SUPPORTED,
          "arm bootloader -> NOT_SUPPORTED");
    make_request(r, CFG_CMD_ENTER_BOOTLOADER, 18, 0, 0, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_NOT_SUPPORTED,
          "enter bootloader -> NOT_SUPPORTED");
}

static void test_diagnostics(void)
{
    uint8_t r[32];
    make_request(r, CFG_CMD_GET_DIAGNOSTICS, 19, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "diagnostics OK");
    CHECK(resp[7] == 8, "diagnostics payload 8 bytes");
}

static void test_exact_request_cache(void)
{
    uint8_t r[32];
    /* first GET_STATUS with txid 20 */
    make_request(r, CFG_CMD_GET_STATUS, 20, 0, 0, 0, NULL, 0);
    uint8_t *resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK, "first get status OK");

    /* identical retry -> cached */
    make_request(r, CFG_CMD_GET_STATUS, 20, 0, 0, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK,
          "identical retry served from cache");

    /* same txid, DIFFERENT command -> must be processed fresh, not a
     * stale cache replay (audit F6) */
    make_request(r, CFG_CMD_GET_CAPABILITIES, 20, 0, 0, 0, NULL, 0);
    resp = run_request(r);
    CHECK(resp != NULL && resp[3] == CFG_STATUS_OK &&
          resp[1] == (CFG_CMD_GET_CAPABILITIES | CFG_CMD_RESPONSE_BIT),
          "same txid + different command -> fresh response, not stale");
}

/* ---- M3-05/M3-06: EP0 transfer state machine (mailbox level) ---- */

static void test_rx_accumulation_partial(void)
{
    /* M3-05: append returns FALSE until exactly 32 bytes arrive. */
    uint8_t pkt[8] = { 0x08, 0, 0, 0, 0, 0, 0, 0 };
    config_rx_begin();
    CHECK(config_rx_append(pkt, 8) == false, "first 8B append not complete");
    CHECK(config_rx_pending() == false, "not pending after 8B");
    CHECK(config_rx_append(pkt, 8) == false, "16B append not complete");
    CHECK(config_rx_append(pkt, 8) == false, "24B append not complete");
    CHECK(config_rx_append(pkt, 8) == true, "32B append completes");
    CHECK(config_rx_pending() == true, "pending after 32B");
    config_rx_release();
}

static void test_rx_reject_wrong_lengths(void)
{
    /* M3-05: a transfer of any length other than 32 must not reach
     * pending. Simulated via the SET_REPORT gate: the firmware stalls
     * wLength != 32 at the USB layer; at the mailbox layer, oversize
     * accumulation is reset and short transfers never complete. */
    uint8_t pkt[8] = { 0x08, 0, 0, 0, 0, 0, 0, 0 };
    config_rx_begin();
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8);
    /* abort mid-transfer (e.g. host sends a new SETUP): the mailbox
     * must be clean for the next request */
    config_rx_begin();
    CHECK(config_rx_pending() == false, "abort mid-transfer clears mailbox");
    /* now a full valid transfer completes */
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8);
    CHECK(config_rx_append(pkt, 8) == true, "full transfer after abort OK");
    config_rx_release();
}

static void test_rx_oversize_reset(void)
{
    /* M3-05: appending more than 32 bytes total resets the mailbox
     * (corrupt accumulation) rather than overrunning. */
    uint8_t pkt[8] = { 0x08, 0, 0, 0, 0, 0, 0, 0 };
    config_rx_begin();
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8);
    config_rx_append(pkt, 8); /* 32: complete */
    CHECK(config_rx_pending() == true, "32B pending");
    /* further append while pending is dropped */
    CHECK(config_rx_append(pkt, 8) == false, "append while pending dropped");
    config_rx_release();
}

int main(void)
{
    printf("RK84 config protocol host harness\n");
    printf("---------------------------------\n");
    dynamic_keymap_load_defaults();
    test_protocol_info();
    test_device_info();
    test_bad_command();
    test_read_keymap();
    test_read_out_of_bounds();
    test_write_chunk();
    test_write_not_staged();
    test_write_locked_fn_rejected();
    test_write_locked_fn_idempotent();
    test_write_bad_keycode_rejected();
    test_write_out_of_bounds();
    test_abort_stage();
    test_valid_then_invalid_cell_nothing_live();
    test_split_keycode_across_chunks();
    test_commit_not_supported_yet();
    test_bootloader_guarded();
    test_diagnostics();
    test_exact_request_cache();
    test_rx_accumulation_partial();
    test_rx_reject_wrong_lengths();
    test_rx_oversize_reset();
    printf("---------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
