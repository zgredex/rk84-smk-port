/* Host-harness shadow of src/smk/keyboard.h. */
#ifndef HARNESS_KEYBOARD_H
#define HARNESS_KEYBOARD_H

#include <stdint.h>

typedef struct {
    uint8_t led_state;
    uint8_t rf_link;
} keyboard_state_t;

typedef struct {
    uint8_t nkro;
} keymap_config_t;

extern volatile keyboard_state_t keyboard_state;
extern keymap_config_t           keymap_config;

void keyboard_init(void);

#endif
