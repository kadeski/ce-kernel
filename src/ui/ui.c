#include "ui.h"
#include "keymap.h"
#include "../core/expr.h"
#include "../core/imath.h"
#include "../drivers/keypad.h"
#include "../drivers/rtc.h"
#include "../kernel/events.h"

#include <stdarg.h>
#include <string.h>

#define TEXT_COLS  (CONSOLE_COLS - 1)   /* after a one-column margin */
#define LIST_TOP   2                    /* the first item's row, under the title */
#define FORM_TOP   2                    /* the first field's row */
#define VIEW_TOP   2
#define VIEW_ROWS  (CONSOLE_ROWS - 4)   /* rows 2-27; 28 says where, 29 has Back */
#define SOFT_ROW   (CONSOLE_ROWS - 1)   /* labels for the keys under the screen */
#define SOFT_KEYS  5
#define SOFT_WIDTH 7                    /* and a gap after each */
#define FORM_LINES 20
#define STEP_LINES 200

static struct keymap_state keys = { .alpha = true };
static char last_key;
static const char *status_name = "";
static enum run_end (*open_notes)(const char *file, const char *heading);

/* ---- status bar and typing layer ---- */

/* Redraws the status bar. A timer event runs it every second for the clock,
 * and next_key() when a key changes the typing layer. */
static void update_status(void)
{
    static char right[12];              /* longest: "2nd a 23:59" */

    /* "[2nd] a HH:MM" */
    uint8_t i = 0;
    if (keys.second) {
        memcpy(&right[i], "2nd ", 4);
        i += 4;
    }
    right[i++] = keys.alpha ? 'a' : '1';
    right[i++] = ' ';
    struct rtc_time now;
    if (rtc_read(&now)) {
        right[i++] = (char)('0' + now.hours / 10);
        right[i++] = (char)('0' + now.hours % 10);
        right[i++] = ':';
        right[i++] = (char)('0' + now.minutes / 10);
        right[i++] = (char)('0' + now.minutes % 10);
    } else {
        memcpy(&right[i], "--:--", 5);
        i += 5;
    }
    right[i] = '\0';

    console_status(status_name, right);
}

void ui_status_start(const char *name)
{
    status_name = name;
    update_status();
    event_every(1000, update_status);
}

void ui_set_alpha(bool alpha)
{
    keys.alpha = alpha;
    keys.second = false;
    update_status();
}

/* The next key press as keymap_translate() gives it, or KC_NONE; *raw gets
 * its key code. */
static char next_key(uint8_t *raw)
{
    struct keymap_state before = keys;
    *raw = keypad_poll();
    char c = keymap_translate(&keys, *raw);
    if (keys.alpha != before.alpha || keys.second != before.second)
        update_status();
    return c;
}

/* ---- drawing ---- */

static void draw_text(uint8_t col, uint8_t row, const char *text, size_t len,
                      uint8_t fg, uint8_t bg)
{
    for (size_t i = 0; i < len && col < CONSOLE_COLS; i++)
        console_draw_cell(col++, row, text[i], fg, bg);
}

static void clear_from(uint8_t col, uint8_t row, uint8_t bg)
{
    if (col < CONSOLE_COLS)
        console_fill(col, row, CONSOLE_COLS - col, bg);
}

/* A row with one line of text after the margin. */
static void draw_line(uint8_t row, const char *text, size_t len, uint8_t fg)
{
    console_draw_cell(0, row, ' ', fg, COLOR_BG);
    draw_text(1, row, text, len, fg, COLOR_BG);
    clear_from((uint8_t)(1 + (len < TEXT_COLS ? len : TEXT_COLS)), row, COLOR_BG);
}

/* Labels for Y=, WINDOW, ZOOM, TRACE and GRAPH; NULL leaves a key blank. */
static void draw_soft(const char *const label[SOFT_KEYS])
{
    for (uint8_t k = 0; k < SOFT_KEYS; k++) {
        const char *s = label[k];
        uint8_t col = k * (SOFT_WIDTH + 1);
        size_t len = s ? strlen(s) : 0, pad = (SOFT_WIDTH - len) / 2;
        for (uint8_t j = 0; j < SOFT_WIDTH; j++) {
            char c = j >= pad && j < pad + len ? s[j - pad] : ' ';
            console_draw_cell(col + j, SOFT_ROW, c, COLOR_BAR_TEXT, s ? COLOR_BAR_BG : COLOR_BG);
        }
        console_draw_cell(col + SOFT_WIDTH, SOFT_ROW, ' ', COLOR_TEXT, COLOR_BG);
    }
}

/* ---- line editor ---- */

/* Replaces the len characters of buf, on screen too, with text. Returns the
 * new length. */
static uint8_t replace_line(char *buf, uint8_t len, uint8_t max, const char *text)
{
    while (len) {
        console_backspace();
        len--;
    }
    for (; *text && len < max; text++) {
        buf[len++] = *text;
        console_putc(*text);
    }
    buf[len] = '\0';
    return len;
}

enum line_end ui_edit_line(char *buf, uint8_t max, char *history)
{
    uint8_t len = (uint8_t)strlen(buf);

    for (;;) {
        if (kernel_poll())
            return LINE_ON;

        uint8_t raw;
        char c = next_key(&raw);
        switch (c) {
        case KC_NONE:
            break;
        case KC_QUIT:
            return LINE_QUIT;
        case KC_ENTER:
            console_newline();
            if (len && history)
                memcpy(history, buf, len + 1);
            return LINE_ENTER;
        case KC_BACKSPACE:
            if (len) {
                buf[--len] = '\0';
                console_backspace();
            }
            break;
        case KC_CLEAR:
            if (!len)
                return LINE_CLEAR;
            len = replace_line(buf, len, max, "");
            break;
        case KC_UP:
            if (history) {
                len = replace_line(buf, len, max, history);
                break;
            }
            /* fall through */
        default:
            if ((unsigned char)c < ' ') {
                last_key = c;
                return LINE_KEY;
            }
            if (len < max) {
                buf[len++] = c;
                buf[len] = '\0';
                console_putc(c);
            }
            break;
        }
    }
}

char ui_line_key(void)
{
    return last_key;
}

/* ---- lists ---- */

static void draw_item(const void *items, size_t size, uint8_t i, bool selected)
{
    const char *label = *(const char *const *)((const char *)items + i * size);
    uint8_t fg = selected ? COLOR_BAR_TEXT : COLOR_TEXT, bg = selected ? COLOR_BAR_BG : COLOR_BG;
    uint8_t row = LIST_TOP + i;
    char number[4] = { ' ', ' ', (char)('1' + i), ' ' };
    draw_text(0, row, number, sizeof number, fg, bg);
    draw_text(4, row, label, strlen(label), fg, bg);
    clear_from((uint8_t)(4 + strlen(label)), row, bg);
}

enum ui_end ui_list(const char *title, const void *items, size_t size, uint8_t n,
                    const char *hint, uint8_t *pick)
{
    ui_set_alpha(false);
    console_clear();
    draw_line(1, title, strlen(title), COLOR_ACCENT);
    for (uint8_t i = 0; i < n; i++)
        draw_item(items, size, i, i == *pick);
    draw_line(LIST_TOP + n, hint, strlen(hint), COLOR_DIM);

    for (;;) {
        if (kernel_poll())
            return UI_ON;
        uint8_t raw, was = *pick;
        char c = next_key(&raw);
        /* Digits pick in either typing layer. */
        int8_t d = keymap_digit(raw);
        if (d >= 1 && d <= n) {
            *pick = (uint8_t)(d - 1);
            return UI_PICK;
        }
        switch (c) {
        case KC_UP:
            *pick = *pick ? *pick - 1 : n - 1;
            break;
        case KC_DOWN:
            *pick = *pick + 1 < n ? *pick + 1 : 0;
            break;
        case KC_ENTER:
            return UI_PICK;
        case KC_CLEAR:
            return UI_BACK;
        case KC_QUIT:
            return UI_QUIT;
        }
        if (*pick != was) {
            draw_item(items, size, was, false);
            draw_item(items, size, *pick, true);
        }
    }
}

/* ---- tool output ---- */

struct out_line {
    char text[TEXT_COLS + 1];
    uint8_t color;
};

static struct out_line form_lines[FORM_LINES];
static uint8_t nform;
static struct out_line step_lines[STEP_LINES];
static uint8_t nsteps;
static char formatted[128];             /* one line before it wraps */

/* A small printf: %d and %u (with l or ll), %s, %c, a width, '-' and '0'. */
static void format(const char *fmt, va_list ap)
{
    char *p = formatted, *end = formatted + sizeof formatted - 1;
    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        fmt++;
        bool left = *fmt == '-';
        if (left)
            fmt++;
        char pad = *fmt == '0' ? '0' : ' ';
        size_t width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (size_t)(*fmt++ - '0');
        uint8_t longs = 0;
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }
        char num[21];
        const char *s = num;
        switch (*fmt++) {
        case 'd':
            i64_to_str(longs > 1 ? va_arg(ap, long long)
                       : longs ? va_arg(ap, long) : va_arg(ap, int), num);
            break;
        case 'u':
            i64_to_str(longs ? (int64_t)va_arg(ap, unsigned long)
                       : (int64_t)va_arg(ap, unsigned int), num);
            break;
        case 's':
            s = va_arg(ap, const char *);
            break;
        case 'c':
            num[0] = (char)va_arg(ap, int);
            num[1] = '\0';
            break;
        default:                        /* "%%" */
            num[0] = '%';
            num[1] = '\0';
            break;
        }
        size_t len = strlen(s);
        for (; !left && width > len && p < end; width--)
            *p++ = pad;
        while (*s && p < end)
            *p++ = *s++;
        for (; left && width > len && p < end; width--)
            *p++ = ' ';
    }
    *p = '\0';
}

/* Splits the formatted line at spaces into lines of the margin's width,
 * indenting the rest by two, but never right after = or ~, so "~ 0.4024"
 * stays together. A full buffer's last line says so. */
static void add_lines(struct out_line *lines, uint8_t *n, uint8_t max, uint8_t color)
{
    const char *s = formatted;
    uint8_t indent = 0;
    do {
        if (*n >= max)
            return;
        struct out_line *l = &lines[(*n)++];
        if (*n == max) {
            strcpy(l->text, "(no room for the rest)");
            l->color = COLOR_DIM;
            return;
        }
        size_t room = TEXT_COLS - indent, len = strlen(s);
        if (len > room) {
            len = room;
            while (len && (s[len] != ' ' || s[len - 1] == '=' || s[len - 1] == '~'))
                len--;
            if (!len)
                len = room;
        }
        memset(l->text, ' ', indent);
        memcpy(l->text + indent, s, len);
        l->text[indent + len] = '\0';
        l->color = color;
        s += len;
        while (*s == ' ')
            s++;
        indent = 2;
    } while (*s);
}

#define OUTPUT(lines, n, max, color)            \
    do {                                        \
        va_list ap;                             \
        va_start(ap, fmt);                      \
        format(fmt, ap);                        \
        va_end(ap);                             \
        add_lines(lines, &n, max, color);       \
    } while (0)

void ui_answer(const char *fmt, ...) { OUTPUT(form_lines, nform, FORM_LINES, COLOR_OK); }
void ui_say(const char *fmt, ...) { OUTPUT(form_lines, nform, FORM_LINES, COLOR_TEXT); }
void ui_check(const char *fmt, ...) { OUTPUT(form_lines, nform, FORM_LINES, COLOR_DIM); }
void ui_step(const char *fmt, ...) { OUTPUT(step_lines, nsteps, STEP_LINES, COLOR_TEXT); }
void ui_step_head(const char *fmt, ...) { OUTPUT(step_lines, nsteps, STEP_LINES, COLOR_DIM); }

bool ui_error(const char *fmt, ...)
{
    OUTPUT(form_lines, nform, FORM_LINES, COLOR_ERROR);
    return false;
}

/* ---- scrolling views ---- */

static void draw_view(const char *name, uint24_t lines, uint24_t top, view_line_fn line)
{
    uint8_t len, color;
    size_t name_len = strlen(name);
    draw_line(1, name, name_len, COLOR_ACCENT);
    for (uint24_t i = top < lines ? top + 1 : 0; i--;) {
        const char *s = line(i, &len, &color);
        if (color == COLOR_ACCENT) {
            draw_text((uint8_t)(1 + name_len + 3), 1, s, len, COLOR_ACCENT, COLOR_BG);
            break;
        }
    }
    for (uint8_t r = 0; r < VIEW_ROWS; r++) {
        uint24_t i = top + r;
        if (i < lines) {
            const char *s = line(i, &len, &color);
            draw_line(VIEW_TOP + r, s, len, color);
        } else {
            clear_from(0, VIEW_TOP + r, COLOR_BG);
        }
    }
    clear_from(0, VIEW_TOP + VIEW_ROWS, COLOR_BG);
    if (lines > VIEW_ROWS) {
        char *p = formatted;
        memcpy(p, "line ", 5);
        p = i64_to_str(top + 1, p + 5);
        memcpy(p, " of ", 4);
        p = i64_to_str(lines, p + 4);
        strcpy(p, ". LEFT and RIGHT page.");
        draw_line(VIEW_TOP + VIEW_ROWS, formatted, strlen(formatted), COLOR_DIM);
    }
    static const char *const soft[SOFT_KEYS] = { NULL, NULL, NULL, NULL, "Back" };
    draw_soft(soft);
}

enum run_end ui_view(const char *name, uint24_t lines, uint24_t top, view_line_fn line)
{
    uint24_t last = lines > VIEW_ROWS ? lines - VIEW_ROWS : 0;
    if (top > last)
        top = last;
    draw_view(name, lines, top, line);

    for (;;) {
        if (kernel_poll())
            return RUN_HOME;
        uint8_t raw;
        uint24_t was = top;
        switch (next_key(&raw)) {
        case KC_UP:
            if (top)
                top--;
            break;
        case KC_DOWN:
            if (top < last)
                top++;
            break;
        case KC_CLEAR:
        case KC_F5:
            return RUN_BACK;
        case KC_QUIT:
            return RUN_HOME;
        }
        if (raw == KEY_LEFT)
            top = top > VIEW_ROWS ? top - VIEW_ROWS : 0;
        else if (raw == KEY_RIGHT)
            top = top + VIEW_ROWS < last ? top + VIEW_ROWS : last;
        if (top != was)
            draw_view(name, lines, top, line);
    }
}

/* ---- study tools ---- */

static char texts[FIELDS_MAX][FIELD_MAX + 1];
static struct value values[FIELDS_MAX];
static struct frac lists[FIELDS_MAX][LIST_MAX];
static bool answered;                   /* the form shows what ENTER worked out */

static const char *step_line(uint24_t i, uint8_t *len, uint8_t *color)
{
    *len = (uint8_t)strlen(step_lines[i].text);
    *color = step_lines[i].color;
    return step_lines[i].text;
}

static uint8_t field_col(const struct field *f)
{
    return (uint8_t)(2 + strlen(f->label) + 3);     /* "> label = " */
}

static void draw_field(const struct tool *t, uint8_t i, bool current)
{
    const struct field *f = &t->fields[i];
    uint8_t row = FORM_TOP + i, col = field_col(f);
    console_draw_cell(0, row, current ? '>' : ' ', COLOR_ACCENT, COLOR_BG);
    console_draw_cell(1, row, ' ', COLOR_TEXT, COLOR_BG);
    draw_text(2, row, f->label, strlen(f->label), COLOR_TEXT, COLOR_BG);
    draw_text(col - 3, row, " = ", 3, COLOR_TEXT, COLOR_BG);
    draw_text(col, row, texts[i], strlen(texts[i]), COLOR_TEXT, COLOR_BG);
    clear_from((uint8_t)(col + strlen(texts[i])), row, COLOR_BG);
}

static void draw_output(const struct tool *t)
{
    uint8_t row = FORM_TOP + t->nfields + 1, last = SOFT_ROW - 1;
    for (uint8_t r = row; r <= last; r++)
        clear_from(0, r, COLOR_BG);
    if (!answered) {
        static const char hint[] = "DOWN: next field. ENTER: answer.";
        draw_line(row, hint, sizeof hint - 1, COLOR_DIM);
        return;
    }
    for (uint8_t i = 0; i < nform && row <= last; i++, row++) {
        if (row == last && i + 1 < nform) {
            static const char more[] = "(more than fits)";
            draw_line(row, more, sizeof more - 1, COLOR_DIM);
            break;
        }
        draw_line(row, form_lines[i].text, strlen(form_lines[i].text), form_lines[i].color);
    }
}

static void draw_form(const struct tool *t, uint8_t cur)
{
    console_clear();
    draw_line(1, t->title, strlen(t->title), COLOR_ACCENT);
    for (uint8_t i = 0; i < t->nfields; i++)
        draw_field(t, i, i == cur);
    draw_output(t);
    const char *const soft[SOFT_KEYS] = {
        "Steps", t->notes_file && open_notes ? "Notes" : NULL, NULL, NULL, "Back",
    };
    draw_soft(soft);
}

/* Parses field i into values[i]; false after ui_error(). */
static bool read_field(const struct field *f, uint8_t i)
{
    struct value *v = &values[i];
    uint8_t kind = f->kind & ~F_OPT;
    v->text = texts[i];
    v->given = texts[i][0] != '\0';
    v->list = lists[i];
    v->n = 0;
    if (!v->given)
        return true;

    if (kind == F_INT || kind == F_NUM) {
        if (!expr_eval(texts[i], NULL, &v->f))
            return ui_error("%s: %s", f->label, expr_error());
        if (kind == F_INT) {
            if (!frac_is_int(v->f))
                return ui_error("%s must be a whole number", f->label);
            v->i = v->f.num;
        }
    } else if (kind == F_LIST) {
        /* Commas inside parentheses belong to functions such as ncr(5,2). */
        for (const char *s = texts[i];; s++) {
            static char item[FIELD_MAX + 1];
            uint8_t len = 0, depth = 0;
            for (; *s && (*s != ',' || depth); s++) {
                if (*s == '(')
                    depth++;
                else if (*s == ')' && depth)
                    depth--;
                item[len++] = *s;
            }
            item[len] = '\0';
            if (v->n == LIST_MAX)
                return ui_error("%s: %d items at most", f->label, LIST_MAX);
            if (!expr_eval(item, NULL, &lists[i][v->n]))
                return ui_error("%s: %s", f->label, expr_error());
            v->n++;
            if (!*s)
                break;
        }
    }
    return true;
}

/* Reads the fields and runs the tool. Returns the field to edit next: the
 * first required one left empty, one that didn't parse, or cur. */
static uint8_t answer(const struct tool *t, uint8_t cur)
{
    for (uint8_t i = 0; i < t->nfields; i++)
        if (!(t->fields[i].kind & F_OPT) && !texts[i][0])
            return i;
    nform = nsteps = 0;
    answered = true;
    for (uint8_t i = 0; i < t->nfields; i++) {
        if (!read_field(&t->fields[i], i)) {
            draw_output(t);
            return i;
        }
    }
    static const char working[] = "working...";
    draw_line(FORM_TOP + t->nfields + 1, working, sizeof working - 1, COLOR_DIM);
    t->compute(values);
    draw_output(t);
    return cur;
}

enum run_end ui_tool(const struct tool *t)
{
    if (t->run)
        return t->run();
    for (uint8_t i = 0; i < t->nfields; i++)
        texts[i][0] = '\0';
    nform = nsteps = 0;
    answered = false;

    uint8_t cur = 0;
    draw_form(t, cur);
    for (;;) {
        const struct field *f = &t->fields[cur];
        ui_set_alpha((f->kind & ~F_OPT) == F_LETTERS);
        console_goto((uint8_t)(field_col(f) + strlen(texts[cur])), FORM_TOP + cur);
        console_set_color(COLOR_TEXT);

        uint8_t next = cur;
        switch (ui_edit_line(texts[cur], FIELD_MAX, NULL)) {
        case LINE_ENTER:
            next = answer(t, cur);
            break;
        case LINE_CLEAR:
            return RUN_BACK;
        case LINE_QUIT:
        case LINE_ON:
            return RUN_HOME;
        case LINE_KEY:
            switch (ui_line_key()) {
            case KC_UP:
                next = cur ? cur - 1 : t->nfields - 1;
                break;
            case KC_DOWN:
                next = cur + 1 < t->nfields ? cur + 1 : 0;
                break;
            case KC_F1:
                if (!nsteps)
                    break;
                if (ui_view(t->title, nsteps, 0, step_line) == RUN_HOME)
                    return RUN_HOME;
                draw_form(t, cur);
                break;
            case KC_F2:
                if (!t->notes_file || !open_notes)
                    break;
                if (open_notes(t->notes_file, t->notes_heading) == RUN_HOME)
                    return RUN_HOME;
                draw_form(t, cur);
                break;
            case KC_F5:
                return RUN_BACK;
            }
            break;
        }
        if (next != cur) {
            draw_field(t, cur, false);
            draw_field(t, next, true);
            cur = next;
        }
    }
}

void ui_set_notes(enum run_end (*open)(const char *file, const char *heading))
{
    open_notes = open;
}

enum run_end ui_section(const struct section *s)
{
    uint8_t pick = 0;
    for (;;) {
        if (ui_list(s->title, s->tools, sizeof *s->tools, s->ntools, "CLEAR goes back.",
                    &pick) != UI_PICK)
            return RUN_BACK;
        if (ui_tool(&s->tools[pick]) == RUN_HOME)
            return RUN_BACK;
    }
}
