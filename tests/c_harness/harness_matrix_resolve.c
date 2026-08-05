/* Host harness: matrix-layer resolution integration (configurator M3
 * audit F1). Compiles the REAL dynamic_keymap.c + the REAL matrix.c
 * resolve_keycode() path against stubs and verifies the KC_TRANSPARENT
 * fallback that was broken in M2:
 *
 *   Fn+A with transparent Fn-layer A  -> KC_A (base key)
 *   Fn+F8 with Fn-layer F8 = media    -> media key
 *   Fn release while layer 1 active   -> releases MO(1)
 *   unmodified base-layer key         -> its own keycode
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "harness_stubs.h"

/* action_layer lives in matrix.c; the harness owns it for the test.
 * Declared BEFORE the includes that reference it. */
uint8_t action_layer;

#include "dynamic_keymap.c"
#include "matrix_resolve.c"

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

/* Stub compiled keymap: base layer row0 = A..P (0x04..0x13), Fn layer
 * row0 = F8(media 0x00AB) at col 8, everything else KC_TRANSPARENT.
 * Fn position (5,9) = MO(1) on base, transparent on Fn layer. */
const uint16_t keymaps[2][MATRIX_ROWS][MATRIX_COLS] = {
    /* layer 0 */
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
    /* layer 1 (Fn) — almost all KC_TRANSPARENT */
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

int main(void)
{
    printf("RK84 matrix-resolution integration harness\n");
    printf("-------------------------------------------\n");

    /* Load defaults (copies compiled map into dynamic map). */
    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_is_active(), "dynamic map active after defaults");

    /* --- Fn + A: Fn-layer A is KC_TRANSPARENT -> KC_A (0x04) ------- */
    action_layer = 1;
    uint16_t k = resolve_keycode(0, 0); /* A at (row0,col0) */
    CHECK(k == 0x04, "Fn+A with transparent A -> KC_A (0x04)");
    CHECK(k != KC_TRANSPARENT, "Fn+A must NOT resolve to KC_TRANSPARENT");

    /* --- Fn + F8: Fn-layer F8 is media -> media key (0x00AB) ------- */
    k = resolve_keycode(0, 8); /* F8 at (row0,col8) */
    CHECK(k == 0x00AB, "Fn+F8 with media F8 -> media 0x00AB");

    /* --- base layer (no Fn): A -> KC_A, F8 -> KC_F8 ---------------- */
    action_layer = 0;
    k = resolve_keycode(0, 0);
    CHECK(k == 0x04, "base A -> KC_A (0x04)");
    k = resolve_keycode(0, 8);
    CHECK(k == 0x0C, "base F8 -> KC_F8 (0x0C)");

    /* --- Fn held + unmodified key on another row -------------------- */
    action_layer = 1;
    k = resolve_keycode(2, 3); /* (row2,col3) transparent on Fn layer */
    CHECK(k == 0x27, "Fn + (2,3) transparent -> base 0x27");

    /* --- Fn release: momentary resolves to MO(1) even on Fn layer --- */
    k = resolve_keycode(5, 9); /* Fn position itself */
    CHECK(k == MO(1), "Fn key resolves to MO(1) while layer active");

    /* --- dynamic edit: change base A to KC_B, verify live ----------- */
    action_layer = 0;
    CHECK(dynamic_keymap_set(0, 0, 0, 0x05) == CFG_STATUS_OK,
          "set base A -> KC_B");
    k = resolve_keycode(0, 0);
    CHECK(k == 0x05, "base A now KC_B (0x05)");
    action_layer = 1;
    k = resolve_keycode(0, 0); /* Fn+A, A now B on base, Fn-layer transparent */
    CHECK(k == 0x05, "Fn+A (transparent) falls through to edited base B");

    printf("-------------------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
