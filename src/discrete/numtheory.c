#include "discrete.h"
#include "../core/imath.h"
#include "../kernel/events.h"

#include <string.h>

#define NOTES "NUMR"

static const char digit_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* ---- div and mod ---- */

static const struct field divmod_fields[] = {
    { "x", F_INT },
    { "N", F_INT },
};

static bool tool_divmod(const struct value *v)
{
    int64_t x = v[0].i, n = v[1].i, q, r;
    if (n <= 0)
        return ui_error("N must be positive");
    i64_divmod(x, n, &q, &r);
    ui_answer("q = %lld    r = %lld", q, r);
    ui_say(r ? "%lld does not divide %lld" : "%lld divides %lld", n, x);
    ui_check("check: %lld*%lld + %lld = %lld", q, n, r, x);
    if (r && x < 0)
        ui_check("rounded down, so r is not %lld", x % n);
    return true;
}

/* ---- gcd and lcm ---- */

static const struct field gcd_fields[] = {
    { "a", F_INT },
    { "b", F_INT },
};

static bool tool_gcd(const struct value *v)
{
    int64_t a = i64_abs(v[0].i), b = i64_abs(v[1].i), x = a, y = b, l, ab;
    if (!a && !b)
        return ui_error("gcd(0, 0) has no value");
    text_clear();
    ui_step_head("a mod b = r, then a, b = b, r");
    while (y) {
        int64_t r = x % y;
        ui_step("%lld mod %lld = %lld", x, y, r);
        if (text()[0])
            text_add(" ");
        text_int(r);
        x = y;
        y = r;
    }
    int64_t g = x;
    if (!i64_mul(a / g, b, &l))
        return ui_error("gcd = %lld; lcm is too big", g);
    ui_answer("gcd = %lld    lcm = %lld", g, l);
    if (b)
        ui_say("remainders: %s", text());
    if (i64_mul(a, b, &ab))
        ui_check("check: %lld*%lld = %lld = %lld*%lld", g, l, ab, a, b);
    return true;
}

/* ---- inverse mod N ---- */

static const struct field inverse_fields[] = {
    { "N", F_INT },
    { "x", F_INT },
};

/* x^-1 mod n by the extended Euclid tableau, with its rows as steps. The
 * rows keep r = s*n + t*x, so the row with r = 1 has the inverse as t.
 * It answers through a pointer: returning i64_mod()'s result directly made
 * the toolchain's clang 19 swap the top two thirds of the value. */
static void inverse_steps(int64_t x, int64_t n, int64_t *inverse)
{
    int64_t r0 = n, t0 = 0, r1 = i64_mod(x, n), t1 = 1;
    ui_step_head("%12s%12s%12s", "q", "r", "t");
    ui_step("%12s%12lld%12lld", "", r0, t0);
    ui_step("%12s%12lld%12lld", "", r1, t1);
    while (r1 != 1) {
        int64_t q = r0 / r1, r2 = r0 - q * r1, t2 = t0 - q * t1;
        r0 = r1, t0 = t1, r1 = r2, t1 = t2;
        ui_step("%12lld%12lld%12lld", q, r1, t1);
    }
    ui_step_head("inverse: t mod N = %lld mod %lld", t1, n);
    *inverse = i64_mod(t1, n);
}

/* x*s = q*n + 1, or x*s mod n = 1 when the product is too big. */
static void check_inverse(int64_t x, int64_t s, int64_t n)
{
    int64_t p;
    if (i64_mul(x, s, &p))
        ui_check("check: %lld*%lld = %lld = %lld*%lld + 1", x, s, p, (p - 1) / n, n);
    else
        ui_check("check: %lld*%lld mod %lld = 1", x, s, n);
}

static bool tool_inverse(const struct value *v)
{
    int64_t n = v[0].i, x = v[1].i, g = i64_gcd(x, n);
    if (n < 2)
        return ui_error("N must be 2 or more");
    if (g != 1)
        return ui_error("gcd(%lld, %lld) = %lld, so no inverse", x, n, g);
    int64_t s;
    inverse_steps(x, n, &s);
    ui_answer("%lld^-1 mod %lld = %lld", x, n, s);
    ui_check("all: %lld + %lldk", s, n);
    check_inverse(x, s, n);
    return true;
}

/* ---- power mod N ---- */

static const struct field powmod_fields[] = {
    { "x", F_INT },
    { "y", F_INT },
    { "N", F_INT },
};

static void text_binary(int64_t y)
{
    char bits[64];
    uint8_t n = 0;
    do {
        bits[n++] = (char)('0' + (y & 1));
        y >>= 1;
    } while (y);
    char one[2] = { 0, 0 };
    while (n) {
        one[0] = bits[--n];
        text_add(one);
    }
}

static bool tool_powmod(const struct value *v)
{
    int64_t x = v[0].i, y = v[1].i, n = v[2].i;
    if (n < 1)
        return ui_error("N must be positive");
    if (y < 0)
        return ui_error("y must be 0 or more");

    /* The loop on the formula sheet, reducing mod N at every step. */
    int64_t p = 1 % n, s = i64_mod(x, n);
    ui_step_head("%12s%4s%12s%12s", "r", "bit", "p", "s");
    for (int64_t r = y; r; r >>= 1) {
        if (r & 1)
            p = mulmod(p, s, n);
        s = mulmod(s, s, n);
        ui_step("%12lld%4d%12lld%12lld", r, (int)(r & 1), p, s);
    }
    ui_answer("%lld^%lld mod %lld = %lld", x, y, n, p);

    text_clear();
    text_binary(y);
    ui_say("%lld = %s in base 2", y, text());
    /* The set bits name the factors: x^21 = x^16 * x^4 * x. */
    text_clear();
    for (int64_t bit = (int64_t)1 << 62; bit; bit >>= 1) {
        if (!(y & bit))
            continue;
        if (text()[0])
            text_add(" * ");
        text_int(x);
        if (bit > 1) {
            text_add("^");
            text_int(bit);
        }
    }
    if (y)
        ui_check("%lld^%lld = %s", x, y, text());
    return true;
}

/* ---- primes and factors ---- */

static const struct field factor_fields[] = {
    { "n", F_INT },
};

static bool tool_factor(const struct value *v)
{
    int64_t n = v[0].i;
    if (n < 1 || n > UINT32_MAX)
        return ui_error("n must be 1 to %lu", (unsigned long)UINT32_MAX);
    if (n == 1) {
        ui_answer("1 is not prime");
        ui_say("divisors: 1");
        return true;
    }

    uint32_t m = (uint32_t)n, smallest = 0;
    int64_t count = 1;
    text_clear();
    static char divisors[64];           /* "(3+1)(2+1)(1+1)" */
    divisors[0] = '\0';
    for (uint32_t d = 2; d <= m / d; d += d == 2 ? 1 : 2) {
        if (kernel_poll())
            return ui_error("stopped");
        if (m % d)
            continue;
        uint8_t e = 0;
        while (m % d == 0) {
            m /= d;
            e++;
        }
        if (!smallest)
            smallest = d;
        if (text()[0])
            text_add(" * ");
        text_int(d);
        if (e > 1) {
            text_add("^");
            text_int(e);
        }
        size_t at = strlen(divisors);
        if (at + 8 < sizeof divisors) {
            divisors[at++] = '(';
            i64_to_str(e, &divisors[at]);
            strcat(divisors, "+1)");
        }
        count *= e + 1;
        ui_step("%lu divides it %d times", (unsigned long)d, e);
    }
    if (m > 1) {
        if (!smallest) {
            ui_answer("%lld is prime", n);
            ui_say("no factor up to sqrt(n) = %lld", i64_isqrt(n));
            ui_say("divisors: 2");
            return true;
        }
        if (text()[0])
            text_add(" * ");
        text_int(m);
        strcat(divisors, "(1+1)");
        count *= 2;
        ui_step("%lu is left, a prime", (unsigned long)m);
    }
    ui_answer("%lld = %s", n, text());
    ui_say("not prime: %lu * %lld", (unsigned long)smallest, n / smallest);
    ui_say("divisors: %s = %lld", divisors, count);
    return true;
}

/* ---- bases ---- */

static const struct field base_fields[] = {
    { "n", F_DIGITS },
    { "base", F_INT | F_OPT },
    { "to", F_INT | F_OPT },
};

/* Digits of v in base b, by repeated division, with each division as a step
 * when steps is set. */
static void text_base(int64_t v, int64_t b, bool steps)
{
    char digits[64];
    uint8_t n = 0;
    bool neg = v < 0;
    if (neg)
        v = -v;
    do {
        if (steps)
            ui_step("%lld div %lld = %lld r %lld", v, b, v / b, v % b);
        digits[n++] = digit_chars[v % b];
        v /= b;
    } while (v);
    if (steps)
        ui_step_head("read the remainders bottom up");
    char one[2] = { 0, 0 };
    if (neg)
        text_add("-");
    while (n) {
        one[0] = digits[--n];
        text_add(one);
    }
}

static bool tool_base(const struct value *v)
{
    const char *s = v[0].text;
    int64_t from = v[1].given ? v[1].i : 10, value = 0;
    if (from < 2 || from > 36 || (v[2].given && (v[2].i < 2 || v[2].i > 36)))
        return ui_error("bases go from 2 to 36");

    bool neg = *s == '-';
    size_t len = strlen(s + neg);
    if (!len)
        return ui_error("n has no digits");
    for (size_t i = 0; i < len; i++) {
        char c = s[neg + i];
        int64_t d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : 99;
        if (d == 99)
            return ui_error("%c is not a digit", c);
        if (d >= from)
            return ui_error("%c is too big for base %lld", digit_chars[d], from);
        if (!i64_mul(value, from, &value) || !i64_add(value, d, &value))
            return ui_error("too big");
        int64_t place, term;
        if (from != 10 && i64_pow(from, (int64_t)(len - 1 - i), &place) && i64_mul(d, place, &term))
            ui_step("%c * %lld^%u = %lld", digit_chars[d], from, (unsigned)(len - 1 - i), term);
    }
    if (neg)
        value = -value;
    if (from != 10) {
        ui_step_head("add them: %lld", value);
        ui_answer("%s in base %lld = %lld", s, from, value);
    } else {
        ui_answer("%lld in base 10", value);
    }

    static const uint8_t shown[] = { 2, 8, 16 };
    for (uint8_t i = 0; i < sizeof shown; i++) {
        text_clear();
        text_base(value, shown[i], false);
        ui_say("base %d: %s", shown[i], text());
    }
    if (v[2].given) {
        text_clear();
        text_base(value, v[2].i, true);
        ui_answer("base %lld: %s", v[2].i, text());
    } else if (from == 10) {
        text_clear();
        text_base(value, 2, true);
    }
    int64_t bits = 0;
    for (int64_t m = i64_abs(value); m; m >>= 1)
        bits++;
    ui_check("bits needed: %lld", bits ? bits : 1);
    return true;
}

/* ---- RSA ---- */

static const struct field rsa_fields[] = {
    { "p", F_INT },
    { "q", F_INT },
    { "e", F_INT },
    { "m", F_INT | F_OPT },
    { "c", F_INT | F_OPT },
};

static bool tool_rsa(const struct value *v)
{
    int64_t p = v[0].i, q = v[1].i, e = v[2].i;
    if (p > INT32_MAX || q > INT32_MAX)
        return ui_error("p and q must be under 2^31");
    if (!is_prime(p) || !is_prime(q))
        return ui_error(is_prime(p) ? "q is not prime" : "p is not prime");
    if (p == q)
        return ui_error("p and q must differ");
    int64_t n = p * q, phi = (p - 1) * (q - 1), g = i64_gcd(e, phi);
    if (e <= 1 || e >= phi)
        return ui_error("e must be between 1 and phi = %lld", phi);
    if (g != 1)
        return ui_error("gcd(e, phi) = %lld: pick another e", g);

    ui_step_head("d = e^-1 mod phi:");
    int64_t d;
    inverse_steps(e, phi, &d);
    ui_say("N = %lld    phi = %lld", n, phi);
    ui_answer("d = %lld", d);
    check_inverse(e, d, phi);
    if (v[3].given) {
        if (v[3].i < 0 || v[3].i >= n)
            return ui_error("m must be 0 to N-1");
        ui_answer("c = m^e mod N = %lld", powmod(v[3].i, e, n));
    }
    if (v[4].given) {
        if (v[4].i < 0 || v[4].i >= n)
            return ui_error("c must be 0 to N-1");
        ui_answer("m = c^d mod N = %lld", powmod(v[4].i, d, n));
    }
    ui_check("public: N, e    private: d, p, q, phi");
    return true;
}

/* ---- ciphers and A=1 codes ---- */

static const struct field cipher_fields[] = {
    { "text", F_LETTERS },
    { "a", F_INT | F_OPT },
    { "b", F_INT | F_OPT },
};

static bool tool_cipher(const struct value *v)
{
    const char *s = v[0].text;
    int64_t a = v[1].given ? i64_mod(v[1].i, 26) : 1, b = v[2].given ? i64_mod(v[2].i, 26) : 0;
    int64_t ainv = 0;
    bool decodes = invmod(a, 26, &ainv);
    static char enc[FIELD_MAX + 1], dec[FIELD_MAX + 1];
    size_t n = strlen(s);

    ui_step_head("letter: A=0 value, then E(x) and D(x)");
    for (size_t i = 0; i <= n; i++) {
        char c = s[i];
        if (c < 'a' || c > 'z') {
            enc[i] = dec[i] = c;
            continue;
        }
        int64_t x = c - 'a', y = (a * x + b) % 26, z = decodes ? i64_mod(ainv * (x - b), 26) : 0;
        enc[i] = (char)('A' + y);
        dec[i] = (char)('A' + z);
        ui_step("%c: %2lld  E = %c  D = %c", c - 'a' + 'A', x, enc[i], decodes ? dec[i] : '-');
    }
    ui_answer("enc: %s", enc);
    if (decodes)
        ui_answer("dec: %s", dec);
    else
        ui_say("dec: none, gcd(a, 26) = %lld", i64_gcd(a, 26));
    ui_check("E(x) = %lldx + %lld mod 26, letters A=0", a, b);

    /* A=1 codes run the numbers together, with no separator. */
    text_clear();
    for (size_t i = 0; i < n; i++) {
        if (s[i] < 'a' || s[i] > 'z')
            continue;
        if (text()[0])
            text_add(" ");
        text_int(s[i] - 'a' + 1);
    }
    ui_say("A=1: %s", text());
    text_clear();
    for (size_t i = 0; i < n; i++)
        if (s[i] >= 'a' && s[i] <= 'z')
            text_int(s[i] - 'a' + 1);
    ui_say("joined: %s", text());
    return true;
}

/* ---- LCG ---- */

static const struct field lcg_fields[] = {
    { "a", F_INT },
    { "c", F_INT },
    { "m", F_INT },
    { "x0", F_INT },
};

#define LCG_TERMS 20

static bool tool_lcg(const struct value *v)
{
    int64_t m = v[2].i;
    if (m < 1)
        return ui_error("m must be positive");
    int64_t a = i64_mod(v[0].i, m), c = i64_mod(v[1].i, m), x = i64_mod(v[3].i, m);
    static int64_t terms[LCG_TERMS + 1];
    int8_t repeat = -1;                 /* the earlier term the last one equals */

    text_clear();
    text_int(x);
    terms[0] = x;
    uint8_t k;
    for (k = 1; k <= LCG_TERMS && repeat < 0; k++) {
        int64_t next = (mulmod(a, x, m) + c) % m;
        ui_step("x%d = (%lld*%lld + %lld) mod %lld = %lld", k, a, x, c, m, next);
        x = terms[k] = next;
        text_add(" ");
        text_int(x);
        for (uint8_t j = 0; j < k; j++)
            if (terms[j] == x)
                repeat = (int8_t)j;
    }
    ui_answer("x1 = %lld", terms[1]);
    ui_say("x0..: %s", text());
    if (repeat >= 0)
        ui_say("x%d = x%d, so it repeats every %d", k - 1, repeat, k - 1 - repeat);
    else
        ui_say("no repeat in the first %d terms", LCG_TERMS);
    ui_check("x' = (a*x + c) mod m");
    return true;
}

static const struct tool tools[] = {
    { "div and mod", FIELDS(divmod_fields), tool_divmod, NOTES, "DIV / MOD", NULL },
    { "gcd and lcm", FIELDS(gcd_fields), tool_gcd, NOTES, "EUCLID GCD", NULL },
    { "inverse mod N", FIELDS(inverse_fields), tool_inverse, NOTES, "INVERSE", NULL },
    { "power mod N", FIELDS(powmod_fields), tool_powmod, NOTES, "FAST EXP", NULL },
    { "primes and factors", FIELDS(factor_fields), tool_factor, NOTES, "PRIMES", NULL },
    { "bases", FIELDS(base_fields), tool_base, NOTES, "BASES", NULL },
    { "RSA", FIELDS(rsa_fields), tool_rsa, NOTES, "RSA", NULL },
    { "ciphers and A=1 codes", FIELDS(cipher_fields), tool_cipher, NOTES, "CIPHERS", NULL },
    { "LCG", FIELDS(lcg_fields), tool_lcg, NOTES, "LCG", NULL },
};

const struct section nt_section = { "Number theory", TOOLS(tools) };
