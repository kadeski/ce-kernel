#include "keymap.h"
#include "../drivers/keypad.h"

/* Letters follow the green labels printed above the keys. */
static const char alpha_layer[64] = {
    [KEY_MATH] = 'a', [KEY_APPS] = 'b', [KEY_PRGM] = 'c',
    [KEY_RECIP] = 'd', [KEY_SIN] = 'e', [KEY_COS] = 'f', [KEY_TAN] = 'g', [KEY_POWER] = 'h',
    [KEY_SQUARE] = 'i', [KEY_COMMA] = 'j', [KEY_LPAREN] = 'k', [KEY_RPAREN] = 'l', [KEY_DIV] = 'm',
    [KEY_LOG] = 'n', [KEY_7] = 'o', [KEY_8] = 'p', [KEY_9] = 'q', [KEY_MUL] = 'r',
    [KEY_LN] = 's', [KEY_4] = 't', [KEY_5] = 'u', [KEY_6] = 'v', [KEY_SUB] = 'w',
    [KEY_STO] = 'x', [KEY_1] = 'y', [KEY_2] = 'z', [KEY_ADD] = '"',
    [KEY_0] = ' ', [KEY_DOT] = ':', [KEY_NEG] = '?',
};

static const char number_layer[64] = {
    [KEY_0] = '0', [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
    [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9',
    [KEY_DOT] = '.', [KEY_NEG] = '-', [KEY_COMMA] = ',',
    [KEY_ADD] = '+', [KEY_SUB] = '-', [KEY_MUL] = '*', [KEY_DIV] = '/', [KEY_POWER] = '^',
    [KEY_LPAREN] = '(', [KEY_RPAREN] = ')',
};

static const char second_layer[64] = {
    [KEY_MUL] = '[', [KEY_SUB] = ']', [KEY_LPAREN] = '{', [KEY_RPAREN] = '}',
    [KEY_DOT] = '=', [KEY_0] = '_', [KEY_DIV] = '\\', [KEY_ADD] = '|',
};

char keymap_translate(struct keymap_state *state, uint8_t key)
{
    if (key == KEY_NONE)
        return KC_NONE;
    if (key == KEY_2ND) {
        state->second = !state->second;
        return KC_NONE;
    }
    if (key == KEY_ALPHA) {
        state->second = false;
        state->alpha = !state->alpha;
        return KC_NONE;
    }

    bool second = state->second;
    state->second = false;

    switch (key) {
    case KEY_ENTER: return KC_ENTER;
    case KEY_DEL:   return KC_BACKSPACE;
    case KEY_CLEAR: return KC_CLEAR;
    case KEY_UP:    return KC_HISTORY;
    case KEY_MODE:  return second ? KC_QUIT : KC_NONE;
    }

    if (second)
        return second_layer[key];
    if (state->alpha && alpha_layer[key])
        return alpha_layer[key];
    return number_layer[key];
}
