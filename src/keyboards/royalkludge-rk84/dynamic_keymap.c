/*
 * RK84 dynamic keymap — runtime-editable keymap (configurator M2/M3).
 *
 * One complete two-layer map in XRAM (384 bytes). When a valid
 * profile is loaded (or defaults are copied in at boot), the matrix
 * resolver consults this map instead of the compiled keymaps[].
 *
 * Spec: SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md §8.
 *
 * Safety invariants (spec §31):
 *   - recovery chord stays PHYSICAL (never remappable);
 *   - compiled keymaps[] remain the permanent fallback;
 *   - a corrupt dynamic map degrades to compiled defaults;
 *   - no flash writes here (persistence is the M5 config store).
 *
 * Transparency (audit): resolve() implements layer fallback itself —
 * a KC_TRANSPARENT overlay falls through to the base layer, so Fn+A
 * with a transparent Fn-layer A still produces KC_A.
 */
#include "kbdef.h"
#include "dynamic_keymap.h"
#include "keycodes.h"
#include "layout.h"   /* keymaps[][][] compiled default fallback */

#if SMK_DYNAMIC_KEYMAP

/* The runtime map: [layer][row][col] -> 16-bit SMK keycode. */
static __xdata uint16_t dynamic_keymap
    [RK84_DYNAMIC_LAYERS][RK84_MATRIX_ROWS][RK84_MATRIX_COLS];

/* Valid flag: set after a successful load or activation. */
static __bit dynamic_keymap_valid;

/* Fn position is LOCKED in protocol v1 (spec §8.6): matrix row 5,
 * column 9. The configurator may only write the compiled value there
 * (idempotent full-map round-trips); anything else is rejected. */
#define RK84_FN_LOCKED_ROW 5u
#define RK84_FN_LOCKED_COL 9u

uint16_t dynamic_keymap_get(uint8_t layer, uint8_t row, uint8_t col)
{
    if (!dynamic_keymap_valid) {
        return 0xFFFFu; /* KC_NO-ish sentinel; callers fall back */
    }
    if (layer >= RK84_DYNAMIC_LAYERS ||
        row >= RK84_MATRIX_ROWS ||
        col >= RK84_MATRIX_COLS) {
        return 0xFFFFu;
    }
    return dynamic_keymap[layer][row][col];
}

bool dynamic_keymap_is_active(void)
{
    return dynamic_keymap_valid;
}

/* Allowlist (audit): use the REAL SMK predicates. System/Consumer/
 * modifiers live INSIDE the basic range (KC_SYSTEM_POWER=0xA5 etc.),
 * so the ranges must be the SMK predicates, not 0x0100..0x02FF.
 * Mouse keycodes stay rejected until a mouse report exists. */
bool dynamic_keymap_keycode_allowed(uint16_t keycode)
{
    if (keycode == KC_NO || keycode == KC_TRANSPARENT) {
        return true;
    }
    if (keycode == MO(1)) {
        return true;
    }
    if (keycode == RGB_BRI_UP || keycode == RGB_BRI_DN) {
        return true;
    }
    if (IS_BASIC_KEYCODE(keycode) ||
        IS_SYSTEM_KEYCODE(keycode) ||
        IS_CONSUMER_KEYCODE(keycode) ||
        IS_MODIFIER_KEYCODE(keycode)) {
        return true;
    }
    /* IS_INTERNAL covers KC_NO..KC_TRANSPARENT (already handled) and
     * anything else internal; reject mouse + QK_MODS combos. */
    return false;
}

bool dynamic_keymap_is_locked(uint8_t row, uint8_t col)
{
    return row == RK84_FN_LOCKED_ROW && col == RK84_FN_LOCKED_COL;
}

config_status_t dynamic_keymap_set(
    uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode)
{
    if (layer >= RK84_DYNAMIC_LAYERS ||
        row >= RK84_MATRIX_ROWS ||
        col >= RK84_MATRIX_COLS) {
        return CFG_STATUS_BAD_OFFSET;
    }
    if (dynamic_keymap_is_locked(row, col)) {
        /* Idempotent round-trip: the locked Fn cell accepts ONLY its
         * compiled value (layer 0 = MO(1), layer 1 = its fixed value).
         * A full-map upload therefore succeeds, but remapping Fn is
         * still impossible. */
        uint16_t required = keymaps[layer][row][col];
        if (keycode != required) {
            return CFG_STATUS_BAD_KEYCODE;
        }
        dynamic_keymap[layer][row][col] = keycode;
        return CFG_STATUS_OK;
    }
    if (!dynamic_keymap_keycode_allowed(keycode)) {
        return CFG_STATUS_BAD_KEYCODE;
    }
    dynamic_keymap[layer][row][col] = keycode;
    return CFG_STATUS_OK;
}

/* Copy compiled defaults into the runtime map (boot fallback). */
void dynamic_keymap_load_defaults(void)
{
    for (uint8_t layer = 0; layer < RK84_DYNAMIC_LAYERS; layer++) {
        for (uint8_t row = 0; row < RK84_MATRIX_ROWS; row++) {
            for (uint8_t col = 0; col < RK84_MATRIX_COLS; col++) {
                uint16_t code = keymaps[layer][row][col];
                dynamic_keymap[layer][row][col] = code;
            }
        }
    }
    dynamic_keymap_valid = true;
}

void dynamic_keymap_activate(void)
{
    dynamic_keymap_valid = true;
}

void dynamic_keymap_deactivate(void)
{
    dynamic_keymap_valid = false;
}

/* Framework hook (matrix.c resolve_keycode when SMK_DYNAMIC_KEYMAP).
 * Implements FULL layer fallback: base layer first, transparent
 * overlay falls through to base, momentary base keys stay resolvable.
 * Returns 0xFFFF only when inactive/out of range (caller falls back
 * to the compiled map). */
uint16_t dynamic_keymap_resolve(uint8_t layer, uint8_t row, uint8_t col)
{
    uint16_t base;
    uint16_t overlay;

    if (!dynamic_keymap_valid ||
        row >= RK84_MATRIX_ROWS ||
        col >= RK84_MATRIX_COLS) {
        return 0xFFFFu;
    }

    base = dynamic_keymap[0][row][col];

    /* A base-layer momentary key must remain momentary even while
     * another layer is active. */
    if (IS_QK_MOMENTARY(base)) {
        return base;
    }

    if (layer == 0 || layer >= RK84_DYNAMIC_LAYERS) {
        return base;
    }

    overlay = dynamic_keymap[layer][row][col];

    return overlay == KC_TRANSPARENT ? base : overlay;
}

#endif /* SMK_DYNAMIC_KEYMAP */
