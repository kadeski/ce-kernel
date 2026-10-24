#include "notes.h"
#include "archive.h"
#include "console.h"

#include <string.h>

#define TYPE_APPVAR 0x15
#define NOTES_LINES 512                 /* the longest notes file has 241 */
#define FILES_MAX   9                   /* what a list can number */

/* The open file, read in place: only where each line starts is kept. */
static const uint8_t *starts[NOTES_LINES];
static const uint8_t *text_end;
static uint24_t nlines;
static char shown[ARCHIVE_NAME_MAX + 1];

/*
 * Finds the script in a Python AppVar. Its data is the AppVar's own 16-bit
 * size, the magic PYCD or PYSC, metadata records (a length of 1 or 2 bytes,
 * 7 bits each, low first, then that many bytes; the first is its type), a 0
 * byte, and the script, maybe after a UTF-8 BOM. The layout comes from
 * tivars_lib_cpp, not from TI. Returns NULL for anything else.
 */
static const uint8_t *script(const struct archive_entry *e, const uint8_t **end)
{
    if (e->type != TYPE_APPVAR || e->size < 2 + 5)
        return NULL;
    const uint8_t *p = e->data;
    uint24_t size = p[0] | (uint24_t)p[1] << 8;
    if (size > e->size - 2)
        return NULL;
    const uint8_t *stop = p + 2 + size;
    p += 2;
    if (memcmp(p, "PYCD", 4) && memcmp(p, "PYSC", 4))
        return NULL;
    for (p += 4; p < stop && *p;) {
        uint24_t len = *p & 0x7F;
        if (*p++ & 0x80) {
            if (p == stop)
                return NULL;
            len |= (uint24_t)(*p++ & 0x7F) << 7;
        }
        if (len > (uint24_t)(stop - p))
            return NULL;
        p += len;
    }
    if (p == stop)
        return NULL;
    p++;
    if (stop - p >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        p += 3;
    *end = stop;
    return p;
}

/* The first archived Python file called name. */
static bool find(const char *name, struct archive_entry *e)
{
    size_t len = strlen(name);
    const uint8_t *end;
    for (uint24_t cursor = 0; archive_next(&cursor, e);)
        if (e->name_len == len && !memcmp(e->name, name, len) && script(e, &end))
            return true;
    return false;
}

static void load(const struct archive_entry *e)
{
    const uint8_t *end, *p = script(e, &end);
    for (nlines = 0; p < end && nlines < NOTES_LINES;) {
        starts[nlines++] = p;
        while (p < end && *p != '\r' && *p != '\n')
            p++;
        if (p < end && *p == '\r')
            p++;
        if (p < end && *p == '\n')
            p++;
    }
    text_end = end;
    memcpy(shown, e->name, e->name_len);
    shown[e->name_len] = '\0';
}

/* Line i without the "# " that keeps it from running. "==" starts a heading. */
static const char *note_line(uint24_t i, uint8_t *len, uint8_t *color)
{
    const uint8_t *p = starts[i], *q = p;
    while (q < text_end && *q != '\r' && *q != '\n')
        q++;
    if (p < q && *p == '#')
        p++;
    if (p < q && *p == ' ')
        p++;
    *len = (uint8_t)(q - p > 255 ? 255 : q - p);
    *color = q - p >= 2 && p[0] == '=' && p[1] == '=' ? COLOR_ACCENT : COLOR_TEXT;
    return (const char *)p;
}

static uint24_t find_heading(const char *heading)
{
    size_t want = strlen(heading);
    for (uint24_t i = 0; i < nlines; i++) {
        uint8_t len, color;
        const char *s = note_line(i, &len, &color);
        if (color != COLOR_ACCENT)
            continue;
        for (; len && (*s == '=' || *s == ' '); len--)
            s++;
        if (len >= want && !memcmp(s, heading, want))
            return i;
    }
    return 0;
}

static const char *message[2];

static const char *message_line(uint24_t i, uint8_t *len, uint8_t *color)
{
    *len = (uint8_t)strlen(message[i]);
    *color = i ? COLOR_DIM : COLOR_TEXT;
    return message[i];
}

static enum run_end show_message(const char *title, const char *line, const char *hint)
{
    message[0] = line;
    message[1] = hint;
    return ui_view(title, 2, 0, message_line);
}

enum run_end notes_open(const char *name, const char *heading)
{
    struct archive_entry e;
    if (!find(name, &e))
        return show_message(name, "This notes file is not in the archive.",
                            "Send it archived, from TI Connect CE.");
    load(&e);
    return ui_view(shown, nlines, heading ? find_heading(heading) : 0, note_line);
}

struct note_file {
    const char *label;                  /* first, for ui_list */
    char name[ARCHIVE_NAME_MAX + 1];
};

static struct note_file files[FILES_MAX];

enum run_end notes_run(void)
{
    struct archive_entry e;
    const uint8_t *end;
    uint8_t n = 0;
    for (uint24_t cursor = 0; n < FILES_MAX && archive_next(&cursor, &e);) {
        if (e.name[e.name_len - 1] != 'R' || !script(&e, &end))
            continue;
        memcpy(files[n].name, e.name, e.name_len);
        files[n].name[e.name_len] = '\0';
        files[n].label = files[n].name;
        n++;
    }
    if (!n)
        return show_message("Notes", "No notes in the archive.",
                            "It lists Python files ending in R.");

    uint8_t pick = 0;
    for (;;) {
        if (ui_list("Notes", files, sizeof *files, n, "CLEAR goes back.", &pick) != UI_PICK)
            return RUN_BACK;
        if (notes_open(files[pick].name, NULL) == RUN_HOME)
            return RUN_BACK;
    }
}
