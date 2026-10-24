#ifndef KERNEL_MENU_H
#define KERNEL_MENU_H

/* Runs the start menu of discrete math tools until the user leaves the kernel: ON or
 * 2nd + MODE at the menu (the bare ROM ignores ON there), or the shell's
 * exit command. */
void menu_run(void);

#endif
