/* Host harness for the RK84 dynamic keymap (configurator M2).
 *
 * Compiles the REAL board dynamic_keymap.c against stub headers so
 * the actual C code — validation, locked positions, fallback, boot
 * defaults — is tested on the host, not just a Python model.
 *
 * Spec §26 (dynamic keymap tests).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- stub framework headers pulled in via -include ---------------- */
#include "harness_stubs.h"

/* The real module under test. */
#include "dynamic_keymap.c"

/* ---- test helpers -------------------------------------------------- */

/* Compiled default keymap (stub): distinct values so tests can
 * verify the dynamic map actually copied them. */
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
          0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63 },
    },
    {
        { 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
          0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73 },
        { 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B,
          0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83 },
        { 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
          0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93 },
        { 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
          0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3 },
        { 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
          0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3 },
        { 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
          0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3 },
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

/* ---- tests --------------------------------------------------------- */

static void test_defaults_load(void)
{
    dynamic_keymap_deactivate();
    CHECK(dynamic_keymap_is_active() == false,
          "inactive before load");

    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_is_active() == true,
          "active after defaults load");

    /* defaults copied from keymaps[][] — spot-check a few positions */
    CHECK(dynamic_keymap_get(0, 0, 0) == keymaps[0][0][0],
          "base layer [0][0] matches compiled default");
    CHECK(dynamic_keymap_get(1, 4, 14) == keymaps[1][4][14],
          "fn layer [4][14] matches compiled default");
}

static void test_set_get(void)
{
    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_set(0, 2, 5, 0x28 /* KC_ENTER */) == CFG_STATUS_OK,
          "set valid keycode OK");
    CHECK(dynamic_keymap_get(0, 2, 5) == 0x28,
          "get returns stored keycode");
}

static void test_out_of_range(void)
{
    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_get(2, 0, 0) == 0xFFFFu,
          "get layer out of range -> 0xFFFF");
    CHECK(dynamic_keymap_get(0, 6, 0) == 0xFFFFu,
          "get row out of range -> 0xFFFF");
    CHECK(dynamic_keymap_get(0, 0, 16) == 0xFFFFu,
          "get col out of range -> 0xFFFF");
    CHECK(dynamic_keymap_set(2, 0, 0, 0x04) == CFG_STATUS_BAD_OFFSET,
          "set layer out of range rejected");
    CHECK(dynamic_keymap_set(0, 6, 0, 0x04) == CFG_STATUS_BAD_OFFSET,
          "set row out of range rejected");
    CHECK(dynamic_keymap_set(0, 0, 16, 0x04) == CFG_STATUS_BAD_OFFSET,
          "set col out of range rejected");
}

static void test_locked_fn(void)
{
    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_is_locked(5, 9) == true,
          "Fn position (5,9) reported locked");
    CHECK(dynamic_keymap_is_locked(0, 0) == false,
          "ordinary position not locked");
    CHECK(dynamic_keymap_set(0, 5, 9, 0x04) == CFG_STATUS_BAD_KEYCODE,
          "set on locked Fn rejected");
}

static void test_keycode_allowlist(void)
{
    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_keycode_allowed(KC_NO) == true, "KC_NO allowed");
    CHECK(dynamic_keymap_keycode_allowed(KC_TRANSPARENT) == true,
          "KC_TRANSPARENT allowed");
    CHECK(dynamic_keymap_keycode_allowed(MO(1)) == true, "MO(1) allowed");
    CHECK(dynamic_keymap_keycode_allowed(0x04 /* KC_A */) == true,
          "basic usage allowed");
    CHECK(dynamic_keymap_keycode_allowed(0xE7 /* KC_F24 */) == true,
          "F24 (top of basic) allowed");
    CHECK(dynamic_keymap_keycode_allowed(0x0158 /* consumer */) == true,
          "consumer range allowed");
    CHECK(dynamic_keymap_keycode_allowed(0x0201 /* system */) == true,
          "system range allowed");
    CHECK(dynamic_keymap_keycode_allowed(RGB_BRI_UP) == true,
          "RGB custom allowed");
    CHECK(dynamic_keymap_keycode_allowed(0x0300u) == false,
          "unmapped range rejected");
    CHECK(dynamic_keymap_keycode_allowed(0xFFFFu) == false,
          "0xFFFF rejected");
}

static void test_resolve_fallback(void)
{
    dynamic_keymap_deactivate();
    CHECK(dynamic_keymap_resolve(0, 0, 0) == 0xFFFFu,
          "resolve inactive -> 0xFFFF (caller falls back)");

    dynamic_keymap_load_defaults();
    CHECK(dynamic_keymap_resolve(0, 3, 7) == keymaps[0][3][7],
          "resolve active returns dynamic value");

    dynamic_keymap_set(0, 3, 7, 0x30 /* KC_B */);
    CHECK(dynamic_keymap_resolve(0, 3, 7) == 0x30,
          "resolve reflects runtime edit");
}

static void test_activate_deactivate(void)
{
    dynamic_keymap_deactivate();
    CHECK(dynamic_keymap_is_active() == false, "deactivated");
    dynamic_keymap_activate();
    CHECK(dynamic_keymap_is_active() == true, "activated");
    dynamic_keymap_deactivate();
}

int main(void)
{
    printf("RK84 dynamic keymap host harness\n");
    printf("--------------------------------\n");
    test_defaults_load();
    test_set_get();
    test_out_of_range();
    test_locked_fn();
    test_keycode_allowlist();
    test_resolve_fallback();
    test_activate_deactivate();
    printf("--------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
