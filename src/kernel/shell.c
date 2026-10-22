#include "shell.h"
#include "console.h"
#include "kernel.h"
#include "keymap.h"
#include "../drivers/hw.h"
#include "../drivers/keypad.h"
#include "../drivers/timer.h"

#include <string.h>

#define PROMPT     "> "
#define LINE_MAX   (CONSOLE_COLS - sizeof PROMPT)
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
    { "echo",   "TEXT...",       cmd_echo },
    { "exit",   "",              cmd_exit },
    { NULL, NULL, NULL },
};

static char line[LINE_MAX + 1];
static char last_line[LINE_MAX + 1];
static uint8_t line_len;
static struct keymap_state keys = { .alpha = true };
static bool running;

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
    console_puts("ALPHA: letters/digits  2nd+MODE: quit");
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
    for (;;) {
        keypad_scan(state);
        if ((state[KEY_CLEAR >> 3] & (1 << (KEY_CLEAR & 7))) || keypad_on_pressed())
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
    while (state[KEY_CLEAR >> 3] & (1 << (KEY_CLEAR & 7)));
    keypad_poll();
}

static void cmd_exit(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    running = false;
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
    line_len = 0;
    line[0] = '\0';
}

static void update_status(void)
{
    static char right[16];              /* longest: "2nd a 99999s" */
    static uint32_t last_second = 0xFFFFFFFF;
    static struct keymap_state last_keys;
    uint32_t second = timer_ms() / 1000;

    if (second == last_second && keys.alpha == last_keys.alpha && keys.second == last_keys.second)
        return;
    last_second = second;
    last_keys = keys;

    /* "[2nd] a  12345s" */
    uint8_t i = 0;
    if (keys.second) {
        memcpy(&right[i], "2nd ", 4);
        i += 4;
    }
    right[i++] = keys.alpha ? 'a' : '1';
    right[i++] = ' ';
    char digits[6];
    uint8_t n = 0;
    uint32_t s = second % 100000;
    do {
        digits[n++] = (char)('0' + s % 10);
        s /= 10;
    } while (s);
    while (n)
        right[i++] = digits[--n];
    right[i++] = 's';
    right[i] = '\0';

    console_status(" " KERNEL_NAME " " KERNEL_VERSION, right);
}

static void replace_line(const char *text)
{
    while (line_len) {
        console_backspace();
        line_len--;
    }
    for (; *text && line_len < LINE_MAX; text++) {
        line[line_len++] = *text;
        console_putc(*text);
    }
    line[line_len] = '\0';
}

void shell_run(void)
{
    running = true;
    update_status();
    console_set_color(COLOR_ACCENT);
    console_puts(KERNEL_NAME " " KERNEL_VERSION);
    console_newline();
    console_set_color(COLOR_DIM);
    console_puts("type help and press ENTER");
    console_newline();
    console_set_color(COLOR_TEXT);
    prompt();

    while (running) {
        update_status();
        if (keypad_on_pressed() && kernel_boot->can_exit)
            break;

        char c = keymap_translate(&keys, keypad_poll());
        switch (c) {
        case KC_NONE:
            break;
        case KC_QUIT:
            running = false;
            break;
        case KC_ENTER:
            console_newline();
            if (line_len)
                memcpy(last_line, line, sizeof line);
            execute(line);
            if (running)
                prompt();
            break;
        case KC_BACKSPACE:
            if (line_len) {
                line[--line_len] = '\0';
                console_backspace();
            }
            break;
        case KC_CLEAR:
            replace_line("");
            break;
        case KC_HISTORY:
            replace_line(last_line);
            break;
        default:
            if (line_len < LINE_MAX) {
                line[line_len++] = c;
                line[line_len] = '\0';
                console_putc(c);
            }
            break;
        }
    }

    if (!kernel_boot->can_exit) {
        console_newline();
        console_puts("halted; nothing to return to");
    }
}
