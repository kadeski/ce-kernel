#include "menu.h"
#include "calc.h"
#include "kernel.h"
#include "notes.h"
#include "shell.h"
#include "../discrete/discrete.h"
#include "../ui/console.h"
#include "../ui/ui.h"

#include <stddef.h>

struct menu_entry {
    const char *label;                  /* first, for ui_list */
    const struct section *section;      /* a list of tools, or */
    enum run_end (*run)(void);          /* a screen of its own */
};

static enum run_end calc_entry(void)
{
    console_clear();
    calc_run();
    return RUN_BACK;
}

static enum run_end shell_entry(void)
{
    console_clear();
    return shell_run();
}

static const struct menu_entry home[] = {
    { "Number theory",        &nt_section,    NULL },
    { "Counting",             &count_section, NULL },
    { "Recurrences and sums", &recur_section, NULL },
    { "Probability",          &prob_section,  NULL },
    { "Algorithms",           &algo_section,  NULL },
    { "Notes",                NULL,           notes_run },
    { "Calculator",           NULL,           calc_entry },
    { "Shell",                NULL,           shell_entry },
};
#define ENTRIES (sizeof home / sizeof home[0])

void menu_run(void)
{
    const char *hint = kernel_boot->can_exit ? "1-8 or ENTER. ON leaves the kernel."
                                             : "1-8 or ENTER.";
    uint8_t pick = 0;

    ui_status_start(" " KERNEL_NAME " " KERNEL_VERSION);
    ui_set_notes(notes_open);
    for (;;) {
        switch (ui_list("Discrete math", home, sizeof *home, ENTRIES, hint, &pick)) {
        case UI_PICK:
            break;
        case UI_ON:
            if (kernel_boot->can_exit)
                return;
            continue;                   /* the bare ROM has nowhere to go */
        case UI_QUIT:
            return;
        case UI_BACK:
            continue;
        }
        const struct menu_entry *e = &home[pick];
        if ((e->section ? ui_section(e->section) : e->run()) == RUN_EXIT)
            return;
    }
}
