#ifndef UI_UI_H
#define UI_UI_H

#include "console.h"
#include "../core/frac.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Screens the menu, the study tools, calc and the shell share: the status
 * bar and typing layer, the line editor, numbered lists, scrolling views and
 * tool forms. Each one polls with kernel_poll(), so ON always gets out.
 */

/* How a screen ended, for the one that opened it. */
enum run_end {
    RUN_BACK,                           /* CLEAR: back one level */
    RUN_HOME,                           /* ON or 2nd + MODE: back to the menu */
    RUN_EXIT,                           /* the shell's exit: leave the kernel */
};

/* Draws the status bar, name on the left, and keeps its clock current, once
 * a second. */
void ui_status_start(const char *name);
/* Sets the typing layer to letters (alpha lock) or digits. */
void ui_set_alpha(bool alpha);

/* How ui_edit_line ended. */
enum line_end {
    LINE_ENTER,                         /* the cursor is on the next line */
    LINE_QUIT,                          /* 2nd + MODE */
    LINE_ON,                            /* ON, pressed and released */
    LINE_CLEAR,                         /* CLEAR on an empty line */
    LINE_KEY,                           /* a key the editor doesn't use: ui_line_key() */
};

/*
 * Lets the user edit buf, which is already shown at the cursor and holds up
 * to max characters, until one of the keys above. With history (max + 1
 * bytes), ENTER copies a non-empty line into it and UP puts it back; without,
 * UP ends with LINE_KEY like DOWN, MATH and the five keys under the screen.
 * Calling it again after LINE_KEY carries on with the same line.
 */
enum line_end ui_edit_line(char *buf, uint8_t max, char *history);
/* The KC_ code (keymap.h) that ended the last LINE_KEY. */
char ui_line_key(void);

/* How ui_list ended. */
enum ui_end { UI_PICK, UI_BACK, UI_ON, UI_QUIT };

/*
 * Shows a numbered list of n items (up to 9) under title, with hint below,
 * and returns UI_PICK with *pick set when one is chosen by its digit or by
 * UP, DOWN and ENTER. *pick also says which item starts selected. Each item
 * is size bytes and starts with its label, a const char *.
 */
enum ui_end ui_list(const char *title, const void *items, size_t size, uint8_t n,
                    const char *hint, uint8_t *pick);

/* Line i of a view: its text, which needs no NUL, its length and colour.
 * Lines in COLOR_ACCENT are headings: the title row shows the last one at
 * or above the top of the screen. */
typedef const char *(*view_line_fn)(uint24_t i, uint8_t *len, uint8_t *color);

/* Shows lines 0 to lines-1 from top, scrolling with UP, DOWN, LEFT and
 * RIGHT, until CLEAR or GRAPH (RUN_BACK), or ON or 2nd + MODE (RUN_HOME). */
enum run_end ui_view(const char *name, uint24_t lines, uint24_t top, view_line_fn line);

/* ---- study tools ---- */

#define FIELD_MAX  24                   /* characters in a field */
#define FIELDS_MAX 8
#define LIST_MAX   12                   /* items in an F_LIST field */

enum field_kind {
    F_INT,                              /* an expression with a whole-number value */
    F_NUM,                              /* an expression; any fraction */
    F_LIST,                             /* expressions separated by commas */
    F_DIGITS,                           /* text as typed, starting in digit mode */
    F_LETTERS,                          /* text as typed, starting in alpha lock */
    F_OPT = 0x80,                       /* or'd in: may be left empty */
};

struct field {
    const char *label;
    uint8_t kind;
};

/* A field's value once ENTER has read it. */
struct value {
    bool given;                         /* false for an optional field left empty */
    const char *text;                   /* as typed */
    struct frac f;                      /* F_INT and F_NUM */
    int64_t i;                          /* F_INT */
    const struct frac *list;            /* F_LIST */
    uint8_t n;                          /* items in list */
};

struct tool {
    const char *title;                  /* first, for ui_list */
    const struct field *fields;
    uint8_t nfields;
    /* Works out the answer from v[0..nfields-1] with ui_answer() and the
     * rest; returns false after ui_error(). */
    bool (*compute)(const struct value *v);
    const char *notes_file;             /* an archived *R.py, for the Notes key */
    const char *notes_heading;          /* open it at the line "# == heading ==" */
    enum run_end (*run)(void);          /* a screen of its own instead of a form */
};

struct section {
    const char *title;
    const struct tool *tools;
    uint8_t ntools;
};

/* Sets what the WINDOW key in a form calls to show a tool's notes: file is
 * its notes_file, heading its notes_heading. Until then WINDOW does nothing. */
void ui_set_notes(enum run_end (*open)(const char *file, const char *heading));
/* Lists a section's tools until CLEAR, ON or 2nd + MODE. */
enum run_end ui_section(const struct section *s);
/* Runs one tool's form: UP and DOWN move between fields, ENTER shows the
 * answer, Y= the steps, WINDOW the notes, and CLEAR or GRAPH go back. */
enum run_end ui_tool(const struct tool *t);

/*
 * A tool's output, printf-style (%d %u %s %c with l and ll, a width, - and 0).
 * The first four go on the form under the fields; ui_step and ui_step_head
 * go to the Steps view. Long lines wrap.
 */
void ui_answer(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_check(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool ui_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* false */
void ui_step(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_step_head(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
