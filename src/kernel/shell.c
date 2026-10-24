#include "shell.h"
#include "archive.h"
#include "calc.h"
#include "console.h"
#include "events.h"
#include "kernel.h"
#include "../drivers/hw.h"
#include "../drivers/keypad.h"
#include "../drivers/timer.h"

#include <string.h>

#define PROMPT     "> "
#define MAX_ARGS   4

/* Placed by the linker script (toolchain script for the TI-OS build,
 * rom/rom.ld for the bare ROM build). */
extern uint8_t __bss_low[], __bss_high[], __heap_low[], __heap_high[];

struct command {
    const char *name;
    const char *usage;
    void (*run)(uint8_t argc, char **argv);
};

static void cmd_help(uint8_t argc, char **argv);
static void cmd_about(uint8_t argc, char **argv);
static void cmd_clear(uint8_t argc, char **argv);
static void cmd_mem(uint8_t argc, char **argv);
static void cmd_uptime(uint8_t argc, char **argv);
static void cmd_keys(uint8_t argc, char **argv);
static void cmd_peek(uint8_t argc, char **argv);
static void cmd_ls(uint8_t argc, char **argv);
static void cmd_cat(uint8_t argc, char **argv);
static void cmd_calc(uint8_t argc, char **argv);
static void cmd_echo(uint8_t argc, char **argv);
static void cmd_exit(uint8_t argc, char **argv);

static const struct command commands[] = {
    { "help",   "",              cmd_help },
    { "about",  "",              cmd_about },
    { "clear",  "",              cmd_clear },
    { "mem",    "",              cmd_mem },
    { "uptime", "",              cmd_uptime },
    { "keys",   "",              cmd_keys },
    { "peek",   "ADDR [LEN]",    cmd_peek },
    { "ls",     "",              cmd_ls },
    { "cat",    "NAME",          cmd_cat },
    { "calc",   "",              cmd_calc },
    { "echo",   "TEXT...",       cmd_echo },
    { "exit",   "",              cmd_exit },
    { NULL, NULL, NULL },
};

static char line[SHELL_LINE_MAX + 1];
static char last_line[SHELL_LINE_MAX + 1];
static bool running, exiting;

static void print_error(const char *msg)
{
    console_set_color(COLOR_ERROR);
    console_puts(msg);
    console_newline();
    console_set_color(COLOR_TEXT);
}

static bool parse_hex(const char *s, uint24_t *out)
{
    uint24_t value = 0;
    if (!*s)
        return false;
    for (; *s; s++) {
        char c = *s;
        uint8_t digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return false;
        if (value >> 20)
            return false;               /* would overflow 24 bits */
        value = value << 4 | digit;
    }
    *out = value;
    return true;
}

/* Reading I/O registers can have side effects (some clear status bits), so
 * peek only reads plain memory. */
static bool readable(uint24_t start, uint24_t len)
{
    uint24_t end = start + len;
    if (end < start)
        return false;                   /* wrapped past 0xFFFFFF */
    return end <= FLASH_END || (start >= RAM_START && end <= RAM_END);
}

static void put_range(const char *label, uint24_t start, uint24_t end)
{
    console_puts(label);
    console_puthex(start, 6);
    console_putc('-');
    console_puthex(end, 6);
    console_puts("  ");
    console_putdec(end - start);
    console_newline();
}

static void cmd_help(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (const struct command *c = commands; c->name; c++) {
        console_set_color(COLOR_ACCENT);
        console_puts(c->name);
        console_set_color(COLOR_DIM);
        console_putc(' ');
        console_puts(c->usage);
        console_newline();
    }
    console_set_color(COLOR_DIM);
    console_puts("ALPHA: letters/digits  2nd+MODE: menu");
    console_newline();
    console_set_color(COLOR_TEXT);
}

static void cmd_clear(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_clear();
}

static void cmd_about(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_set_color(COLOR_ACCENT);
    console_puts(KERNEL_NAME " " KERNEL_VERSION);
    console_newline();
    console_set_color(COLOR_TEXT);
    console_puts("loader: ");
    console_puts(kernel_boot->loader);
    console_newline();
    console_puts("open-source takeover kernel for the");
    console_newline();
    console_puts("TI-84 Plus CE family");
    console_newline();
}

static void cmd_mem(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_set_color(COLOR_DIM);
    console_puts("region  start  end     bytes");
    console_newline();
    console_set_color(COLOR_TEXT);
    put_range("image   ", kernel_boot->image_start, kernel_boot->image_end);
    put_range("bss     ", (uint24_t)__bss_low, (uint24_t)__bss_high);
    put_range("free    ", (uint24_t)__heap_low, (uint24_t)__heap_high);
    put_range("screen  ", VRAM_START, VRAM_START + 320 * 240);
}

static void cmd_uptime(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t ms = timer_ms();
    console_putdec(ms / 1000);
    console_putc('.');
    uint24_t frac = ms % 1000;
    console_putc((char)('0' + frac / 100));
    console_putc((char)('0' + frac / 10 % 10));
    console_putc((char)('0' + frac % 10));
    console_puts(" s");
    console_newline();
}

static void cmd_echo(uint8_t argc, char **argv)
{
    for (uint8_t i = 1; i < argc; i++) {
        if (i > 1)
            console_putc(' ');
        console_puts(argv[i]);
    }
    console_newline();
}

static void dump(uint24_t addr, uint24_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    for (uint24_t i = 0; i < len; i++) {
        if (i % 8 == 0) {
            if (i)
                console_newline();
            console_set_color(COLOR_DIM);
            console_puthex(addr + i, 6);
            console_putc(':');
            console_set_color(COLOR_TEXT);
        }
        console_putc(' ');
        console_puthex(p[i], 2);
    }
    console_newline();
}

static void cmd_peek(uint8_t argc, char **argv)
{
    uint24_t addr, len = 64;
    if (argc < 2 || !parse_hex(argv[1], &addr) || (argc > 2 && !parse_hex(argv[2], &len))) {
        print_error("usage: peek ADDR [LEN]  (hex)");
        return;
    }
    if (len == 0 || len > 0x100) {
        print_error("LEN must be 1-100 (hex)");
        return;
    }
    if (!readable(addr, len)) {
        print_error("only flash and RAM can be read");
        return;
    }
    dump(addr, len);
}

/* Up to this much of a variable is shown; peek reads the rest. */
#define CAT_MAX 0x80

static const char *type_name(uint8_t type)
{
    switch (type) {
    case 0x00: return "real";
    case 0x01: return "list";
    case 0x02: return "matrix";
    case 0x03: return "equ";
    case 0x04: return "string";
    case 0x05: return "prog";
    case 0x06: return "pprog";
    case 0x07: return "pic";
    case 0x08: return "gdb";
    case 0x0C: return "cplx";
    case 0x0D: return "clist";
    case 0x15: return "appvar";
    case 0x17: return "group";
    case 0x1A: return "image";
    default:   return NULL;
    }
}

static void pad_to(uint8_t col)
{
    while (console_column() < col)
        console_putc(' ');
}

static void cmd_ls(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_set_color(COLOR_DIM);
    console_puts("type   name     bytes  at");
    console_newline();
    console_set_color(COLOR_TEXT);

    struct archive_entry e;
    uint24_t cursor = 0, count = 0;
    while (archive_next(&cursor, &e)) {
        const char *type = type_name(e.type);
        if (type) {
            console_puts(type);
        } else {
            console_puts("type");
            console_puthex(e.type, 2);
        }
        pad_to(7);
        /* Names of lists, matrices and the like are tokens, not text. */
        for (uint8_t i = 0; i < e.name_len; i++)
            console_putc(e.name[i] >= ' ' && e.name[i] <= '~' ? e.name[i] : '?');
        pad_to(16);
        console_putdec(e.size);
        pad_to(23);
        console_puthex((uint24_t)e.data, 6);
        console_newline();
        count++;
    }
    console_putdec(count);
    console_puts(count == 1 ? " file" : " files");
    console_newline();
}

static void cmd_cat(uint8_t argc, char **argv)
{
    struct archive_entry e;
    if (argc != 2) {
        print_error("usage: cat NAME");
        return;
    }
    if (!archive_find(argv[1], &e)) {
        console_set_color(COLOR_ERROR);
        console_puts("not in archive: ");
        console_puts(argv[1]);
        console_newline();
        console_set_color(COLOR_TEXT);
        return;
    }
    if (e.size)
        dump((uint24_t)e.data, e.size < CAT_MAX ? e.size : CAT_MAX);
    if (e.size > CAT_MAX) {
        console_set_color(COLOR_DIM);
        console_puts("first ");
        console_putdec(CAT_MAX);
        console_puts(" of ");
        console_putdec(e.size);
        console_puts(" bytes; peek for more");
        console_newline();
        console_set_color(COLOR_TEXT);
    }
}

/* Live view of the key matrix, for checking the keypad driver. */
static void cmd_keys(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_puts("matrix view, CLEAR to stop");
    console_newline();
    for (uint8_t g = 1; g < KEYPAD_GROUPS; g++)
        console_newline();

    uint8_t state[KEYPAD_GROUPS];
    while (!kernel_poll()) {
        keypad_scan(state);
        if (state[KEY_CLEAR >> 3] & (1 << (KEY_CLEAR & 7)))
            break;
        for (uint8_t g = 1; g < KEYPAD_GROUPS; g++) {
            uint8_t row = CONSOLE_ROWS - KEYPAD_GROUPS + g;
            console_draw_cell(0, row, (char)('0' + g), COLOR_DIM, COLOR_BG);
            for (uint8_t bit = 0; bit < 8; bit++) {
                bool down = state[g] & (1 << bit);
                console_draw_cell(2 + bit * 2, row, down ? '#' : '.',
                                  down ? COLOR_OK : COLOR_DIM, COLOR_BG);
            }
        }
    }
    /* Wait for CLEAR to come back up so the shell doesn't see it. */
    do
        keypad_scan(state);
    while ((state[KEY_CLEAR >> 3] & (1 << (KEY_CLEAR & 7))) && !kernel_poll());
    keypad_poll();
}

static void cmd_calc(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    calc_run();
}

static void cmd_exit(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    running = false;
    exiting = true;
}

static void execute(char *text)
{
    char *argv[MAX_ARGS];
    uint8_t argc = 0;

    for (char *p = text; *p && argc < MAX_ARGS;) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    if (argc == 0)
        return;

    for (const struct command *c = commands; c->name; c++) {
        if (strcmp(c->name, argv[0]) == 0) {
            c->run(argc, argv);
            return;
        }
    }
    console_set_color(COLOR_ERROR);
    console_puts("unknown command: ");
    console_puts(argv[0]);
    console_newline();
    console_set_color(COLOR_TEXT);
}

static void prompt(void)
{
    console_set_color(COLOR_ACCENT);
    console_puts(PROMPT);
    console_set_color(COLOR_TEXT);
    line[0] = '\0';
}

enum run_end shell_run(void)
{
    running = true;
    exiting = false;
    ui_set_alpha(true);
    console_set_color(COLOR_ACCENT);
    console_puts(KERNEL_NAME " " KERNEL_VERSION);
    console_newline();
    console_set_color(COLOR_DIM);
    console_puts("type help and press ENTER");
    console_newline();
    console_set_color(COLOR_TEXT);
    prompt();

    while (running) {
        switch (ui_edit_line(line, SHELL_LINE_MAX, last_line)) {
        case LINE_ENTER:
            execute(line);
            if (running)
                prompt();
            break;
        case LINE_ON:                   /* commands stop on ON themselves */
        case LINE_QUIT:
            /* Back to the menu; only the exit command leaves the kernel. */
            running = false;
            break;
        case LINE_CLEAR:
        case LINE_KEY:
            break;
        }
    }
    return exiting ? RUN_EXIT : RUN_BACK;
}
