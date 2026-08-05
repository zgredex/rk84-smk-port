/*
 * RK84 configuration protocol — header (configurator M3).
 *
 * Mirrors configurator/src/protocol/constants.ts (M1). Wire format
 * MUST stay byte-identical between host and firmware.
 *
 * Spec: SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md §6/§7.
 */
#ifndef RK84_CONFIG_PROTOCOL_H
#define RK84_CONFIG_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* ---- report ------------------------------------------------------- */

#define REPORT_ID_SMK84_CONFIG 8u
#define CONFIG_REPORT_DATA_SIZE 31u   /* payload after report ID byte */
#define CONFIG_REPORT_TOTAL_SIZE 32u  /* report ID + payload */
#define CONFIG_PAYLOAD_MAX 24u

/* ---- commands (spec §7.4) ------------------------------------------ */

enum config_command {
    CFG_CMD_GET_PROTOCOL_INFO    = 0x01,
    CFG_CMD_GET_DEVICE_INFO      = 0x02,
    CFG_CMD_GET_CAPABILITIES     = 0x03,
    CFG_CMD_GET_STATUS           = 0x04,

    CFG_CMD_READ_OBJECT          = 0x10,
    CFG_CMD_BEGIN_STAGE          = 0x11,
    CFG_CMD_WRITE_CHUNK          = 0x12,
    CFG_CMD_VALIDATE_STAGE       = 0x13,
    CFG_CMD_APPLY_STAGE          = 0x14,
    CFG_CMD_COMMIT_STAGE         = 0x15,
    CFG_CMD_ABORT_STAGE          = 0x16,
    CFG_CMD_RESET_DEFAULTS       = 0x17,

    CFG_CMD_GET_DIAGNOSTICS      = 0x20,

    CFG_CMD_ARM_BOOTLOADER       = 0x70,
    CFG_CMD_ENTER_BOOTLOADER     = 0x71,
};

#define CFG_CMD_RESPONSE_BIT 0x80u

/* ---- status (spec §7.3) --------------------------------------------
 * EXPLICIT wire values, byte-identical with the TypeScript enum in
 * configurator/src/protocol/constants.ts. Do not renumber. */

typedef enum {
    CFG_STATUS_OK                  = 0,
    CFG_STATUS_BAD_COMMAND         = 1,
    CFG_STATUS_BAD_VERSION         = 2,
    CFG_STATUS_BAD_LENGTH          = 3,
    CFG_STATUS_BAD_OFFSET          = 4,
    CFG_STATUS_BAD_OBJECT          = 5,
    CFG_STATUS_BAD_KEYCODE         = 6,
    CFG_STATUS_BAD_ANIMATION       = 7,
    CFG_STATUS_STACK_OVERFLOW      = 8,
    CFG_STATUS_BUDGET_EXCEEDED     = 9,
    CFG_STATUS_BUSY                = 10,
    CFG_STATUS_KEYS_HELD           = 11,
    CFG_STATUS_NOT_STAGED          = 12,
    CFG_STATUS_CRC_MISMATCH        = 13,
    CFG_STATUS_FLASH_VERIFY_FAILED = 14,
    CFG_STATUS_PROTECTED_ADDRESS   = 15,
    CFG_STATUS_NOT_SUPPORTED       = 16,
    CFG_STATUS_INTERNAL_ERROR      = 17,
} config_status_t;

/* ---- objects (spec §7.5) -------------------------------------------- */

enum config_object {
    CFG_OBJECT_KEYMAP            = 0x01,
    CFG_OBJECT_RGB_CONFIG        = 0x02,
    CFG_OBJECT_RGB_STATIC        = 0x03,
    CFG_OBJECT_ANIMATION_PROGRAM = 0x04,
    CFG_OBJECT_ANIMATION_PARAMS  = 0x05,

    CFG_OBJECT_DEVICE_INFO       = 0x80,
    CFG_OBJECT_LED_MAP           = 0x81,
    CFG_OBJECT_DIAGNOSTICS       = 0x82,
};

/* ---- object sizes (firmware-enforced) ------------------------------- */

#define CFG_OBJECT_KEYMAP_SIZE        384u  /* 2*6*16*2 */
#define CFG_OBJECT_RGB_CONFIG_SIZE    32u
#define CFG_OBJECT_RGB_STATIC_SIZE    378u  /* 3*126 */
#define CFG_OBJECT_ANIMATION_SIZE     512u
#define CFG_OBJECT_ANIMATION_PARAMS   64u

/* ---- mailbox (spec §6.3) -------------------------------------------- */

#define CONFIG_MAILBOX_SIZE CONFIG_REPORT_TOTAL_SIZE

/* One generic staging buffer for object uploads (spec §15.4/M4):
 * BEGIN_STAGE copies the active object, WRITE_CHUNK mutates only the
 * stage, VALIDATE checks it, APPLY_STAGE copies it into the active
 * map, ABORT_STAGE discards it. The live map is untouched until a
 * validated APPLY. 512 B covers the largest object (keymap 384 B;
 * animation 512 B in M7). */
#define CONFIG_STAGE_MAX 512u

typedef struct {
    uint8_t data[CONFIG_MAILBOX_SIZE]; /* report ID + 31 payload */
    uint8_t length;
    uint8_t pending;
} config_mailbox_t;

typedef struct {
    uint16_t length;
    uint8_t  object;
    uint8_t  active;     /* stage in progress */
    uint8_t  validated;  /* stage passed VALIDATE_STAGE */
} config_stage_t;

/* The stage buffer itself lives at file scope in config_protocol.c as
 * a plain __xdata array (SDCC cannot put xdata arrays inside structs). */
#define CONFIG_STAGE_BUF_SIZE CONFIG_STAGE_MAX

/* ---- API ------------------------------------------------------------ */

/* Called from the USB ISR (usb.c) when the SET_REPORT handler accepts
 * a config report packet; appends up to 8 bytes into the RX mailbox.
 * Returns true when the full 32-byte report has arrived. */
bool config_rx_append(const uint8_t *pkt, uint8_t len);

/* True once a complete report is pending in the RX mailbox. */
bool config_rx_pending(void);

/* Claim the RX mailbox (main loop); returns a pointer to the data.
 * Caller must then call config_rx_release(). */
uint8_t *config_rx_claim(void);

/* Release the RX mailbox after processing. */
void config_rx_release(void);

/* Called by GET_REPORT (usb.c): returns pointer+len of the prepared
 * response (report ID + payload) to send, or NULL if none. */
const uint8_t *config_tx_get(uint8_t *len);

/* Process one pending request (main loop; never ISR). */
void config_protocol_task(void);

/* Cached-response semantics (spec §7.6): the last completed response
 * may be replayed ONLY for an identical full request. */
void config_cache_set(const uint8_t *resp, uint8_t len,
                      const uint8_t *req_payload, uint8_t req_len);
bool config_cache_get(const uint8_t *req_payload, uint8_t req_len,
                      const uint8_t **resp, uint8_t *len);

#endif /* RK84_CONFIG_PROTOCOL_H */
