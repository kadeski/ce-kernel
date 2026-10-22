#include "calc.h"
#include "console.h"
#include "shell.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Numbers are 32-bit signed integers. Each step is computed in 64 bits and
 * then checked, so a result out of range is an error rather than a wrapped
 * value. The toolchain's float routines call TI's boot code, so there are no
 * decimals.
 */

#define PROMPT "= "
#define NEG    'n'                      /* unary minus on the operator stack */

static char expr[SHELL_LINE_MAX + 1];
static char last_expr[SHELL_LINE_MAX + 1];

/* Operator precedence with explicit stacks instead of recursion: the TI-OS
 * build has about 4 KB of stack, and a line can nest 37 parentheses deep.
 * Each character pushes at most one value or operator, so a line's length
 * bounds both stacks. */
static int32_t values[SHELL_LINE_MAX];
static char ops[SHELL_LINE_MAX];
static uint8_t nvalues, nops;
static const char *error;

static bool fail(const char *msg)
{
    error = msg;
    return false;
}

static bool fits(int64_t v)
{
    return v >= INT32_MIN && v <= INT32_MAX;
}

/* Binds tighter the higher it is; '(' is 0 so nothing pops past it. */
static uint8_t precedence(char op)
{
    switch (op) {
    case '+': case '-': return 1;
    case '*': case '/': return 2;
    case NEG:           return 3;
    case '^':           return 4;
    default:            return 0;
    }
}

/* base^exp for exp >= 0, by squaring so it takes at most 31 steps. If a
 * square overflows while exponent bits remain, the result would too. */
static bool power(int64_t base, int32_t exp, int64_t *out)
{
    int64_t result = 1;
    while (exp) {
        if (exp & 1) {
            result *= base;
            if (!fits(result))
                return false;
        }
        exp >>= 1;
        if (exp) {
            base *= base;
            if (!fits(base))
                return false;
        }
    }
    *out = result;
    return true;
}

/* Pops the top operator and its operands and pushes the result. */
static bool apply(void)
{
    char op = ops[--nops];
    int64_t b = values[--nvalues];
    int64_t r;

    if (op == NEG) {
        r = -b;
    } else {
        int64_t a = values[--nvalues];
        switch (op) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/':
            if (b == 0)
                return fail("divide by zero");
            r = a / b;                  /* C truncates toward zero */
            break;
        default:                        /* '^' */
            if (b < 0)
                return fail("negative exponent");
            if (!power(a, (int32_t)b, &r))
                return fail("overflow");
            break;
        }
    }
    if (!fits(r))
        return fail("overflow");
    values[nvalues++] = (int32_t)r;
    return true;
}

static bool evaluate(const char *s, int32_t *result)
{
    bool operand = true;                /* expecting a number, '(' or unary minus */
    nvalues = nops = 0;

    for (char c; (c = *s++);) {
        if (c >= '0' && c <= '9') {
            if (!operand)
                return fail("syntax error");
            int64_t v = c - '0';
            for (; *s >= '0' && *s <= '9'; s++)
                if ((v = v * 10 + (*s - '0')) > INT32_MAX)
                    return fail("overflow");
            values[nvalues++] = (int32_t)v;
            operand = false;
        } else if (c == '(') {
            if (!operand)
                return fail("syntax error");
            ops[nops++] = '(';
        } else if (c == ')') {
            if (operand)
                return fail("syntax error");
            while (nops && ops[nops - 1] != '(')
                if (!apply())
                    return false;
            if (!nops)
                return fail("missing (");
            nops--;
        } else if (c == '-' && operand) {
            ops[nops++] = NEG;
        } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            if (operand)
                return fail("syntax error");
            /* Equal precedence pops too (left to right), except for '^',
             * which groups right to left. */
            uint8_t p = precedence(c);
            while (nops && (precedence(ops[nops - 1]) > p
                            || (precedence(ops[nops - 1]) == p && c != '^')))
                if (!apply())
                    return false;
            ops[nops++] = c;
            operand = true;
        } else if (c == '.' || c == ',') {
            return fail("integers only");
        } else {
            return fail("syntax error");
        }
    }
    if (operand)
        return fail("syntax error");
    while (nops) {
        if (ops[nops - 1] == '(')
            return fail("missing )");
        if (!apply())
            return false;
    }
    *result = values[0];
    return true;
}

static void put_int(int32_t v)
{
    if (v < 0) {
        console_putc('-');
        console_putdec(-(uint32_t)v);   /* also right for INT32_MIN */
    } else {
        console_putdec((uint32_t)v);
    }
}

void calc_run(void)
{
    console_set_color(COLOR_DIM);
    console_puts("integer math; CLEAR or ON to leave");
    console_newline();
    shell_set_alpha(false);

    for (;;) {
        console_set_color(COLOR_ACCENT);
        console_puts(PROMPT);
        console_set_color(COLOR_TEXT);
        expr[0] = '\0';
        if (shell_edit_line(expr, last_expr) != LINE_ENTER)
            break;
        if (!expr[0])
            continue;

        int32_t result;
        if (evaluate(expr, &result)) {
            put_int(result);
        } else {
            console_set_color(COLOR_ERROR);
            console_puts(error);
            console_set_color(COLOR_TEXT);
        }
        console_newline();
    }

    /* The cursor is still after the prompt. */
    console_newline();
    shell_set_alpha(true);
}
