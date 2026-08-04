#include "kbdef.h"
#include "layout.h"
#include "user_layout.h"
#include <stdint.h>

// =====================================================================
// RK84 layout — 84-key default (matrix pins and layout derived from
// the factory firmware's default keymap).
// Matrix: 16 cols (c0=P5.0 c1=P5.1 c2=P5.2 c3=P3.5 c4=P3.4 c5=P3.3
// c6=P3.2 c7=P3.1 c8=P3.0 c9=P2.5 c10=P2.4 c11=P2.3 c12=P2.2 c13=P2.1
// c14=P2.0 c15=P1.5) x 6 rows (r0=P7.0 r1=P7.1 r2=P7.2 r3=P7.3 r4=P5.3
// r5=P5.4).
//
// 84 populated keys. Empty positions = KC_NO.
//
// Fn = c9r5 (hard-coded); RCtrl = c10r5. Fn layer is layer [1]; all
// unremapped cells are KC_TRANSPARENT so held keys keep working.
// Modifiers: c0r4 LSHIFT, c0r5 LCTRL, c1r5 LGUI, c2r5 LALT,
// c8r5 RALT, c10r5 RCTRL, c11r4 RSHIFT
// =====================================================================

// clang-format off
const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
 [0] = {
 // row 0 (P7.0): ESC F1-F12 PSCR PAUSE DEL
 { KC_ESC, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7,
 KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_PSCR, KC_PAUSE,
 KC_DEL },
 // row 1 (P7.1): ` 1-0 - = BSPC HOME
 { KC_GRAVE, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7,
 KC_8, KC_9, KC_0, KC_MINUS, KC_EQUAL, KC_BSPC, KC_NO,
 KC_HOME },
 // row 2 (P7.2): TAB Q-P [ ] \ END
 { KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U,
 KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_NO,
 KC_END },
 // row 3 (P7.3): CAPS A-L ; ' [NO] ENTER [NO] PGUP
 // c12r3 = ANSI phantom (0x32 NonUS# record unpopulated on RK84)
 { KC_CAPS, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J,
 KC_K, KC_L, KC_SEMICOLON, KC_QUOTE, KC_NO, KC_ENTER, KC_NO,
 KC_PGUP },
 // row 4 (P5.3): LSHIFT Z-/ RSHIFT UP PGDN
 { KC_LEFT_SHIFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M,
 KC_COMMA, KC_DOT, KC_SLASH, KC_RIGHT_SHIFT, KC_NO, KC_NO,
 KC_UP, KC_PGDN },
 // row 5 (P5.4): LCTRL LGUI LALT [NO] [NO] SPACE [NO] [NO] RALT
 // Fn RCtrl [NO] [NO] LEFT DOWN RIGHT
 // Fn = c9r5 (hard-coded in the Fn detector)
 // RCtrl = c10r5 (USB RCtrl bit)
 { KC_LEFT_CTRL, KC_LEFT_GUI, KC_LEFT_ALT, KC_NO, KC_NO, KC_SPACE, KC_NO,
 KC_NO, KC_RIGHT_ALT, MO(1), KC_RIGHT_CTRL, KC_NO, KC_NO,
 KC_LEFT, KC_DOWN, KC_RIGHT },
 },
 [1] = {
     // Fn layer: media/apps on F1-F12 (stock Windows-mode map),
     // Print=INS, Pause=SCRLK, Up/Down = RGB brightness.
     // KC_TRANSPARENT everywhere else: unremapped keys keep working
     // while Fn is held, and releases never resolve to KC_NO.
     { KC_TRANSPARENT, KC_MY_COMPUTER, KC_WWW_HOME, KC_MAIL,
       KC_CALCULATOR, KC_MEDIA_SELECT, KC_MEDIA_STOP,
       KC_MEDIA_PREV_TRACK, KC_MEDIA_PLAY_PAUSE,
       KC_MEDIA_NEXT_TRACK, KC_AUDIO_MUTE, KC_AUDIO_VOL_DOWN,
       KC_AUDIO_VOL_UP, KC_INSERT, KC_SCROLL_LOCK,
       KC_SYSTEM_POWER },
     { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT },
     { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT },
     { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT },
     // row 4: Up (c14) -> RGB_BRI_UP
     { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, RGB_BRI_UP,
       KC_TRANSPARENT },
     // row 5: Down (c14) -> RGB_BRI_DN
     { KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, KC_TRANSPARENT,
       KC_TRANSPARENT, KC_TRANSPARENT, RGB_BRI_DN,
       KC_TRANSPARENT },
 },
 };
// clang-format on

bool layout_process_record(uint16_t keycode, bool key_pressed)
{
 (void)keycode;
 (void)key_pressed;
 return true;
}
