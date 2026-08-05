/*
 * RK84 configuration protocol — main-loop implementation (M3).
 *
 * ISR discipline (spec §6.3): the USB ISR only appends bytes into the
 * RX mailbox (config_rx_append). All parsing, validation, and
 * dispatch happens here, in the main loop (config_protocol_task).
 *
 * Strict validation (spec §7.7): length/offset are checked BEFORE any
 * copy; host-controlled lengths never reach memcpy.
 */
#include "config_protocol.h"
#include "dynamic_keymap.h"
#include "kbdef.h"
#include "layout.h"   /* keymaps[][][] for locked-Fn required values */
#if SMK_CONFIG_PROTOCOL
#include "usbdef.h"   /* REPORT_TYPE_FEATURE for the R2 validators */
#endif

#if SMK_CONFIG_PROTOCOL

#include <string.h>

static __xdata config_mailbox_t config_rx;
static __xdata config_mailbox_t config_tx;

/* Cached last response for retry semantics (spec §7.6). */
static __xdata uint8_t config_cache[CONFIG_MAILBOX_SIZE];
static __xdata uint8_t config_cache_len;
static __xdata uint8_t config_cache_txid;
static __xdata uint8_t config_cache_cmd;
/* Exact-request compare (audit): the full 31-byte request is cached
 * so a DIFFERENT request reusing a transaction ID is a protocol
 * error, never a stale replay. */
static __xdata uint8_t config_cache_req[CONFIG_REPORT_DATA_SIZE];

/* Upload staging buffer (M4 semantics, audit): the live map is only
 * mutated by a validated APPLY_STAGE. */
static __xdata uint8_t config_stage_buf[CONFIG_STAGE_BUF_SIZE];
static __xdata config_stage_t config_stage;

/* The request currently being processed (31-byte payload) — needed by
 * build_response for the exact-request cache. */
static __xdata uint8_t config_last_req[CONFIG_REPORT_DATA_SIZE];

/* Diagnostics counters (spec §22). */
static __xdata uint8_t diag_malformed;
static __xdata uint8_t diag_unknown_cmd;
static __xdata uint8_t diag_retry;

bool config_rx_start(void)
{
    /* R5: a complete request is owned by the main loop until release.
     * Refuse a new transfer rather than clobbering it. */
    if (config_rx.pending) {
        return false;
    }
    /* No complete request: discard any abandoned partial transfer. */
    config_rx.length = 0;
    return true;
}

void config_rx_force_reset(void)
{
    config_rx.pending = 0;
    config_rx.length = 0;
}

void config_tx_invalidate(void)
{
    config_tx.length = 0;
}

bool config_rx_append(const uint8_t *pkt, uint8_t len)
{
    /* ISR-safe: bounded copy, no allocation, no long loops. */
    if (config_rx.pending) {
        return false; /* overrun: drop (main loop will claim) */
    }
    if (config_rx.length + len > CONFIG_MAILBOX_SIZE) {
        config_rx.length = 0; /* reset: corrupt accumulation */
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        config_rx.data[config_rx.length + i] = pkt[i];
    }
    config_rx.length += len;
    /* M3-05 (audit): pending fires ONLY at exactly 32 bytes. A partial
     * append returns false — the caller (EP0 OUT IRQ) must not treat
     * it as a complete report. */
    if (config_rx.length == CONFIG_MAILBOX_SIZE) {
        config_rx.pending = 1;
        return true;
    }
    return false;
}

bool config_rx_pending(void)
{
    return config_rx.pending != 0;
}

uint8_t *config_rx_claim(void)
{
    return config_rx.data;
}

void config_rx_release(void)
{
    config_rx.pending = 0;
    config_rx.length = 0;
}

const uint8_t *config_tx_get(uint8_t *len)
{
    if (config_tx.length == 0) {
        return NULL;
    }
    *len = config_tx.length;
    return config_tx.data;
}

void config_cache_set(const uint8_t *resp, uint8_t len,
                      const uint8_t *req_payload, uint8_t req_len)
{
    if (len > CONFIG_MAILBOX_SIZE) {
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        config_cache[i] = resp[i];
    }
    config_cache_len = len;
    config_cache_txid = resp[2]; /* transaction id */
    config_cache_cmd = resp[1] & 0x7F;
    if (req_len <= CONFIG_REPORT_DATA_SIZE) {
        for (uint8_t i = 0; i < req_len; i++) {
            config_cache_req[i] = req_payload[i];
        }
    }
}

/* Exact-request cache hit (audit): returns true ONLY when the full
 * 31-byte request (command, txid, flags, object, offset, payload) is
 * identical to the cached one. A reused transaction ID with different
 * contents is NOT a cache hit — the caller treats it as a protocol
 * error. */
bool config_cache_get(const uint8_t *req_payload, uint8_t req_len,
                      const uint8_t **resp, uint8_t *len)
{
    if (config_cache_len == 0) {
        return false;
    }
    if (req_len != CONFIG_REPORT_DATA_SIZE) {
        return false;
    }
    for (uint8_t i = 0; i < CONFIG_REPORT_DATA_SIZE; i++) {
        if (config_cache_req[i] != req_payload[i]) {
            return false; /* different request, same ID -> not a retry */
        }
    }
    *resp = config_cache;
    *len = config_cache_len;
    diag_retry++;
    return true;
}

/* ---- packet helpers ------------------------------------------------- */

static uint16_t cfg_read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void cfg_write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Build a response into config_tx. command echoed with response bit. */
static void build_response(uint8_t command, uint8_t txid, uint8_t status,
                           uint8_t object, uint16_t offset,
                           const uint8_t *payload, uint8_t payload_len)
{
    uint8_t *r = config_tx.data;
    if (payload_len > CONFIG_PAYLOAD_MAX) {
        payload_len = CONFIG_PAYLOAD_MAX;
    }
    r[0] = REPORT_ID_SMK84_CONFIG;
    r[1] = (command & 0x7F) | CFG_CMD_RESPONSE_BIT;
    r[2] = txid;
    r[3] = status;
    r[4] = object;
    cfg_write_u16(r + 5, offset);
    r[7] = payload_len;
    if (payload_len > 0) {
        for (uint8_t i = 0; i < payload_len; i++) {
            r[8 + i] = payload[i];
        }
    }
    /* M3-01 (audit): the wire report is ALWAYS the full 32 bytes
     * (report ID + 31 payload), zero-padded. The TypeScript codec
     * rejects any other length. */
    for (uint8_t i = 8 + payload_len; i < CONFIG_MAILBOX_SIZE; i++) {
        r[i] = 0;
    }
    config_tx.length = CONFIG_REPORT_TOTAL_SIZE;
    config_cache_set(r, config_tx.length, config_last_req, CONFIG_REPORT_DATA_SIZE);
}

/* ---- capabilities (R7 audit: single source of truth) ---------------- */

static void config_get_capabilities(uint8_t out[4])
{
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
#if SMK_DYNAMIC_KEYMAP
    out[0] |= 0x01; /* dynamicKeymap */
#endif
    /* Future milestones set their bits here when implemented:
     * 0x02 static RGB, 0x04 built-in effects, 0x08 custom VM,
     * 0x10 reactive effects. */
}

/* ---- HID request validation (R2 audit) ------------------------------- */

bool config_set_report_valid(
    uint8_t report_type, uint8_t report_id,
    uint16_t interface, uint16_t length)
{
    return report_type == REPORT_TYPE_FEATURE &&
           report_id == REPORT_ID_SMK84_CONFIG &&
           interface == 1 &&
           length == CONFIG_REPORT_TOTAL_SIZE;
}

bool config_get_report_valid(
    uint8_t report_type, uint8_t report_id,
    uint16_t interface, uint16_t length)
{
    (void)length;
    return report_type == REPORT_TYPE_FEATURE &&
           report_id == REPORT_ID_SMK84_CONFIG &&
           interface == 1;
}

/* ---- command dispatch ------------------------------------------------- */

static void handle_request(const uint8_t *req)
{
    uint8_t command = req[0];
    uint8_t txid    = req[1];
    uint8_t flags   = req[2];
    uint8_t object  = req[3];
    uint16_t offset = cfg_read_u16(req + 4);
    uint8_t plen    = req[6];
    const uint8_t *payload = req + 7;
    uint8_t status = CFG_STATUS_OK;

    (void)flags;

    /* Spec §7.7: validate before any copy. */
    if (plen > CONFIG_PAYLOAD_MAX) {
        build_response(command, txid, CFG_STATUS_BAD_LENGTH, object, offset, NULL, 0);
        diag_malformed++;
        return;
    }

    switch (command) {
        case CFG_CMD_GET_PROTOCOL_INFO: {
            __xdata uint8_t p[2] = { 0x01, 0x00 }; /* major 1, minor 0 */
            build_response(command, txid, status, object, offset, p, 2);
            break;
        }

        case CFG_CMD_GET_DEVICE_INFO: {
            __xdata uint8_t d[24] = { 0 };
            __xdata uint8_t caps[4];
            const char *name = "rk84-smk";
            uint8_t i = 0;
            for (; name[i] && i < 12; i++) d[i] = (uint8_t)name[i];
            d[12] = 0x01; /* protocol major */
            d[13] = 0x00; /* protocol minor */
            d[14] = 6;    /* matrix rows */
            d[15] = 16;   /* matrix cols */
            d[16] = 2;    /* layers */
            d[17] = 6;    /* rgb electrical rows */
            d[18] = 21;   /* rgb source cols */
            d[19] = 126;  /* rgb positions */
            d[20] = 0x00; /* max animation bytes lo (512) */
            d[21] = 0x02;
            /* R7 (audit): capability bits from the ONE shared source —
             * no more advertising unimplemented features here. */
            config_get_capabilities(caps);
            d[22] = caps[0];
            d[23] = caps[1];
            build_response(command, txid, status, object, offset, d, 24);
            break;
        }

        case CFG_CMD_GET_CAPABILITIES: {
            /* R7 (audit): one capability source shared with DEVICE_INFO. */
            __xdata uint8_t c[4];
            config_get_capabilities(c);
            build_response(command, txid, status, object, offset, c, 4);
            break;
        }

        case CFG_CMD_GET_STATUS: {
            __xdata uint8_t s[1] = { 0x00 };
            build_response(command, txid, status, object, offset, s, 1);
            break;
        }

        case CFG_CMD_READ_OBJECT: {
            if (object == CFG_OBJECT_KEYMAP) {
                /* Read the live dynamic map: [layer][row][col] u16 LE. */
                __xdata uint8_t buf[CONFIG_PAYLOAD_MAX];
                uint8_t n = plen > 0 ? plen : 24;
                if ((uint32_t)offset + n > CFG_OBJECT_KEYMAP_SIZE) {
                    build_response(command, txid, CFG_STATUS_BAD_OFFSET, object, offset, NULL, 0);
                    break;
                }
                for (uint8_t i = 0; i < n; i++) {
                    uint32_t idx = (uint32_t)offset + i;
                    uint8_t layer = (uint8_t)(idx / (6u * 16u * 2u));
                    uint32_t rem = idx % (6u * 16u * 2u);
                    uint8_t row = (uint8_t)(rem / (16u * 2u));
                    uint8_t col = (uint8_t)((rem / 2u) % 16u);
                    uint16_t code;
                    if (layer < RK84_DYNAMIC_LAYERS) {
                        code = dynamic_keymap_get(layer, row, col);
                        if (code == 0xFFFFu) {
                            code = 0x0000u; /* fallback: KC_NO */
                        }
                    } else {
                        code = 0x0000u;
                    }
                    buf[i] = (idx & 1u) ? (uint8_t)(code >> 8) : (uint8_t)code;
                }
                build_response(command, txid, status, object, offset, buf, n);
            } else {
                build_response(command, txid, CFG_STATUS_BAD_OBJECT, object, offset, NULL, 0);
            }
            break;
        }

        case CFG_CMD_BEGIN_STAGE: {
            /* Copy the active object into the stage buffer. */
            if (object != CFG_OBJECT_KEYMAP) {
                build_response(command, txid, CFG_STATUS_BAD_OBJECT, object, offset, NULL, 0);
                break;
            }
            config_stage.object  = object;
            config_stage.length  = CFG_OBJECT_KEYMAP_SIZE;
            config_stage.active  = 1;
            config_stage.validated = 0;
            /* Snapshot the live map into the stage. */
            for (uint16_t i = 0; i < CFG_OBJECT_KEYMAP_SIZE; i++) {
                uint16_t idx = i;
                uint8_t layer = (uint8_t)(idx / (6u * 16u * 2u));
                uint32_t rem = idx % (6u * 16u * 2u);
                uint8_t row = (uint8_t)(rem / (16u * 2u));
                uint8_t col = (uint8_t)((rem / 2u) % 16u);
                uint16_t code = dynamic_keymap_get(layer, row, col);
                if (code == 0xFFFFu) code = 0;
                config_stage_buf[i] = (idx & 1u) ? (uint8_t)(code >> 8) : (uint8_t)code;
            }
            build_response(command, txid, status, object, offset, NULL, 0);
            break;
        }

        case CFG_CMD_WRITE_CHUNK: {
            /* R4 (audit): byte-copy into the stage ONLY. No keycode
             * interpretation here — the stage is not live, and a
             * half-updated cell (high byte first, or low byte pending)
             * must not be judged as a temporary keycode. VALIDATE_STAGE
             * is the sole place that interprets keycodes. */
            if (object != CFG_OBJECT_KEYMAP) {
                build_response(command, txid, CFG_STATUS_BAD_OBJECT, object, offset, NULL, 0);
                break;
            }
            if (!config_stage.active) {
                build_response(command, txid, CFG_STATUS_NOT_STAGED, object, offset, NULL, 0);
                break;
            }
            if ((uint32_t)offset + plen > config_stage.length) {
                build_response(command, txid, CFG_STATUS_BAD_OFFSET, object, offset, NULL, 0);
                break;
            }
            for (uint8_t i = 0; i < plen; i++) {
                config_stage_buf[offset + i] = payload[i];
            }
            config_stage.validated = 0;
            build_response(command, txid, status, object, offset, NULL, 0);
            break;
        }

        case CFG_CMD_VALIDATE_STAGE: {
            /* Validate all 192 cells: allowlist + locked positions.
             * Side-effect-free (M3-02): no live map writes. */
            if (!config_stage.active) {
                build_response(command, txid, CFG_STATUS_NOT_STAGED, object, offset, NULL, 0);
                break;
            }
            status = CFG_STATUS_OK;
            for (uint16_t idx = 0; idx < CFG_OBJECT_KEYMAP_SIZE && status == CFG_STATUS_OK; idx += 2) {
                uint8_t layer = (uint8_t)(idx / (6u * 16u * 2u));
                uint32_t rem = idx % (6u * 16u * 2u);
                uint8_t row = (uint8_t)(rem / (16u * 2u));
                uint8_t col = (uint8_t)((rem / 2u) % 16u);
                uint16_t code = (uint16_t)config_stage_buf[idx] |
                                ((uint16_t)config_stage_buf[idx + 1] << 8);
                status = (uint8_t)dynamic_keymap_validate_cell(
                    layer, row, col, code);
            }
            if (status == CFG_STATUS_OK) {
                config_stage.validated = 1;
            }
            build_response(command, txid, status, object, offset, NULL, 0);
            break;
        }

        case CFG_CMD_APPLY_STAGE: {
            /* Copy the validated stage into the live map. */
            if (!config_stage.active) {
                build_response(command, txid, CFG_STATUS_NOT_STAGED, object, offset, NULL, 0);
                break;
            }
            if (!config_stage.validated) {
                build_response(command, txid, CFG_STATUS_NOT_STAGED, object, offset, NULL, 0);
                break;
            }
            for (uint16_t idx = 0; idx < CFG_OBJECT_KEYMAP_SIZE; idx += 2) {
                uint8_t layer = (uint8_t)(idx / (6u * 16u * 2u));
                uint32_t rem = idx % (6u * 16u * 2u);
                uint8_t row = (uint8_t)(rem / (16u * 2u));
                uint8_t col = (uint8_t)((rem / 2u) % 16u);
                uint16_t code = (uint16_t)config_stage_buf[idx] |
                                ((uint16_t)config_stage_buf[idx + 1] << 8);
                dynamic_keymap_set(layer, row, col, code);
            }
            dynamic_keymap_activate();
            config_stage.active = 0;
            config_stage.validated = 0;
            build_response(command, txid, status, object, offset, NULL, 0);
            break;
        }

        case CFG_CMD_ABORT_STAGE:
            config_stage.active = 0;
            config_stage.validated = 0;
            build_response(command, txid, status, object, offset, NULL, 0);
            break;

        case CFG_CMD_COMMIT_STAGE:
            /* M5 adds persistence. Until then: RAM-only, but report
             * NOT_SUPPORTED so the host knows nothing was written. */
            build_response(command, txid, CFG_STATUS_NOT_SUPPORTED, object, offset, NULL, 0);
            break;

        case CFG_CMD_RESET_DEFAULTS:
            dynamic_keymap_load_defaults();
            config_stage.active = 0;
            build_response(command, txid, status, object, offset, NULL, 0);
            break;

        case CFG_CMD_GET_DIAGNOSTICS: {
            __xdata uint8_t di[8] = {
                diag_malformed, diag_unknown_cmd, diag_retry, 0,
                0, 0, 0, 0,
            };
            build_response(command, txid, status, object, offset, di, 8);
            break;
        }

        case CFG_CMD_ARM_BOOTLOADER:
        case CFG_CMD_ENTER_BOOTLOADER:
            /* v1: no self-flashing/bootloader entry (spec §4.9). */
            build_response(command, txid, CFG_STATUS_NOT_SUPPORTED, object, offset, NULL, 0);
            break;

        default:
            diag_unknown_cmd++;
            build_response(command, txid, CFG_STATUS_BAD_COMMAND, object, offset, NULL, 0);
            break;
    }
}

void config_protocol_task(void)
{
    if (!config_rx_pending()) {
        return;
    }

    uint8_t *req = config_rx_claim();
    /* req[0] = report ID, req[1..31] = payload */
    if (req[0] != REPORT_ID_SMK84_CONFIG) {
        config_rx_release();
        diag_malformed++;
        return;
    }

    /* Exact-request cache hit? (audit, spec §7.6) — identical full
     * request only; a reused txid with different contents is handled
     * as a fresh request below. */
    const uint8_t *cached;
    uint8_t cached_len;
    if (config_cache_get(req + 1, CONFIG_REPORT_DATA_SIZE,
                         &cached, &cached_len)) {
        /* replay */
        for (uint8_t i = 0; i < cached_len; i++) {
            config_tx.data[i] = cached[i];
        }
        config_tx.length = cached_len;
        config_rx_release();
        return;
    }

    /* remember the request for the response cache */
    for (uint8_t i = 0; i < CONFIG_REPORT_DATA_SIZE; i++) {
        config_last_req[i] = req[i + 1];
    }

    handle_request(req + 1);
    config_rx_release();
}

#endif /* SMK_CONFIG_PROTOCOL */
