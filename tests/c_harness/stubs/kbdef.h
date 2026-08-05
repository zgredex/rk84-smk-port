/* Host-harness shadow of the board's kbdef.h — minimal subset needed
 * by dynamic_keymap.c under test (no SDCC register stack). */
#ifndef HARNESS_KBDEF_H
#define HARNESS_KBDEF_H

#include <stdint.h>
#include <stdbool.h>

/* Build-mode gates: the harness compiles the dynamic keymap ON. */
#ifndef SMK_DYNAMIC_KEYMAP
#define SMK_DYNAMIC_KEYMAP 1
#endif
#ifndef RK84_RGB_ENABLE
#define RK84_RGB_ENABLE 0
#endif
#ifndef RK84_RECOVERY_ONLY
#define RK84_RECOVERY_ONLY 0
#endif
#ifndef RK84_USB_FULL
#define RK84_USB_FULL 0
#endif

/* SAFE_RANGE + custom keycodes (from keycodes.h + kbdef.h).
 * N1 (audit): SAFE_RANGE = QK_USER = 0x7E40, NOT 0x5200 (QK_TO). */
#ifndef SAFE_RANGE
#define SAFE_RANGE 0x7E40u
#endif

/* Matrix geometry — real layout.h's keymaps extern needs these. */
#ifndef MATRIX_ROWS
#define MATRIX_ROWS 6
#endif
#ifndef MATRIX_COLS
#define MATRIX_COLS 16
#endif

enum custom_keycodes {
    RGB_BRI_UP = SAFE_RANGE,
    RGB_BRI_DN,
};

/* N1: the harness must agree with pinned SMK exactly. */
_Static_assert(RGB_BRI_UP == 0x7E40u,
               "harness custom-keycode base differs from pinned SMK");
_Static_assert(RGB_BRI_DN == 0x7E41u,
               "harness custom-keycode values differ from firmware");
/* Only MO(1) is allowed, not the whole momentary range. */
#ifndef MO_1
#define MO_1 (0x5220u | 1u)
#endif

#endif /* HARNESS_KBDEF_H */
