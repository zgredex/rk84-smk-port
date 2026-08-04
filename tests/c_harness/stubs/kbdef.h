/* Host-harness shadow of the board's kbdef.h — the real one includes
 * the SDCC sh68f90a.h register stack. The report.c path needs only the
 * RK84 report gates, inlined here so the harness compiles on the host.
 *
 * IMPORTANT: keep the gate values in sync with the board's meson flags
 * (RK84_STOCK_REPORTS/NKRO_ENABLE come from the compiler command line;
 * the matriX dims and custom-keycode base are board constants). */
#ifndef HARNESS_KBDEF_H
#define HARNESS_KBDEF_H

#define MATRIX_ROWS 6
#define MATRIX_COLS 16
#define RGB_ROWS 6
#define RGB_COLS 21
#define RGB_PHASES 19
#define RGB_BRIGHTNESS_MAX 5

/* SAFE_RANGE comes from keycodes.h (included via report.h). The custom
 * keycodes only matter for kb.c/layout.c, not report.c; keep the enum
 * minimal and guarded so it only defines when keycodes.h hasn't. */
#ifndef SAFE_RANGE
#define SAFE_RANGE 0x5200
#endif

enum custom_keycodes {
    RGB_BRI_UP = SAFE_RANGE,
    RGB_BRI_DN,
};

#ifndef RK84_RGB_ENABLE
#define RK84_RGB_ENABLE 0
#endif
#ifndef RK84_RECOVERY_ONLY
#define RK84_RECOVERY_ONLY 0
#endif
#ifndef RK84_USB_FULL
#define RK84_USB_FULL 0
#endif
#ifndef RK84_WIRELESS_ENABLE
#define RK84_WIRELESS_ENABLE 0
#endif

#endif
