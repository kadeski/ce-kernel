#include "expr.h"
#include "imath.h"

#include <string.h>

#define NEG '~'                         /* unary minus on the operator stack */

struct function {
    const char *name;
    uint8_t args;
};

static const struct function functions[] = {
    { "ncr", 2 }, { "npr", 2 }, { "fact", 1 }, { "gcd", 2 }, { "lcm", 2 },
    { "div", 2 }, { "mod", 2 }, { "inv", 2 }, { "powmod", 3 },
};
#define FUNCTIONS (sizeof functions / sizeof functions[0])

static const char var_names[] = "abn";  /* vars[0], vars[1], vars[2] */

/* Operator precedence with explicit stacks instead of recursion: the TI-OS
 * build has about 4 KB of stack. Each character pushes at most one value,
 * and at most two operators (an implied * and a parenthesis). */
static struct frac values[EXPR_LEN_MAX];
static char ops[2 * EXPR_LEN_MAX];
static uint8_t fn_of[2 * EXPR_LEN_MAX];         /* for '(': 1 + the function, or 0 */
static uint8_t args_of[2 * EXPR_LEN_MAX];       /* for '(': values between its commas */
static uint8_t nvalues, nops;
static const char *error;

static bool fail(const char *msg)
{
    error = msg;
    return false;
}

const char *expr_error(void)
{
    return error;
}

const char *expr_function(uint8_t i)
{
    return i < FUNCTIONS ? functions[i].name : NULL;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_letter(char c)
{
    return c >= 'a' && c <= 'z';
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

/* Digits with at most one point: 0.015 is 15/1000. */
static bool read_number(const char **s, struct frac *out)
{
    int64_t num = 0, den = 1;
    bool point = false, digits = false;
    for (const char *p = *s;; p++) {
        if (*p == '.' && !point) {
            point = true;
        } else if (is_digit(*p)) {
            if (num > (INT64_MAX - 9) / 10 || (point && den > INT64_MAX / 10))
                return fail("number too long");
            num = num * 10 + (*p - '0');
            if (point)
                den *= 10;
            digits = true;
        } else {
            *s = p;
            break;
        }
    }
    if (!digits)
        return fail("syntax error");
    return frac_make(num, den, out);
}

static bool whole(const struct frac *v, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
        if (!frac_is_int(v[i]))
            return false;
    return true;
}

/* Replaces a function's arguments, the top values, with its result. */
static bool call(uint8_t fn, uint8_t args)
{
    if (args != functions[fn].args)
        return fail(functions[fn].args == 1 ? "takes 1 number"
                    : functions[fn].args == 2 ? "takes 2 numbers" : "takes 3 numbers");
    nvalues -= args;
    struct frac *v = &values[nvalues];
    if (!whole(v, args))
        return fail("whole numbers only");
    int64_t a = v[0].num, b = args > 1 ? v[1].num : 0, c = args > 2 ? v[2].num : 0, r;
    bool ok = true;

    switch (fn) {
    case 0:                             /* ncr */
    case 1:                             /* npr */
        if (a < 0 || b < 0)
            return fail("negative count");
        ok = fn == 0 ? i64_choose(a, b, &r) : i64_perm(a, b, &r);
        break;
    case 2:                             /* fact */
        if (a < 0)
            return fail("negative count");
        ok = i64_fact(a, &r);
        break;
    case 3:                             /* gcd */
        r = i64_gcd(a, b);
        break;
    case 4:                             /* lcm */
        r = a && b ? i64_abs(a / i64_gcd(a, b)) : 0;
        ok = i64_mul(r, i64_abs(b), &r);
        break;
    case 5:                             /* div */
    case 6: {                           /* mod */
        if (b <= 0)
            return fail("N must be positive");
        int64_t q;
        i64_divmod(a, b, &q, &r);
        if (fn == 5)
            r = q;
        break;
    }
    case 7:                             /* inv */
        if (b < 2)
            return fail("N must be 2 or more");
        if (!invmod(a, b, &r))
            return fail("no inverse: gcd is not 1");
        break;
    default:                            /* powmod */
        if (b < 0)
            return fail("negative exponent");
        if (c <= 0)
            return fail("N must be positive");
        r = powmod(a, b, c);
        break;
    }
    if (!ok)
        return fail("overflow");
    values[nvalues++] = frac_int(r);
    return true;
}

/* Pops the top operator and its operands and pushes the result. */
static bool apply(void)
{
    char op = ops[--nops];
    struct frac b = values[--nvalues], r;

    if (op == NEG) {
        b.num = -b.num;
        values[nvalues++] = b;
        return true;
    }
    struct frac a = values[--nvalues];
    bool ok;
    switch (op) {
    case '+': ok = frac_add(a, b, &r); break;
    case '-': ok = frac_sub(a, b, &r); break;
    case '*': ok = frac_mul(a, b, &r); break;
    case '/':
        if (b.num == 0)
            return fail("divide by zero");
        ok = frac_div(a, b, &r);
        break;
    default:                            /* '^' */
        if (!frac_is_int(b))
            return fail("whole exponents only");
        if (a.num == 0 && b.num < 0)
            return fail("divide by zero");
        ok = frac_pow(a, b.num, &r);
        break;
    }
    if (!ok)
        return fail("overflow");
    values[nvalues++] = r;
    return true;
}

static void push_paren(uint8_t fn)
{
    ops[nops] = '(';
    fn_of[nops] = fn;
    args_of[nops] = 1;
    nops++;
}

/* Applies operators down to the innermost '(', leaving it on the stack. */
static bool close_group(void)
{
    while (nops && ops[nops - 1] != '(')
        if (!apply())
            return false;
    return nops ? true : fail("missing (");
}

static bool push_binary(char c)
{
    /* Equal precedence pops too (left to right), except for '^', which
     * groups right to left. */
    uint8_t p = precedence(c);
    while (nops && (precedence(ops[nops - 1]) > p
                    || (precedence(ops[nops - 1]) == p && c != '^')))
        if (!apply())
            return false;
    ops[nops++] = c;
    return true;
}

bool expr_eval(const char *s, const struct frac *vars, struct frac *out)
{
    bool operand = true;                /* expecting a value, '(' or unary minus */
    nvalues = nops = 0;
    if (strlen(s) > EXPR_LEN_MAX)
        return fail("too long");

    while (*s) {
        char c = *s;
        if (c == ' ') {
            s++;
            continue;
        }
        if (!operand && (c == '(' || is_letter(c))) {
            if (!push_binary('*'))
                return false;
            operand = true;
        }
        if (is_digit(c) || c == '.') {
            if (!operand)
                return fail("syntax error");
            if (!read_number(&s, &values[nvalues++]))
                return false;
            operand = false;
            continue;
        }
        if (is_letter(c)) {
            const char *name = s;
            while (is_letter(*s))
                s++;
            size_t len = (size_t)(s - name);
            uint8_t fn = 0;
            while (fn < FUNCTIONS && (strlen(functions[fn].name) != len
                                      || memcmp(functions[fn].name, name, len)))
                fn++;
            if (fn < FUNCTIONS) {
                if (*s++ != '(')
                    return fail("( must follow the name");
                push_paren((uint8_t)(fn + 1));
                continue;
            }
            const char *var = len == 1 && vars ? strchr(var_names, *name) : NULL;
            if (!var)
                return fail("unknown name");
            values[nvalues++] = vars[var - var_names];
            operand = false;
            continue;
        }

        s++;
        switch (c) {
        case '(':
            push_paren(0);
            break;
        case ',':
            if (operand)
                return fail("syntax error");
            if (!close_group())
                return false;
            if (!fn_of[nops - 1])
                return fail("comma outside a function");
            args_of[nops - 1]++;
            operand = true;
            break;
        case ')':
            if (operand)
                return fail("syntax error");
            if (!close_group())
                return false;
            nops--;
            if (fn_of[nops] && !call((uint8_t)(fn_of[nops] - 1), args_of[nops]))
                return false;
            operand = false;
            break;
        case '-':
            if (operand) {
                ops[nops++] = NEG;
                break;
            }
            /* fall through */
        case '+': case '*': case '/': case '^':
            if (operand)
                return fail("syntax error");
            if (!push_binary(c))
                return false;
            operand = true;
            break;
        default:
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
    *out = values[0];
    return true;
}
