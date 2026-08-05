/* Host harness: the REAL usb.c EP0 IN wrapper (P5 audit).
 *
 * Compiles the ACTUAL `step_ep0_in_xfer()` from src/platform/sh68f90a/
 * usb.c (extracted verbatim) against stubbed EP0 registers and
 * buffers, and drives it through the shared ep0_xfer_next() core.
 * This closes the N4/P5 gap: the harness exercises the real wrapper's
 * enum-to-state mapping, SET_EP0_CNT call, buffer copy, and
 * source/remaining accounting — not a hand-written mirror.
 *
 * The extraction is deliberate: usb.c is a full USB stack (ISRs,
 * handlers, descriptors) that cannot be linked wholesale into a host
 * binary. We take ONLY the wrapper function + its dependencies.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- EP0 register/buffer stubs (what the wrapper touches) ---- */
#define EP0_BUF_SIZE 8u
static uint8_t ep0_in_buf[EP0_BUF_SIZE];   /* EP0_IN_BUF */
static uint8_t iep0cnt;                    /* IEP0CNT (lower nibble) */

/* --- macros the wrapper expands (from sh68f90a.h / usbregs.h) --- */
#define EP0_IN_BUF  ep0_in_buf
#define IEP0CNT     iep0cnt
#define SET_EP0_CNT(COUNT) \
    do { CLEAR_EP0_CNT; IEP0CNT |= (COUNT); } while (0)
#define CLEAR_EP0_CNT \
    do { IEP0CNT &= ~0x0fu; } while (0)

/* ---- usb_ep0_state_t enum (verbatim from usb.c) ---- */
typedef enum {
    USB_EP0_STATE_DEFAULT     = 0x00,
    USB_EP0_STATE_IN_DATA     = 0x01,
    USB_EP0_STATE_RECV_STATUS = 0x02,
    USB_EP0_STATE_LED         = 0x04,
    USB_EP0_STATE_ISP         = 0x05,
} usb_ep0_state_t;
usb_ep0_state_t usb_ep0_state;

/* ---- transfer globals the wrapper uses (from usb.c) ---- */
uint16_t ep0_xfer_bytes_left;
uint8_t *ep0_xfer_src;

/* ---- the shared core + validators (config_protocol.c) ---- */
#ifndef __xdata
#define __xdata
#endif
#include "config_protocol.h"
#include "config_protocol.c"
#include "dynamic_keymap.c"

/* Compiled default keymap (stub) — dynamic_keymap.c needs it. */
const uint16_t keymaps[2][6][16] = { 0 };

/* forward decl (the real wrapper below calls it) */
static void set_ep0_in_buffer(uint8_t *src, uint8_t len);

static void setup_ep0_in_xfer(uint8_t *src, uint16_t len)
{
    ep0_xfer_src = src;
    ep0_xfer_bytes_left = len;
}

/* The REAL wrapper — extracted verbatim from usb.c. This is the
 * production code path used by the firmware's GET_REPORT handler. */
static void step_ep0_in_xfer(void)
{
#if SMK_CONFIG_PROTOCOL
    /* N4 (audit): the packetization decisions live in the shared
     * ep0_xfer_next() (config_protocol.c) — the harness compiles the
     * SAME code, so the state machine can never drift. The packet
     * buffer + xfer state live in XRAM: the DSEG overlay is full. */
    static __xdata uint8_t packet[EP0_BUF_SIZE];
    static __xdata uint8_t len;
    __xdata ep0_xfer_t xfer;
    xfer.src = ep0_xfer_src;
    xfer.remaining = ep0_xfer_bytes_left;

    ep0_next_state_t next = ep0_xfer_next(&xfer, packet, &len);

    ep0_xfer_src = (uint8_t *)xfer.src;
    ep0_xfer_bytes_left = xfer.remaining;

    set_ep0_in_buffer(packet, len);
    SET_EP0_CNT(len);

    usb_ep0_state = (next == EP0_NEXT_DATA)
                        ? USB_EP0_STATE_IN_DATA
                        : USB_EP0_STATE_RECV_STATUS;
#else
    /* Baseline (no config protocol): original inline packetization. */
    if (ep0_xfer_bytes_left == 0) {
        usb_ep0_state = USB_EP0_STATE_RECV_STATUS;
        SET_EP0_CNT(0);
    } else if (ep0_xfer_bytes_left > EP0_BUF_SIZE) {
        usb_ep0_state = USB_EP0_STATE_IN_DATA;
        set_ep0_in_buffer(ep0_xfer_src, EP0_BUF_SIZE);
        ep0_xfer_bytes_left -= EP0_BUF_SIZE;
        ep0_xfer_src += EP0_BUF_SIZE;
        SET_EP0_CNT(EP0_BUF_SIZE);
    } else if (ep0_xfer_bytes_left == EP0_BUF_SIZE) {
        usb_ep0_state = USB_EP0_STATE_IN_DATA;
        set_ep0_in_buffer(ep0_xfer_src, EP0_BUF_SIZE);
        ep0_xfer_bytes_left = 0;
        SET_EP0_CNT(EP0_BUF_SIZE);
    } else {
        usb_ep0_state = USB_EP0_STATE_RECV_STATUS;
        set_ep0_in_buffer(ep0_xfer_src, ep0_xfer_bytes_left);
        SET_EP0_CNT(ep0_xfer_bytes_left);
    }
#endif
}

/* set_ep0_in_buffer from usb.c (verbatim). */
static void set_ep0_in_buffer(uint8_t *src, uint8_t len)
{
    if (len > EP0_BUF_SIZE) {
        return; /* never happens; guards the memcpy without logging */
    }
    memcpy(ep0_in_buf, src, len);
}

/* ---- tests ---- */

static int checks, failures;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        checks++;                                                     \
        if (!(cond)) {                                                \
            failures++;                                               \
            printf("FAIL: %s\n", msg);                                \
        }                                                             \
    } while (0)

static void test_32_byte_transfer(void)
{
    uint8_t resp[32];
    for (uint8_t i = 0; i < 32; i++) resp[i] = (uint8_t)(0xA0 + i);
    memset(ep0_in_buf, 0, EP0_BUF_SIZE);
    iep0cnt = 0;
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    setup_ep0_in_xfer(resp, 32);
    step_ep0_in_xfer();

    /* wrapper must copy the first packet into EP0_IN_BUF, arm the
     * count, and map EP0_NEXT_DATA -> USB_EP0_STATE_IN_DATA */
    CHECK(iep0cnt == 8, "SET_EP0_CNT(8) armed after first packet");
    CHECK(memcmp(ep0_in_buf, resp, 8) == 0,
          "first 8 bytes copied into EP0_IN_BUF");
    CHECK(usb_ep0_state == USB_EP0_STATE_IN_DATA,
          "32-byte transfer maps to IN_DATA");
    CHECK(ep0_xfer_bytes_left == 24, "24 bytes remain");

    /* second packet */
    step_ep0_in_xfer();
    CHECK(iep0cnt == 8, "second packet count 8");
    CHECK(memcmp(ep0_in_buf, resp + 8, 8) == 0,
          "bytes 8..15 copied");
    CHECK(ep0_xfer_bytes_left == 16, "16 bytes remain");

    /* third */
    step_ep0_in_xfer();
    CHECK(ep0_xfer_bytes_left == 8, "8 bytes remain");

    /* fourth (exactly 8) -> still IN_DATA, then exhausted */
    step_ep0_in_xfer();
    CHECK(iep0cnt == 8, "final 8-byte packet armed");
    CHECK(usb_ep0_state == USB_EP0_STATE_IN_DATA,
          "exact-8 final packet still IN_DATA");
    CHECK(ep0_xfer_bytes_left == 0, "zero bytes remain");

    /* step again: nothing left -> RECV_STATUS, count 0 */
    step_ep0_in_xfer();
    CHECK(usb_ep0_state == USB_EP0_STATE_RECV_STATUS,
          "exhausted transfer -> RECV_STATUS");
    CHECK(iep0cnt == 0, "exhausted transfer arms count 0");
}

static void test_short_read(void)
{
    uint8_t resp[32];
    for (uint8_t i = 0; i < 32; i++) resp[i] = (uint8_t)(0x10 + i);
    memset(ep0_in_buf, 0, EP0_BUF_SIZE);
    iep0cnt = 0;
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    setup_ep0_in_xfer(resp, 3);
    step_ep0_in_xfer();

    CHECK(usb_ep0_state == USB_EP0_STATE_RECV_STATUS,
          "short read -> RECV_STATUS (no overwrite)");
    CHECK(iep0cnt == 3, "short read arms count 3");
    CHECK(memcmp(ep0_in_buf, resp, 3) == 0, "3 bytes copied");
}

static void test_zero_length(void)
{
    memset(ep0_in_buf, 0x55, EP0_BUF_SIZE);
    iep0cnt = 0xFF;
    usb_ep0_state = USB_EP0_STATE_DEFAULT;

    setup_ep0_in_xfer(NULL, 0);
    step_ep0_in_xfer();

    CHECK(usb_ep0_state == USB_EP0_STATE_RECV_STATUS,
          "zero-length -> RECV_STATUS");
    CHECK((iep0cnt & 0x0f) == 0, "zero-length arms count 0");
    CHECK(ep0_in_buf[0] == 0x55, "zero-length leaves buffer untouched");
}

int main(void)
{
    printf("harness_usb_ep0: real usb.c wrapper + shared core\n");
    test_32_byte_transfer();
    test_short_read();
    test_zero_length();
    printf("---------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
