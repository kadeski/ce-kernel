#ifndef KERNEL_KEYMAP_H
#define KERNEL_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

/* Non-printing results of keymap_translate (all below 0x20). */
enum {
    KC_NONE = 0,
    KC_QUIT = 0x04,                     /* 2nd + MODE */
    KC_BACKSPACE = 0x08,                /* DEL */
    KC_ENTER = 0x0A,
    KC_HISTORY = 0x10,                  /* UP */
    KC_CLEAR = 0x1B,
};

/* Typing layers. ALPHA toggles alpha lock; 2nd applies to the next key only. */
struct keymap_state {
    bool alpha;
    bool second;
};

/* Turns a key press into a character or KC_ code, updating the layer state.
 * Returns KC_NONE for modifier keys and unmapped keys. */
char keymap_translate(struct keymap_state *state, uint8_t key);

#endif
