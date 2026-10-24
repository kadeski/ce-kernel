#include "calc.h"
#include "console.h"
#include "events.h"
#include "expr.h"
#include "keymap.h"
#include "shell.h"
#include "ui.h"
#include "../drivers/keypad.h"

#include <string.h>

/*
 * Each line is one expression in exact fractions (expr.c), so 7/2 is 7/2,
 * not 3. The toolchain's float routines call TI's boot code, so decimals are
 * shown by long division instead.
 */

#define PROMPT "= "

static char expr[SHELL_LINE_MAX + 1];
static char last_expr[SHELL_LINE_MAX + 1];

static void prompt(void)
{
    console_set_color(COLOR_ACCENT);
    console_puts(PROMPT);
    console_set_color(COLOR_TEXT);
}

/* MATH: lists the functions below the line, then types the one picked by
 * its digit, with its "(". CLEAR picks none. Returns false on ON. */
static bool pick_function(void)
{
    const char *name;
    console_newline();
    console_set_color(COLOR_DIM);
    for (uint8_t i = 0; (name = expr_function(i)); i++) {
        if (console_column() + 2 + strlen(name) > CONSOLE_COLS - 1)
            console_newline();
        console_putc((char)('1' + i));
        console_putc(' ');
        console_puts(name);
        console_puts("  ");
    }
    console_newline();

    int8_t d;
    for (;;) {
        if (kernel_poll())
            return false;
        uint8_t key = keypad_poll();
        if (key == KEY_CLEAR)
            break;
        d = keymap_digit(key);
        if (d >= 1 && (name = expr_function((uint8_t)(d - 1)))) {
            if (strlen(expr) + strlen(name) + 1 <= SHELL_LINE_MAX) {
                strcat(expr, name);
                strcat(expr, "(");
            }
            break;
        }
    }
    prompt();
    console_puts(expr);
    return true;
}

void calc_run(void)
{
    console_set_color(COLOR_DIM);
    console_puts("fractions; MATH lists the functions");
    console_newline();
    ui_set_alpha(false);

    for (;;) {
        prompt();
        expr[0] = '\0';
        enum line_end end;
        while ((end = ui_edit_line(expr, SHELL_LINE_MAX, last_expr)) == LINE_KEY)
            if (ui_line_key() == KC_MATH && !pick_function())
                break;
        if (end != LINE_ENTER)
            break;
        if (!expr[0])
            continue;

        struct frac result;
        if (expr_eval(expr, NULL, &result)) {
            console_puts(frac_show(result));
        } else {
            console_set_color(COLOR_ERROR);
            console_puts(expr_error());
            console_set_color(COLOR_TEXT);
        }
        console_newline();
    }

    /* The cursor is still after the prompt. */
    console_newline();
    ui_set_alpha(true);
}
