#include "console.h"
#include "font8x8.h"
#include "../drivers/lcd.h"

#include <string.h>

#define FIRST_ROW 1                     /* below the status bar */
#define ROW_BYTES (LCD_WIDTH * 8)

static uint8_t cur_col;
static uint8_t cur_row = FIRST_ROW;
static uint8_t fg = COLOR_TEXT;

void console_draw_cell(uint8_t col, uint8_t row, char c, uint8_t color, uint8_t bg)
{
    if (c < FONT_FIRST || c > FONT_LAST)
        c = '?';
    const uint8_t *glyph = font8x8[c - FONT_FIRST];
    uint8_t *dst = LCD_FRAMEBUFFER + (uint24_t)row * ROW_BYTES + col * 8;

    for (uint8_t y = 0; y < 8; y++, dst += LCD_WIDTH) {
        uint8_t bits = glyph[y];
        for (uint8_t x = 0; x < 8; x++, bits >>= 1)
            dst[x] = (bits & 1) ? color : bg;
    }
}

static void scroll(void)
{
    uint8_t *top = LCD_FRAMEBUFFER + FIRST_ROW * ROW_BYTES;
    memmove(top, top + ROW_BYTES, (CONSOLE_ROWS - FIRST_ROW - 1) * ROW_BYTES);
    memset(LCD_FRAMEBUFFER + (CONSOLE_ROWS - 1) * ROW_BYTES, COLOR_BG, ROW_BYTES);
}

void console_newline(void)
{
    cur_col = 0;
    if (cur_row == CONSOLE_ROWS - 1)
        scroll();
    else
        cur_row++;
}

void console_init(void)
{
    cur_col = 0;
    cur_row = FIRST_ROW;
    fg = COLOR_TEXT;
}

void console_clear(void)
{
    memset(LCD_FRAMEBUFFER + FIRST_ROW * ROW_BYTES, COLOR_BG,
           (CONSOLE_ROWS - FIRST_ROW) * ROW_BYTES);
    cur_col = 0;
    cur_row = FIRST_ROW;
}

void console_set_color(uint8_t color)
{
    fg = color;
}

void console_putc(char c)
{
    if (c == '\n') {
        console_newline();
        return;
    }
    console_draw_cell(cur_col, cur_row, c, fg, COLOR_BG);
    if (++cur_col == CONSOLE_COLS)
        console_newline();
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_putdec(uint32_t value)
{
    char buf[11];
    uint8_t i = sizeof buf;
    buf[--i] = '\0';
    do {
        buf[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    console_puts(&buf[i]);
}

void console_puthex(uint32_t value, uint8_t digits)
{
    static const char hex[] = "0123456789ABCDEF";
    while (digits--)
        console_putc(hex[(value >> (digits * 4)) & 0xF]);
}

void console_backspace(void)
{
    if (cur_col == 0)
        return;
    cur_col--;
    console_draw_cell(cur_col, cur_row, ' ', fg, COLOR_BG);
}

uint8_t console_column(void)
{
    return cur_col;
}

void console_status(const char *left, const char *right)
{
    uint8_t right_len = (uint8_t)strlen(right);
    uint8_t col = 0;
    while (*left && col < CONSOLE_COLS)
        console_draw_cell(col++, 0, *left++, COLOR_BAR_TEXT, COLOR_BAR_BG);
    while (col < CONSOLE_COLS - right_len)
        console_draw_cell(col++, 0, ' ', COLOR_BAR_TEXT, COLOR_BAR_BG);
    while (*right && col < CONSOLE_COLS)
        console_draw_cell(col++, 0, *right++, COLOR_BAR_TEXT, COLOR_BAR_BG);
}
