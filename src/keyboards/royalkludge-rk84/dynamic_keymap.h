/*
 * RK84 dynamic keymap — header (configurator M2/M3).
 *
 * Spec: SMK84-CONFIGURATOR-AND-RGB-ANIMATION-SPEC.md §8.
 * Guarded by SMK_DYNAMIC_KEYMAP=1 (meson cc_args).
 */
#ifndef RK84_DYNAMIC_KEYMAP_H
#define RK84_DYNAMIC_KEYMAP_H

#include <stdint.h>
#include <stdbool.h>
#include "config_protocol.h"  /* config_status_t (M3 unified) */

#define RK84_DYNAMIC_LAYERS 2u
#define RK84_MATRIX_ROWS    6u
#define RK84_MATRIX_COLS    16u

#if SMK_DYNAMIC_KEYMAP

uint16_t dynamic_keymap_get(uint8_t layer, uint8_t row, uint8_t col);
bool     dynamic_keymap_is_active(void);
bool     dynamic_keymap_keycode_allowed(uint16_t keycode);
bool     dynamic_keymap_is_locked(uint8_t row, uint8_t col);
config_status_t dynamic_keymap_set(
    uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode);
void dynamic_keymap_load_defaults(void);
void dynamic_keymap_activate(void);
void dynamic_keymap_deactivate(void);

/* Framework hook: matrix.c resolve_keycode() calls this when
 * SMK_DYNAMIC_KEYMAP is defined. Returns the keycode for the given
 * layer/row/col, or 0xFFFF when the dynamic map is inactive/out of
 * range so the caller falls back to keymaps[][][]. */
uint16_t dynamic_keymap_resolve(uint8_t layer, uint8_t row, uint8_t col);

#else
/* Not compiled: no dynamic map, no XRAM cost. */
#endif

#endif /* RK84_DYNAMIC_KEYMAP_H */
