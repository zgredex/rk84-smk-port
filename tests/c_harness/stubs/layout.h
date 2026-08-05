/* Host-harness shadow of src/smk/layout.h — provides a small compiled
 * keymaps[] so dynamic_keymap.c's defaults-load has something to copy.
 * Values are arbitrary-but-distinct so tests can verify the copy. */
#ifndef HARNESS_LAYOUT_H
#define HARNESS_LAYOUT_H

#include <stdint.h>

#ifndef MATRIX_ROWS
#define MATRIX_ROWS 6
#endif
#ifndef MATRIX_COLS
#define MATRIX_COLS 16
#endif

extern const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS];

#endif /* HARNESS_LAYOUT_H */
