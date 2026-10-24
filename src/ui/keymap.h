#ifndef UI_KEYMAP_H
#define UI_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

/* Non-printing results of keymap_translate (all below 0x20). */
enum {
    KC_NONE = 0,
    KC_QUIT = 0x04,                     /* 2nd + MODE */
    KC_BACKSPACE = 0x08,                /* DEL */
    KC_ENTER = 0x0A,
    KC_MATH = 0x0E,                     /* MATH in digit mode; it types a in alpha lock */
    KC_UP = 0x10,
    KC_DOWN = 0x11,
    KC_F1 = 0x12,                       /* Y=, WINDOW, ZOOM, TRACE and GRAPH, */
    KC_F2 = 0x13,                       /* the five keys under the screen */
    KC_F3 = 0x14,
    KC_F4 = 0x15,
    KC_F5 = 0x16,
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

/* The digit a key is labelled with, in any layer, or -1. */
int8_t keymap_digit(uint8_t key);

#endif
