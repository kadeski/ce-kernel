#ifndef KERNEL_NOTES_H
#define KERNEL_NOTES_H

#include "ui.h"

/* The Notes entry: lists the archived Python files whose names end in R,
 * such as NUMR, and shows the one picked. */
enum run_end notes_run(void);

/* Shows the archived Python file name, at its line "# == heading ==" when
 * heading is given and found. */
enum run_end notes_open(const char *name, const char *heading);

#endif
