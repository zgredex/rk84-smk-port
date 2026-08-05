/*
 * RK84 dynamic keymap — runtime-editable keymap (configurator M2).
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
 */
#include "kbdef.h"
#include "dynamic_keymap.h"
#include "keycodes.h"
#include "layout.h"   /* keymaps[][][] compiled default fallback */

#if SMK_DYNAMIC_KEYMAP

/* The runtime map: [layer][row][col] -> 16-bit SMK keycode. */
static __xdata uint16_t dynamic_keymap
    [RK84_DYNAMIC_LAYERS][RK84_MATRIX_ROWS][RK84_MATRIX_COLS];

/* Valid flag: set after a successful load; cleared by set() until the
 * caller validates and confirms, or by a failed boot load. */
static __bit dynamic_keymap_valid;

/* Fn position is LOCKED in protocol v1 (spec §8.6): matrix row 5,
 * column 9. The configurator cannot remap it. */
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

/* Bounds + allowlist validation for a single keycode. Returns true
 * when the code may be stored. Mirrors the host-side catalogue:
 * basic usages, modifiers, function keys, navigation, Consumer,
 * System, transparent, no-key, MO(1), RK84 RGB custom controls. */
bool dynamic_keymap_keycode_allowed(uint16_t keycode)
{
    /* SDCC 4.6 error-110 workaround: avoid chained uint16 range
     * comparisons (optimizer bug). Sequential, single-test blocks. */
    if (keycode == KC_NO) {
        return true;
    }
    if (keycode == KC_TRANSPARENT) {
        return true;
    }
    if (keycode == MO(1)) {
        return true;
    }
    if (keycode <= 0x00FFu) {
        /* QK_BASIC range (SMK keycodes.h). */
        return true;
    }
    if (keycode < 0x0300u) {
        /* Consumer (0x0100..0x01FF) + System (0x0200..0x02FF). */
        return true;
    }
    if (keycode >= RGB_BRI_UP) {
        if (keycode <= RGB_BRI_DN) {
            /* RK84 RGB custom controls (kbdef.h). */
            return true;
        }
    }
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
        return CFG_STATUS_BAD_KEYCODE;
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

/* Mark the runtime map valid after a staged bulk load validated
 * every cell. The config protocol calls this in APPLY_STAGE. */
void dynamic_keymap_activate(void)
{
    dynamic_keymap_valid = true;
}

/* Invalidate (fall back to compiled map). */
void dynamic_keymap_deactivate(void)
{
    dynamic_keymap_valid = false;
}

/* Framework hook (called by matrix.c resolve_keycode when
 * SMK_DYNAMIC_KEYMAP is defined). Returns the dynamic keycode, or
 * 0xFFFF so the caller falls back to the compiled keymaps[][][]. */
uint16_t dynamic_keymap_resolve(uint8_t layer, uint8_t row, uint8_t col)
{
    if (!dynamic_keymap_valid) {
        return 0xFFFFu;
    }
    if (layer >= RK84_DYNAMIC_LAYERS ||
        row >= RK84_MATRIX_ROWS ||
        col >= RK84_MATRIX_COLS) {
        return 0xFFFFu;
    }
    return dynamic_keymap[layer][row][col];
}

#endif /* SMK_DYNAMIC_KEYMAP */
