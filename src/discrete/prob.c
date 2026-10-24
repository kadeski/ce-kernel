#include "discrete.h"
#include "../core/imath.h"
#include "../kernel/events.h"

#define NOTES "PROBR"

static const char too_big[] = "too big for exact fractions";

static const struct frac one = { 1, 1 };

/* Each given value of v is a probability, or ui_error() names the field. */
static bool probabilities(const struct field *f, const struct value *v, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
        if (v[i].given && (v[i].f.num < 0 || frac_cmp(v[i].f, one) > 0))
            return ui_error("%s must be 0 to 1", f[i].label);
    return true;
}

/* ---- Bayes ---- */

static const struct field bayes_fields[] = {
    { "P(X|F)", F_NUM },
    { "P(F)", F_NUM },
    { "P(X|~F)", F_NUM },
};

static bool tool_bayes(const struct value *v)
{
    struct frac not_f, top, other, px, fx, t;
    if (!probabilities(bayes_fields, v, 3))
        return false;
    if (!frac_sub(one, v[1].f, &not_f) || !frac_mul(v[0].f, v[1].f, &top)
        || !frac_mul(v[2].f, not_f, &other) || !frac_add(top, other, &px))
        return ui_error(too_big);
    if (!px.num)
        return ui_error("P(X) is 0");
    if (!frac_div(top, px, &fx) || !frac_sub(one, fx, &t))
        return ui_error(too_big);
    ui_step("P(X|F)P(F) = %s", frac_str(top));
    ui_step("P(X|~F)P(~F) = %s", frac_str(other));
    ui_step("P(X), their sum = %s", frac_str(px));
    ui_step("P(F|X) = P(X|F)P(F) / P(X)");
    ui_answer("P(F|X) = %s", frac_show(fx));
    ui_say("P(~F|X) = %s", frac_show(t));
    ui_say("P(X) = %s", frac_show(px));
    if (frac_mul(fx, px, &t))
        ui_check("check: %s*%s = %s", frac_str(fx), frac_str(px), frac_str(t));
    return true;
}

/* ---- and, or, given ---- */

static const struct field rules_fields[] = {
    { "P(A)", F_NUM },
    { "P(B)", F_NUM },
    { "P(AnB)", F_NUM | F_OPT },
};

static bool tool_rules(const struct value *v)
{
    struct frac a = v[0].f, b = v[1].f, ab, both, u, t;
    if (!probabilities(rules_fields, v, 3))
        return false;
    if (!frac_mul(a, b, &both))
        return ui_error(too_big);
    if (v[2].given) {
        ab = v[2].f;
        if (frac_cmp(ab, a) > 0 || frac_cmp(ab, b) > 0)
            return ui_error("P(AnB) is more than P(A) or P(B)");
        ui_say(frac_cmp(ab, both) ? "dependent: P(A)P(B) = %s" : "independent: P(A)P(B) = %s",
               frac_str(both));
    } else {
        ab = both;
        ui_say("independent: P(AnB) = %s", frac_show(ab));
    }
    if (!frac_add(a, b, &u) || !frac_sub(u, ab, &u))
        return ui_error(too_big);
    if (frac_cmp(u, one) > 0)
        return ui_error("P(AuB) would be over 1");
    ui_answer("P(AuB) = %s", frac_show(u));
    if (b.num && frac_div(ab, b, &t))
        ui_answer("P(A|B) = %s", frac_show(t));
    if (a.num && frac_div(ab, a, &t))
        ui_answer("P(B|A) = %s", frac_show(t));
    if (frac_sub(one, a, &t))
        ui_say("P(not A) = %s", frac_show(t));
    if (frac_sub(one, u, &t))
        ui_say("P(neither) = %s", frac_show(t));
    ui_check("P(AuB) = P(A) + P(B) - P(AnB)");
    return true;
}

/* ---- binomial ---- */

static const struct field binomial_fields[] = {
    { "n", F_INT },
    { "p", F_NUM },
    { "k", F_INT },
};

static bool tool_binomial(const struct value *v)
{
    int64_t n = v[0].i, k = v[2].i, c;
    struct frac p = v[1].f, q, term, a, b, exact = { 0, 1 }, most = { 0, 1 }, least = { 0, 1 };
    if (!probabilities(&binomial_fields[1], &v[1], 1))
        return false;
    if (n < 0 || n > 100 || k < 0 || k > n)
        return ui_error("need 0 <= k <= n <= 100");
    if (!frac_sub(one, p, &q))
        return ui_error(too_big);

    for (int64_t j = 0; j <= n; j++) {
        if (!i64_choose(n, j, &c) || !frac_pow(p, j, &a) || !frac_pow(q, n - j, &b)
            || !frac_mul(a, b, &term) || !frac_mul(term, frac_int(c), &term))
            return ui_error(too_big);
        ui_step("P(X=%lld) = %s", j, frac_show(term));
        if (j == k)
            exact = term;
        if ((j <= k && !frac_add(most, term, &most)) || (j >= k && !frac_add(least, term, &least)))
            return ui_error(too_big);
    }
    ui_answer("P(X = %lld) = %s", k, frac_show(exact));
    ui_say("P(X >= %lld) = %s", k, frac_show(least));
    ui_say("P(X <= %lld) = %s", k, frac_show(most));
    if (frac_mul(p, frac_int(n), &a))
        ui_say("E[X] = np = %s", frac_show(a));
    ui_check("C(n,k) p^k (1-p)^(n-k)");
    return true;
}

/* ---- at least once ---- */

static const struct field atleast_fields[] = {
    { "q", F_NUM },
    { "n", F_INT | F_OPT },
    { "target", F_NUM | F_OPT },
};

#define ATLEAST_MAX 100000

static bool tool_atleast(const struct value *v)
{
    struct frac q = v[0].f, miss, none, p;
    if (q.num <= 0 || frac_cmp(q, one) > 0)
        return ui_error("q must be over 0, up to 1");
    if (!v[1].given && !v[2].given)
        return ui_error("give n, or a target");
    if (!frac_sub(one, q, &miss))
        return ui_error(too_big);

    if (v[1].given) {
        if (v[1].i < 0)
            return ui_error("n must be 0 or more");
        if (!frac_pow(miss, v[1].i, &none) || !frac_sub(one, none, &p))
            return ui_error(too_big);
        ui_answer("P = %s", frac_show(p));
        ui_say("none: (1-q)^n = %s", frac_str(none));
        ui_check("at least once = 1 - (1-q)^n");
    }
    if (v[2].given) {
        struct frac target = v[2].f;
        if (target.num <= 0 || frac_cmp(target, one) >= 0)
            return ui_error("target must be over 0, under 1");
        /* The smallest n with 1 - (1-q)^n >= target. Once the exact
         * fractions get too big, (1-q)^n is rounded to 40 bits. */
        bool rounded = false;
        int64_t n;
        none = one;
        for (n = 1;; n++) {
            if (n > ATLEAST_MAX)
                return ui_error("not reached by n = %d", ATLEAST_MAX);
            if (kernel_poll())
                return ui_error("stopped");
            struct frac next;
            if (!frac_mul(none, miss, &next)) {
                if (!frac_round(none, 40, &none) || !frac_mul(none, miss, &next))
                    return ui_error(too_big);
                rounded = true;
            }
            none = next;
            if (!frac_sub(one, none, &p))
                return ui_error(too_big);
            ui_step("n = %lld: %s", n, rounded ? frac_decimal(p) : frac_show(p));
            if (frac_cmp(p, target) >= 0)
                break;
        }
        if (rounded)
            ui_answer("n = %lld: P %s", n, frac_decimal(p));
        else
            ui_answer("n = %lld: %s", n, frac_show(p));
        ui_check("smallest n with 1 - (1-q)^n >= target");
    }
    return true;
}

/* ---- expected value ---- */

static const struct field expect_fields[] = {
    { "x", F_LIST },
    { "p", F_LIST | F_OPT },
};

static bool tool_expect(const struct value *v)
{
    uint8_t n = v[0].n;
    struct frac sum = { 0, 1 }, total = { 0, 1 }, p, term;
    if (v[1].given && v[1].n != n)
        return ui_error("x and p need as many values");
    if (!frac_make(1, n, &p))
        return false;
    for (uint8_t i = 0; i < n; i++) {
        if (v[1].given)
            p = v[1].list[i];
        if (!frac_mul(v[0].list[i], p, &term) || !frac_add(sum, term, &sum)
            || !frac_add(total, p, &total))
            return ui_error(too_big);
        ui_step("%s * %s = %s", frac_str(v[0].list[i]), frac_str(p), frac_str(term));
    }
    ui_answer("E[X] = %s", frac_show(sum));
    if (frac_cmp(total, one))
        return ui_error("the p add up to %s, not 1", frac_str(total));
    if (v[1].given)
        ui_check("sum of x*p; the p add up to 1");
    else
        ui_check("each x has p = 1/%d", n);
    return true;
}

static const struct tool tools[] = {
    { "Bayes", FIELDS(bayes_fields), tool_bayes, NOTES, "BAYES", NULL },
    { "and, or, given", FIELDS(rules_fields), tool_rules, NOTES, "OR / NOT", NULL },
    { "binomial", FIELDS(binomial_fields), tool_binomial, NOTES, "RANDOM VARS", NULL },
    { "at least once", FIELDS(atleast_fields), tool_atleast, NOTES, "OR / NOT", NULL },
    { "expected value", FIELDS(expect_fields), tool_expect, NOTES, "EXPECTATION", NULL },
};

const struct section prob_section = { "Probability", TOOLS(tools) };
