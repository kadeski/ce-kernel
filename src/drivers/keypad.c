#include "keypad.h"
#include "hw.h"
#include "timer.h"

#define REPEAT_DELAY_MS 400
#define REPEAT_RATE_MS  60

static uint8_t current[KEYPAD_GROUPS];
static uint8_t repeat_key = KEY_NONE;
static uint32_t repeat_at;
static uint8_t saved_on_latch;

void keypad_init(void)
{
    saved_on_latch = INT_ON_LATCH;
    INT_ON_LATCH = saved_on_latch & ~INT_ON_BIT;
    INT_ON_ACK = INT_ON_BIT;
}

void keypad_restore(void)
{
    INT_ON_LATCH = saved_on_latch;
    INT_ON_ACK = INT_ON_BIT;
}

void keypad_scan(uint8_t state[KEYPAD_GROUPS])
{
    /* Scan-once mode: the controller walks every row, then drops back to
     * idle. Only the mode bits change, so the row timing set by TI-OS (or by
     * the ROM's cold init) is kept. */
    KB_MODE = (KB_MODE & ~KB_MODE_MASK) | KB_MODE_SCAN_ONCE;
    while (KB_MODE & KB_MODE_MASK)
        ;
    state[0] = 0;
    for (uint8_t g = 1; g < KEYPAD_GROUPS; g++)
        state[g] = (uint8_t)KB_GROUP(g);
}

bool keypad_on_pressed(void)
{
    return INT_ON_STATUS & INT_ON_BIT;
}

bool keypad_held(uint8_t key)
{
    return key != KEY_NONE && (current[key >> 3] & (1 << (key & 7)));
}

static bool repeats(uint8_t key)
{
    return key == KEY_DEL || key == KEY_LEFT || key == KEY_RIGHT ||
           key == KEY_UP || key == KEY_DOWN;
}

uint8_t keypad_poll(void)
{
    uint8_t previous[KEYPAD_GROUPS];
    for (uint8_t g = 0; g < KEYPAD_GROUPS; g++)
        previous[g] = current[g];
    keypad_scan(current);

    for (uint8_t g = 1; g < KEYPAD_GROUPS; g++) {
        uint8_t pressed = current[g] & ~previous[g];
        for (uint8_t bit = 0; pressed; bit++, pressed >>= 1) {
            if (pressed & 1) {
                uint8_t key = KEY_CODE(g, bit);
                repeat_key = repeats(key) ? key : KEY_NONE;
                repeat_at = timer_ms() + REPEAT_DELAY_MS;
                return key;
            }
        }
    }

    if (keypad_held(repeat_key) && (int32_t)(timer_ms() - repeat_at) >= 0) {
        repeat_at += REPEAT_RATE_MS;
        return repeat_key;
    }
    return KEY_NONE;
}
