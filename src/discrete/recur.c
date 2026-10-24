#include "discrete.h"
#include "../core/expr.h"
#include "../core/imath.h"

#include <string.h>

#define NOTES "INDR"

static const char too_big[] = "too big for exact fractions";

/* ---- iterate ---- */

static const struct field iterate_fields[] = {
    { "f(n)", F_DIGITS },               /* in a = f(n-1), b = f(n-2) and n */
    { "n0", F_INT | F_OPT },
    { "f(n0)", F_NUM },
    { "f(n0+1)", F_NUM | F_OPT },
    { "last n", F_INT | F_OPT },
};

#define ITERATE_MAX 60

static bool tool_iterate(const struct value *v)
{
    const char *e = v[0].text;
    int64_t n0 = v[1].given ? v[1].i : 0, last = v[4].given ? v[4].i : n0 + 10, n = n0;
    if (strchr(e, 'b') && !v[3].given)
        return ui_error("b is f(n-2): give f(n0+1)");
    if (last < n0 || last - n0 > ITERATE_MAX)
        return ui_error("last n must be n0 to n0+%d", ITERATE_MAX);

    struct frac vars[3], f = v[2].f, before = frac_int(0);
    text_clear();
    for (;;) {
        ui_step("f(%lld) = %s", n, frac_show(f));
        if (n > n0)
            text_add(" ");
        text_add(frac_str(f));
        if (n == last)
            break;
        n++;
        vars[0] = f;
        vars[1] = before;
        vars[2] = frac_int(n);
        before = f;
        if (n == n0 + 1 && v[3].given)
            f = v[3].f;
        else if (!expr_eval(e, vars, &f))
            return ui_error("f(%lld): %s", n, expr_error());
    }
    ui_answer("f(%lld) = %s", last, frac_show(f));
    ui_say("from f(%lld): %s", n0, text());
    ui_check("a is f(n-1), b is f(n-2)");
    return true;
}

/* ---- characteristic equation ---- */

static const struct field chareq_fields[] = {
    { "c1", F_NUM },
    { "c2", F_NUM },
    { "f0", F_NUM },
    { "f1", F_NUM },
};

/* sqrt(d) when it is a fraction. */
static bool frac_sqrt(struct frac d, struct frac *out)
{
    if (d.num < 0)
        return false;
    int64_t sn = i64_isqrt(d.num), sd = i64_isqrt(d.den);
    return sn * sn == d.num && sd * sd == d.den && frac_make(sn, sd, out);
}

/* Appends m*r^n, or m*n*r^n with times_n, to the text: "1/22*12^n",
 * " - (-10)^n", or just m when r is 1. Nothing when m is 0. */
static void text_term(struct frac m, struct frac r, bool times_n)
{
    if (!m.num)
        return;
    if (text()[0])
        text_add(m.num < 0 ? " - " : " + ");
    else if (m.num < 0)
        text_add("-");
    m.num = i64_abs(m.num);
    bool power = r.num != 1 || r.den != 1;
    bool coefficient = m.num != 1 || m.den != 1 || (!power && !times_n);
    if (coefficient)
        text_add(frac_str(m));
    if (times_n) {
        if (coefficient)
            text_add("*");
        text_add("n");
    }
    if (!power)
        return;
    if (coefficient || times_n)
        text_add("*");
    bool paren = r.num < 0 || r.den != 1;
    if (paren)
        text_add("(");
    text_add(frac_str(r));
    text_add(paren ? ")^n" : "^n");
}

static bool tool_chareq(const struct value *v)
{
    struct frac c1 = v[0].f, c2 = v[1].f, f0 = v[2].f, f1 = v[3].f, d, s, r1, r2, m1, m2, t;
    if (!c2.num)
        return ui_error("c2 is 0: use iterate");
    /* x^2 = c1 x + c2 has roots (c1 +- sqrt(D))/2 with D = c1^2 + 4 c2. */
    if (!frac_mul(c1, c1, &d) || !frac_mul(frac_int(4), c2, &t) || !frac_add(d, t, &d))
        return ui_error(too_big);
    ui_check("x^2 = c1*x + c2, D = c1^2 + 4*c2 = %s", frac_str(d));
    if (d.num < 0)
        return ui_error("D < 0: complex roots");
    if (!frac_sqrt(d, &s)) {
        ui_say("roots: (%s +- sqrt(%s))/2", frac_str(c1), frac_str(d));
        return ui_error("not fractions: m1, m2 by hand");
    }
    struct frac half = { 1, 2 };
    if (!frac_add(c1, s, &r1) || !frac_mul(r1, half, &r1)
        || !frac_sub(c1, s, &r2) || !frac_mul(r2, half, &r2))
        return ui_error(too_big);

    text_clear();
    if (!d.num) {
        /* One root r: f(n) = (m1 + m2 n) r^n, so m1 = f0 and m2 = f1/r - f0. */
        m1 = f0;
        if (!frac_div(f1, r1, &m2) || !frac_sub(m2, f0, &m2))
            return ui_error(too_big);
        ui_say("one root: %s", frac_str(r1));
        text_term(m1, r1, false);
        text_term(m2, r1, true);
    } else {
        /* f0 = m1 + m2 and f1 = m1 r1 + m2 r2. */
        if (!frac_mul(r1, f0, &t) || !frac_sub(f1, t, &t) || !frac_sub(r2, r1, &m2)
            || !frac_div(t, m2, &m2) || !frac_sub(f0, m2, &m1))
            return ui_error(too_big);
        ui_say("roots: %s and %s", frac_str(r1), frac_str(r2));
        text_term(m1, r1, false);
        text_term(m2, r2, false);
    }
    ui_answer("f(n) = %s", text()[0] ? text() : "0");
    ui_step("m1 = %s", frac_str(m1));
    ui_step("m2 = %s", frac_str(m2));

    /* f(2) from the recurrence. */
    struct frac f2;
    if (frac_mul(c1, f1, &f2) && frac_mul(c2, f0, &t) && frac_add(f2, t, &f2))
        ui_check("check: f(2) = c1*f1 + c2*f0 = %s", frac_str(f2));
    return true;
}

/* ---- sums ---- */

static const struct field sums_fields[] = {
    { "n", F_INT | F_OPT },
    { "a", F_NUM | F_OPT },
    { "d", F_NUM | F_OPT },
    { "r", F_NUM | F_OPT },
};

static bool tool_sums(const struct value *v)
{
    int64_t n = v[0].i, s1, s2, s3;
    struct frac a = v[1].f, t, u;
    bool said = false;
    if (v[0].given && n < 0)
        return ui_error("n must be 0 or more");

    if (v[0].given) {
        if (!i64_mul(n, n + 1, &s1) || !i64_mul(s1 / 2, 2 * n + 1, &s2) || !i64_mul(s1 / 2, s1 / 2, &s3))
            return ui_error("n is too big");
        ui_answer("1+2+...+n = %lld", s1 / 2);
        ui_say("1^2+...+n^2 = %lld", s2 / 3);
        ui_say("1^3+...+n^3 = %lld", s3);
        said = true;
    }
    if (v[1].given && v[2].given) {
        /* a + (a+d) + ... for n terms: an + d(n-1)n/2. */
        if (!v[0].given)
            return ui_error("arithmetic sums need n");
        struct frac d = v[2].f;
        if (!frac_mul(a, frac_int(n), &t) || !frac_mul(d, frac_int(n * (n - 1) / 2), &u)
            || !frac_add(t, u, &t) || !frac_mul(d, frac_int(n - 1), &u) || !frac_add(a, u, &u))
            return ui_error(too_big);
        ui_answer("a + (a+d) + ...: %s", frac_show(t));
        ui_say("last term a+(n-1)d = %s", frac_str(u));
        said = true;
    }
    if (v[1].given && v[3].given) {
        struct frac r = v[3].f, one = frac_int(1);
        if (v[0].given) {
            /* a + ar + ... for n terms: a(r^n - 1)/(r - 1), or an if r is 1. */
            bool ok = !frac_cmp(r, one) ? frac_mul(a, frac_int(n), &t)
                : frac_pow(r, n, &t) && frac_sub(t, one, &t) && frac_mul(a, t, &t)
                  && frac_sub(r, one, &u) && frac_div(t, u, &t);
            if (!ok)
                return ui_error(too_big);
            ui_answer("a + ar + ...: %s", frac_show(t));
        }
        struct frac mag = { i64_abs(r.num), r.den };
        if (frac_cmp(mag, one) < 0) {
            if (!frac_sub(one, r, &u) || !frac_div(a, u, &t))
                return ui_error(too_big);
            ui_answer("forever: a/(1-r) = %s", frac_show(t));
        } else {
            ui_say("forever: diverges, |r| >= 1");
        }
        said = true;
    }
    if (!said)
        return ui_error("give n, or a with d or r");
    return true;
}

static const struct tool tools[] = {
    { "iterate", FIELDS(iterate_fields), tool_iterate, NOTES, "RECURRENCES", NULL },
    { "characteristic equation", FIELDS(chareq_fields), tool_chareq, NOTES, "CHAR EQ", NULL },
    { "sums", FIELDS(sums_fields), tool_sums, NOTES, "SUMS", NULL },
};

const struct section recur_section = { "Recurrences and sums", TOOLS(tools) };
