#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

#include "../ui/console.h"
#include "../ui/ui.h"

/* Longest line the shell and calc read, after a two-character prompt. */
#define SHELL_LINE_MAX (CONSOLE_COLS - 3)

/* Runs the interactive shell until ON or 2nd + MODE (RUN_BACK, to the menu)
 * or the exit command (RUN_EXIT, out of the kernel). */
enum run_end shell_run(void);

#endif
