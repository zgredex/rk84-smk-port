/* Host-harness umbrella: every framework header report.c includes maps
 * to this single stub file via the -include flag. This avoids touching
 * the real SMK sources. */
#ifndef HARNESS_UMBRELLA_H
#define HARNESS_UMBRELLA_H

#include "harness_stubs.h"

/* layout.h */
#ifndef LAYOUT_H
#define LAYOUT_H
#endif

/* keyboard.h */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#endif

/* kb.h */
#ifndef KB_H
#define KB_H
#endif

/* usb.h */
#ifndef USB_H
#define USB_H
#endif

/* debug.h */
#ifndef DEBUG_H
#define DEBUG_H
#endif

#endif
