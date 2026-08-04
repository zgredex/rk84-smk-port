#include "report.h"
#include "host.h"
#include "layout.h"
#include "keycodes.h"
#include <string.h>
#include "kb.h"
#include "usb.h"
#include "keyboard.h"
#include "debug.h"

static uint8_t real_mods = 0;
static uint8_t weak_mods = 0;

__xdata report_keyboard_t keyboard_report;
__xdata report_keyboard_t last_report;

__xdata report_nkro_t nkro_report;
__xdata report_nkro_t last_nkro_report;

uint8_t biton(uint8_t bits);

void send_6kro_report();
#ifdef NKRO_ENABLE
void send_nkro_report();
#endif

/** \brief Send keyboard report
 *
 * Stock RK84 sends BOTH reports (EP1 boot 6KRO + EP2 NKRO ID 6)
 * simultaneously. The protocol state does not select one or the other.
 */
void send_keyboard_report()
{
    send_6kro_report();

#ifdef NKRO_ENABLE
    send_nkro_report();
#endif
}

void send_6kro_report()
{
    keyboard_report.mods = real_mods;
    keyboard_report.mods |= weak_mods;

    /* Only send the report if there are changes to propagate to the host. */
    if (memcmp(&keyboard_report, &last_report, sizeof(report_keyboard_t)) != 0) {
        memcpy(&last_report, &keyboard_report, sizeof(report_keyboard_t));
        host_keyboard_send(&keyboard_report);
    }
}

#ifdef NKRO_ENABLE
void send_nkro_report()
{
    /* Stock report ID 6 has NO modifier byte — modifiers stay on EP1. */
    nkro_report.report_id = REPORT_ID_NKRO;

    /* Only send the report if there are changes to propagate to the host. */
    if (memcmp(&nkro_report, &last_nkro_report, sizeof(report_nkro_t)) != 0) {
        memcpy(&last_nkro_report, &nkro_report, sizeof(report_nkro_t));
        host_nkro_send(&nkro_report);
    }
}
#endif

/** \brief has_anykey
 *
 */
uint8_t has_anykey(report_keyboard_t *keyboard_report)
{
    uint8_t  cnt = 0;
    uint8_t *p   = keyboard_report->keys;
    uint8_t  lp  = sizeof(keyboard_report->keys);

#ifdef NKRO_ENABLE
    /* Both report states are maintained simultaneously. */
    p  = nkro_report.bits;
    lp = sizeof(nkro_report.bits);
#endif

    while (lp--) {
        if (*p++) {
            cnt++;
        }
    }

    return cnt;
}

/** \brief get_first_key
 *
 * FIXME: Needs doc
 */
uint8_t get_first_key(report_keyboard_t *keyboard_report)
{
#ifdef NKRO_ENABLE
    {
        uint8_t i = 0;
        (void)keyboard_report;
        for (; i < NKRO_REPORT_BITS && !nkro_report.bits[i]; i++)
            ;
        /* bit index -> usage: add NKRO_FIRST_USAGE back */
        return (uint8_t)((i << 3 | biton(nkro_report.bits[i])) +
                         NKRO_FIRST_USAGE);
    }
#else
    return keyboard_report->keys[0];
#endif
}

/** \brief Checks if a key is pressed in the report
 *
 * Returns true if the keyboard_report reports that the key is pressed, otherwise false
 * Note: The function doesn't support modifers currently, and it returns false for KC_NO
 */
bool is_key_pressed(report_keyboard_t *keyboard_report, uint8_t key)
{
    if (key == KC_NO) {
        return false;
    }

#ifdef NKRO_ENABLE
    if (key >= NKRO_FIRST_USAGE) {
        uint8_t index = (uint8_t)(key - NKRO_FIRST_USAGE);
        if ((index >> 3) < NKRO_REPORT_BITS) {
            return nkro_report.bits[index >> 3] & 1 << (index & 7);
        } else {
            return false;
        }
    }
#endif

    for (int i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (keyboard_report->keys[i] == key) {
            return true;
        }
    }

    return false;
}

/** \brief add key byte
 *
 */
void add_key_byte(report_keyboard_t *keyboard_report, uint8_t code)
{
    int8_t i     = 0;
    int8_t empty = -1;

    for (; i < KEYBOARD_REPORT_KEYS; i++) {
        if (keyboard_report->keys[i] == code) {
            break;
        }

        if (empty == -1 && keyboard_report->keys[i] == 0) {
            empty = i;
        }
    }

    if (i == KEYBOARD_REPORT_KEYS) {
        if (empty != -1) {
            keyboard_report->keys[empty] = code;
        }
    }
}

/** \brief del key byte
 *
 */
void del_key_byte(report_keyboard_t *keyboard_report, uint8_t code)
{
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (keyboard_report->keys[i] == code) {
            keyboard_report->keys[i] = 0;
        }
    }
}

#ifdef NKRO_ENABLE
/** \brief add key bit (stock format: bit (usage - 0x04), 15 bytes) */
void add_key_bit(report_nkro_t *nkro_report, uint8_t code)
{
    uint8_t index;

    if (code < NKRO_FIRST_USAGE) {
        return;
    }

    index = (uint8_t)(code - NKRO_FIRST_USAGE);

    if ((index >> 3) < NKRO_REPORT_BITS) {
        nkro_report->bits[index >> 3] |= 1 << (index & 7);
    } else {
        dprintf("add_key_bit: can't add: %02X\n", code);
    }
}

/** \brief del key bit (stock format: bit (usage - 0x04), 15 bytes) */
void del_key_bit(report_nkro_t *nkro_report, uint8_t code)
{
    uint8_t index;

    if (code < NKRO_FIRST_USAGE) {
        return;
    }

    index = (uint8_t)(code - NKRO_FIRST_USAGE);

    if ((index >> 3) < NKRO_REPORT_BITS) {
        nkro_report->bits[index >> 3] &= ~(1 << (index & 7));
    } else {
        dprintf("del_key_bit: can't del: %02X\n", code);
    }
}
#endif

/** \brief add key to report
 *
 * Stock RK84 maintains BOTH report states simultaneously.
 */
void add_key_to_report(report_keyboard_t *keyboard_report, uint8_t key)
{
    add_key_byte(keyboard_report, key);

#ifdef NKRO_ENABLE
    add_key_bit(&nkro_report, key);
#endif
}

/** \brief del key from report
 *
 */
void del_key_from_report(report_keyboard_t *keyboard_report, uint8_t key)
{
    del_key_byte(keyboard_report, key);

#ifdef NKRO_ENABLE
    del_key_bit(&nkro_report, key);
#endif
}

/** \brief clear key from report
 *
 */
void clear_keys_from_report(report_keyboard_t *keyboard_report)
{
    memset(keyboard_report->keys, 0, sizeof(keyboard_report->keys));

#ifdef NKRO_ENABLE
    memset(nkro_report.bits, 0, sizeof(nkro_report.bits));
#endif
}

/** \brief Get mods
 *
 */
uint8_t get_mods(void)
{
    return real_mods;
}
/** \brief add mods
 *
 */
void add_mods(uint8_t mods)
{
    real_mods |= mods;
}
/** \brief del mods
 *
 */
void del_mods(uint8_t mods)
{
    real_mods &= ~mods;
}
/** \brief set mods
 *
 */
void set_mods(uint8_t mods)
{
    real_mods = mods;
}
/** \brief clear mods
 *
 */
void clear_mods(void)
{
    real_mods = 0;
}

/** \brief get weak mods
 *
 */
uint8_t get_weak_mods(void)
{
    return weak_mods;
}

// most significant on-bit - return highest location of on-bit
// NOTE: return 0 when bit0 is on or all bits are off
uint8_t biton(uint8_t bits)
{
    uint8_t n = 0;
    if (bits >> 4) {
        bits >>= 4;
        n += 4;
    }
    if (bits >> 2) {
        bits >>= 2;
        n += 2;
    }
    if (bits >> 1) {
        bits >>= 1;
        n += 1;
    }
    return n;
}
