#ifndef DRIVERS_KEYPAD_H
#define DRIVERS_KEYPAD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The keypad is an 8x8 matrix read as groups 1-7 (group 0 is unused).
 * A key code is group * 8 + bit, so codes fit in 0..63.
 */
#define KEY_CODE(group, bit) ((uint8_t)((group) * 8 + (bit)))
#define KEY_NONE 0xFF

enum {
    KEY_GRAPH = KEY_CODE(1, 0), KEY_TRACE = KEY_CODE(1, 1), KEY_ZOOM = KEY_CODE(1, 2),
    KEY_WINDOW = KEY_CODE(1, 3), KEY_YEQU = KEY_CODE(1, 4), KEY_2ND = KEY_CODE(1, 5),
    KEY_MODE = KEY_CODE(1, 6), KEY_DEL = KEY_CODE(1, 7),

    KEY_STO = KEY_CODE(2, 1), KEY_LN = KEY_CODE(2, 2), KEY_LOG = KEY_CODE(2, 3),
    KEY_SQUARE = KEY_CODE(2, 4), KEY_RECIP = KEY_CODE(2, 5), KEY_MATH = KEY_CODE(2, 6),
    KEY_ALPHA = KEY_CODE(2, 7),

    KEY_0 = KEY_CODE(3, 0), KEY_1 = KEY_CODE(3, 1), KEY_4 = KEY_CODE(3, 2),
    KEY_7 = KEY_CODE(3, 3), KEY_COMMA = KEY_CODE(3, 4), KEY_SIN = KEY_CODE(3, 5),
    KEY_APPS = KEY_CODE(3, 6), KEY_XTON = KEY_CODE(3, 7),

    KEY_DOT = KEY_CODE(4, 0), KEY_2 = KEY_CODE(4, 1), KEY_5 = KEY_CODE(4, 2),
    KEY_8 = KEY_CODE(4, 3), KEY_LPAREN = KEY_CODE(4, 4), KEY_COS = KEY_CODE(4, 5),
    KEY_PRGM = KEY_CODE(4, 6), KEY_STAT = KEY_CODE(4, 7),

    KEY_NEG = KEY_CODE(5, 0), KEY_3 = KEY_CODE(5, 1), KEY_6 = KEY_CODE(5, 2),
    KEY_9 = KEY_CODE(5, 3), KEY_RPAREN = KEY_CODE(5, 4), KEY_TAN = KEY_CODE(5, 5),
    KEY_VARS = KEY_CODE(5, 6),

    KEY_ENTER = KEY_CODE(6, 0), KEY_ADD = KEY_CODE(6, 1), KEY_SUB = KEY_CODE(6, 2),
    KEY_MUL = KEY_CODE(6, 3), KEY_DIV = KEY_CODE(6, 4), KEY_POWER = KEY_CODE(6, 5),
    KEY_CLEAR = KEY_CODE(6, 6),

    KEY_DOWN = KEY_CODE(7, 0), KEY_LEFT = KEY_CODE(7, 1), KEY_RIGHT = KEY_CODE(7, 2),
    KEY_UP = KEY_CODE(7, 3),
};

#define KEYPAD_GROUPS 8

/* Makes the ON key report its live state; keypad_restore undoes it. Keys
 * already held when it runs are not reported as presses. */
void keypad_init(void);
void keypad_restore(void);

/* Scans the matrix once; state[g] holds the pressed bits of group g. */
void keypad_scan(uint8_t state[KEYPAD_GROUPS]);
bool keypad_on_pressed(void);

/*
 * Returns the next key press, or KEY_NONE. Held keys repeat after a delay.
 * Call it often: it scans the matrix on every call.
 */
uint8_t keypad_poll(void);
/* True while the key is held (as of the last keypad_poll or keypad_scan). */
bool keypad_held(uint8_t key);

#endif
