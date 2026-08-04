/* Host-harness shadow of src/smk/layout.h — the real one pulls the
 * whole SDCC board stack; the RK84_STOCK_REPORTS path of report.c
 * never touches the keymaps table. */
#ifndef HARNESS_LAYOUT_H
#define HARNESS_LAYOUT_H

#include <stdint.h>
#include <stdbool.h>

/* MATRIX dims from kbdef.h, inlined to avoid the SDCC chain. */
#define MATRIX_ROWS 6
#define MATRIX_COLS 16

extern const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS];

#endif
