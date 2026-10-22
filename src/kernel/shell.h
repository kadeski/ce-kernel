#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

#include "console.h"

#include <stdbool.h>

/* Longest line the shell and its tools read, after a two-character prompt. */
#define SHELL_LINE_MAX (CONSOLE_COLS - 3)

/* Runs the interactive shell until the user exits. */
void shell_run(void);

/* How shell_edit_line ended. */
enum line_end {
    LINE_ENTER,                         /* the cursor is on the next line */
    LINE_QUIT,                          /* 2nd + MODE */
    LINE_ON,                            /* ON, pressed and released */
    LINE_CLEAR,                         /* CLEAR on an empty line */
};

/*
 * Lets the user edit buf, which is already shown after a prompt and holds up
 * to SHELL_LINE_MAX characters, until one of the keys above. ENTER copies a
 * non-empty line into history (also SHELL_LINE_MAX + 1 bytes), and UP puts it
 * back. Calling it again after any other key carries on with the same line.
 */
enum line_end shell_edit_line(char *buf, char *history);

/* Sets the typing layer to letters (alpha lock) or digits. */
void shell_set_alpha(bool alpha);

#endif
