/* Compiled-C harness for the RK84 report state machine.
 *
 * Compiles the REAL smk/report.c (verbatim, RK84_STOCK_REPORTS +
 * NKRO_ENABLE) against stub headers, so the actual C code — not a
 * Python mirror — is exercised. This is the final offline gate the
 * audit requested: the bitmap self-clear (bug 2) would have been
 * caught here immediately.
 *
 * Build (from repo root):
 *   cc -std=c99 -Wall -Wextra -Werror -O0 \
 *      -DRK84_STOCK_REPORTS=1 -DNKRO_ENABLE=1 -DDEBUG=0 \
 *      -Itests/c_harness/stubs \
 *      -Isrc/keyboards/royalkludge-rk84 -Isrc/smk \
 *      tests/c_harness/harness_report.c src/smk/report.c \
 *      -o /tmp/rk84_report_harness
 *   /tmp/rk84_report_harness
 */
#include "report.h"

#include <stdio.h>
#include <string.h>

/* ---- captured sends (host.c stubs) ---- */
static report_keyboard_t last_sent_kbd;
static int              kbd_send_count;
static report_nkro_t    last_sent_nkro;
static int              nkro_send_count;

void host_keyboard_send(__xdata report_keyboard_t *report)
{
    memcpy(&last_sent_kbd, report, sizeof(last_sent_kbd));
    kbd_send_count++;
}

void host_nkro_send(__xdata report_nkro_t *report)
{
    memcpy(&last_sent_nkro, report, sizeof(last_sent_nkro));
    nkro_send_count++;
}

/* ---- test helpers ---- */
static int failures;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                  \
            failures++;                                                     \
        } else {                                                            \
            printf("PASS: %s\n", msg);                                      \
        }                                                                   \
    } while (0)

static void reset_all(void)
{
    /* Public-API reset: clear keys/mods and poison the last-sent
     * caches so the next send transmits regardless. */
    clear_keys_from_report(&keyboard_report);
    clear_mods();
    report_force_resend();
    kbd_send_count = 0;
    nkro_send_count = 0;
    memset(&last_sent_kbd, 0, sizeof(last_sent_kbd));
    memset(&last_sent_nkro, 0, sizeof(last_sent_nkro));
}

/* The inline add_key/del_key wrappers in report.h don't emit symbols
 * at -O0; call the non-inline implementations directly. */
static void add_key_n(uint8_t key) { add_key_to_report(&keyboard_report, key); }
static void del_key_n(uint8_t key) { del_key_from_report(&keyboard_report, key); }

/* ---- tests ---- */

static void test_six_keys_fill_slots(void)
{
    reset_all();
    for (uint8_t u = 0x04; u < 0x0A; u++) {
        add_key_n(u);
    }
    send_keyboard_report();
    CHECK(kbd_send_count == 1, "six keys -> one report");
    CHECK(last_sent_kbd.keys[0] == 0x04 && last_sent_kbd.keys[5] == 0x09,
          "six slots filled A..F");
}

static void test_seventh_key_dropped_from_boot(void)
{
    reset_all();
    for (uint8_t u = 0x04; u < 0x0B; u++) {  /* A..G */
        add_key_n(u);
    }
    send_keyboard_report();
    CHECK(kbd_send_count == 1, "seven keys -> one report");
    /* boot slots hold A..F; G present in the NKRO bitmap */
    CHECK(last_sent_kbd.keys[5] == 0x09, "boot slots A..F");
    CHECK((nkro_report.bits[0] & (1 << 6)) != 0, "G set in NKRO bitmap");
    CHECK(last_sent_kbd.keys[0] != 0x0A, "G not in boot slots");
}

static void test_release_promotes_seventh(void)
{
    reset_all();
    for (uint8_t u = 0x04; u < 0x0B; u++) {  /* A..G */
        add_key_n(u);
    }
    send_keyboard_report();
    del_key_n(0x04);  /* release A -> G must be promoted */
    send_keyboard_report();
    CHECK(kbd_send_count == 2, "promotion sends a second report");
    CHECK(last_sent_kbd.keys[0] == 0x05, "B moved to slot 0");
    CHECK(last_sent_kbd.keys[5] == 0x0A, "G promoted into slot 5");
    /* NKRO bitmap must NOT be wiped: B..G still held */
    CHECK((nkro_report.bits[0] & 0x7E) == 0x7E,
          "NKRO bitmap survives rebuild (bug-2 regression)");
}

static void test_release_all_clean(void)
{
    reset_all();
    for (uint8_t u = 0x04; u < 0x0B; u++) {
        add_key_n(u);
    }
    send_keyboard_report();
    for (uint8_t u = 0x04; u < 0x0B; u++) {
        del_key_n(u);
    }
    send_keyboard_report();
    CHECK(kbd_send_count == 2, "all-released sends final report");
    CHECK(last_sent_kbd.mods == 0, "mods zero");
    int zero = 1;
    for (int i = 0; i < 6; i++) {
        zero = zero && (last_sent_kbd.keys[i] == 0);
    }
    CHECK(zero, "all key slots zero");
    CHECK((nkro_report.bits[0] == 0), "NKRO bitmap empty");
}

static void test_force_resend_transmits_identical_state(void)
{
    reset_all();
    add_key_n(0x04);
    send_keyboard_report();
    int first_count = kbd_send_count;
    /* identical state: duplicate suppression would skip */
    send_keyboard_report();
    CHECK(kbd_send_count == first_count, "duplicate suppressed");
    /* force resend: poisons last_report so next send transmits */
    report_force_resend();
    send_keyboard_report();
    CHECK(kbd_send_count == first_count + 1,
          "force_resend bypasses duplicate suppression");
}

static void test_rebuild_after_force_resend_sends_state(void)
{
    reset_all();
    add_key_n(0x04);          /* A held across resume */
    send_keyboard_report();
    report_force_resend();  /* what rk84_usb_resume_hook schedules */
    send_keyboard_report(); /* what the main-loop poll does */
    CHECK(kbd_send_count == 2, "resume resend transmits held key");
    CHECK(last_sent_kbd.keys[0] == 0x04, "held key re-delivered after resume");
}

int main(void)
{
    test_six_keys_fill_slots();
    test_seventh_key_dropped_from_boot();
    test_release_promotes_seventh();
    test_release_all_clean();
    test_force_resend_transmits_identical_state();
    test_rebuild_after_force_resend_sends_state();

    if (failures) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nALL C HARNESS TESTS PASS\n");
    return 0;
}
