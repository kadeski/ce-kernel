#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <stdint.h>

#define CONSOLE_COLS 40
#define CONSOLE_ROWS 30                 /* row 0 is the status bar */

/* Palette indices */
enum {
    COLOR_BG,
    COLOR_TEXT,
    COLOR_ACCENT,
    COLOR_BAR_BG,
    COLOR_BAR_TEXT,
    COLOR_ERROR,
    COLOR_OK,
    COLOR_DIM,
};

void console_init(void);
void console_clear(void);

void console_set_color(uint8_t fg);
void console_putc(char c);
void console_puts(const char *s);
void console_putdec(uint32_t value);
void console_puthex(uint32_t value, uint8_t digits);
void console_newline(void);
/* Erases the last character written on the current line. */
void console_backspace(void);
uint8_t console_column(void);
/* Moves the cursor; row 0 is the status bar, so rows start at 1. */
void console_goto(uint8_t col, uint8_t row);

/* Draws text into the status bar; it is padded to the full width. */
void console_status(const char *left, const char *right);

/* Draws one character cell anywhere on screen (used by full-screen tools). */
void console_draw_cell(uint8_t col, uint8_t row, char c, uint8_t fg, uint8_t bg);
/* Fills cols cells from col on row with bg, much faster than blank cells. */
void console_fill(uint8_t col, uint8_t row, uint8_t cols, uint8_t bg);

#endif
