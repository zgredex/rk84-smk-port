/* Host-harness copy of matrix.c's resolve_keycode() — the REAL logic
 * from src/smk/matrix.c, including the SMK_DYNAMIC_KEYMAP hook. Kept
 * in sync manually; the integration harness verifies the resolution
 * contract (KC_TRANSPARENT fallback etc.) that the dynamic map must
 * satisfy. */
#ifndef HARNESS_MATRIX_RESOLVE_C
#define HARNESS_MATRIX_RESOLVE_C

#include "matrix.h"
#include "keycodes.h"
#include "layout.h"
#if SMK_DYNAMIC_KEYMAP
#include "dynamic_keymap.h"
#endif

/* resolve_keycode: identical to src/smk/matrix.c. */
uint16_t resolve_keycode(uint8_t row, uint8_t col)
{
    uint16_t qcode = keymaps[0][row][col];

    /* A base-layer momentary key must remain momentary even while
     * another layer is active. */
    if (IS_QK_MOMENTARY(qcode)) {
        return qcode;
    }

    if (action_layer) {
        uint16_t layer_code = keymaps[action_layer][row][col];

        if (layer_code != KC_TRANSPARENT) {
            qcode = layer_code;
        }
    }

#if SMK_DYNAMIC_KEYMAP
    /* When the runtime map is active it REPLACES compiled resolution
     * entirely — dynamic_keymap_resolve() performs the full base-layer
     * + KC_TRANSPARENT fallback itself. */
    if (dynamic_keymap_is_active()) {
        uint16_t dyn = dynamic_keymap_resolve(action_layer, row, col);
        if (dyn != 0xFFFFu) {
            return dyn;
        }
    }
#endif

    return qcode;
}

#endif /* HARNESS_MATRIX_RESOLVE_C */
