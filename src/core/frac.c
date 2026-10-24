#include "frac.h"
#include "imath.h"

#define PLACES     4                    /* decimal places shown, at least */
#define MAX_PLACES 12
#define BUFFERS    6                    /* frac_str results alive at once */
#define BUFFER_LEN 80                   /* "-n/d ~ -i." and MAX_PLACES digits */

struct frac frac_int(int64_t v)
{
    struct frac f = { v, 1 };
    return f;
}

bool frac_make(int64_t num, int64_t den, struct frac *out)
{
    if (den == 0)
        return false;
    if (den < 0) {
        num = -num;
        den = -den;
    }
    int64_t g = i64_gcd(num, den);
    out->num = num / g;
    out->den = den / g;
    return true;
}

bool frac_is_int(struct frac a)
{
    return a.den == 1;
}

bool frac_add(struct frac a, struct frac b, struct frac *out)
{
    int64_t g = i64_gcd(a.den, b.den), x, y, den;
    return i64_mul(a.num, b.den / g, &x) && i64_mul(b.num, a.den / g, &y)
        && i64_add(x, y, &x) && i64_mul(a.den, b.den / g, &den)
        && frac_make(x, den, out);
}

bool frac_sub(struct frac a, struct frac b, struct frac *out)
{
    b.num = -b.num;
    return frac_add(a, b, out);
}

bool frac_mul(struct frac a, struct frac b, struct frac *out)
{
    int64_t g1 = i64_gcd(a.num, b.den), g2 = i64_gcd(b.num, a.den), num, den;
    return i64_mul(a.num / g1, b.num / g2, &num)
        && i64_mul(a.den / g2, b.den / g1, &den)
        && frac_make(num, den, out);
}

bool frac_div(struct frac a, struct frac b, struct frac *out)
{
    struct frac inv;
    return frac_make(b.den, b.num, &inv) && frac_mul(a, inv, out);
}

bool frac_pow(struct frac a, int64_t exp, struct frac *out)
{
    if (exp < 0) {
        if (!frac_make(a.den, a.num, &a))
            return false;
        exp = -exp;
    }
    int64_t num, den;
    return i64_pow(a.num, exp, &num) && i64_pow(a.den, exp, &den)
        && frac_make(num, den, out);
}

/* The binary digits of the fraction part come from doubling the remainder,
 * which stays below den, so nothing overflows. */
bool frac_round(struct frac a, uint8_t bits, struct frac *out)
{
    uint64_t den = (uint64_t)a.den, r = (uint64_t)a.num % den;
    int64_t num = a.num / a.den;
    for (uint8_t i = 0; i < bits; i++) {
        r *= 2;
        if (!i64_mul(num, 2, &num))
            return false;
        if (r >= den) {
            r -= den;
            num++;
        }
    }
    if (r >= den - r)
        num++;
    return frac_make(num, (int64_t)1 << bits, out);
}

/* Compares whole parts, then the fractional parts upside down, as in a
 * continued fraction, so nothing is ever multiplied. */
int8_t frac_cmp(struct frac a, struct frac b)
{
    int8_t sign = 1;
    for (;;) {
        int64_t qa, ra, qb, rb;
        i64_divmod(a.num, a.den, &qa, &ra);
        i64_divmod(b.num, b.den, &qb, &rb);
        if (qa != qb)
            return qa < qb ? -sign : sign;
        if (!ra || !rb)
            return ra == rb ? 0 : ra ? sign : -sign;
        /* ra/a.den < rb/b.den exactly when a.den/ra > b.den/rb. */
        struct frac na = { a.den, ra }, nb = { b.den, rb };
        a = na;
        b = nb;
        sign = -sign;
    }
}

static char *next_buffer(void)
{
    static char buffers[BUFFERS][BUFFER_LEN];
    static uint8_t next;
    next = (uint8_t)((next + 1) % BUFFERS);
    return buffers[next];
}

static char *put_frac(char *p, struct frac f)
{
    p = i64_to_str(f.num, p);
    if (f.den != 1) {
        *p++ = '/';
        p = i64_to_str(f.den, p);
    }
    return p;
}

const char *frac_str(struct frac f)
{
    char *buf = next_buffer();
    put_frac(buf, f);
    return buf;
}

/* The next decimal digit of r/den, leaving the remainder in r: 10r is
 * built by adding r ten times, so it never overflows even when den is near
 * INT64_MAX. */
static uint8_t next_digit(uint64_t *r, uint64_t den)
{
    uint64_t t = 0;
    uint8_t d = 0;
    for (uint8_t i = 0; i < 10; i++) {
        if (t >= den - *r) {
            t -= den - *r;
            d++;
        } else {
            t += *r;
        }
    }
    *r = t;
    return d;
}

/* "= " or "~ " and the decimal: PLACES decimal places, or three
 * significant digits for a small value, rounded half up. */
static void put_decimal(char *p, struct frac f)
{
    uint64_t den = (uint64_t)f.den, mag = (uint64_t)i64_abs(f.num);
    int64_t whole = (int64_t)(mag / den);
    uint64_t r = mag % den;
    uint8_t digits[MAX_PLACES], n = 0;
    int8_t first = whole ? 0 : -1;      /* the first significant digit */
    while (r && n < MAX_PLACES) {
        digits[n] = next_digit(&r, den);
        if (first < 0 && digits[n])
            first = (int8_t)n;
        n++;
        if (first >= 0 && n >= PLACES && n >= first + 3)
            break;
    }
    bool exact = r == 0;
    if (!exact && r >= den - r) {
        uint8_t i = n;
        while (i && digits[i - 1] == 9)
            digits[--i] = 0;
        if (i)
            digits[i - 1]++;
        else
            whole++;
    }

    *p++ = exact ? '=' : '~';
    *p++ = ' ';
    if (f.num < 0)
        *p++ = '-';
    p = i64_to_str(whole, p);
    if (n)
        *p++ = '.';
    for (uint8_t i = 0; i < n; i++)
        *p++ = (char)('0' + digits[i]);
    *p = '\0';
}

const char *frac_show(struct frac f)
{
    char *buf = next_buffer();
    char *p = put_frac(buf, f);
    if (f.den != 1) {
        *p++ = ' ';
        put_decimal(p, f);
    }
    return buf;
}

const char *frac_decimal(struct frac f)
{
    char *buf = next_buffer();
    put_decimal(buf, f);
    return buf;
}
