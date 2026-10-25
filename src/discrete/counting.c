#include "discrete.h"
#include "../core/imath.h"
#include "../kernel/calc.h"
#include "../ui/console.h"

#include <string.h>

#define NOTES "CNTR"

static const char too_big[] = "too big for 64 bits";

/* ---- nCr, nPr, n! ---- */

static const struct field choose_fields[] = {
    { "n", F_INT },
    { "k", F_INT | F_OPT },
};

static bool tool_choose(const struct value *v)
{
    int64_t n = v[0].i, k = v[1].i, r;
    if (n < 0 || (v[1].given && k < 0))
        return ui_error("n and k must be 0 or more");
    if (v[1].given) {
        /* The four ways to pick k of n, by order and repeats. */
        if (!i64_choose(n, k, &r))
            return ui_error(too_big);
        ui_answer("C(%lld,%lld) = %lld", n, k, r);
        if (i64_perm(n, k, &r))
            ui_answer("P(%lld,%lld) = %lld", n, k, r);
        if (i64_pow(n, k, &r))
            ui_say("%lld^%lld = %lld (order, repeats)", n, k, r);
        if (n && i64_choose(n + k - 1, k, &r))
            ui_say("C(%lld,%lld) = %lld (repeats, no order)", n + k - 1, k, r);
        ui_check("P = n!/(n-k)!    C = P/k!");
        text_clear();
        for (int64_t i = 0; i < k && i < 12; i++) {
            if (i)
                text_add("*");
            text_int(n - i);
        }
        ui_step("P(%lld,%lld) = %s%s", n, k, text(), k > 12 ? "..." : "");
        ui_step("C(%lld,%lld) = P(%lld,%lld)/%lld!", n, k, n, k, k);
    }
    if (i64_fact(n, &r))
        ui_say("%lld! = %lld", n, r);
    else
        ui_say("%lld! is %s", n, too_big);
    return true;
}

/* ---- multisets ---- */

static const struct field multiset_fields[] = {
    { "chosen", F_INT },
    { "types", F_INT },
    { "at least", F_INT | F_OPT },
    { "at most", F_INT | F_OPT },
};

/* Ways to pick n from m types with repeats: C(n+m-1, m-1). */
static bool stars_bars(int64_t n, int64_t m, int64_t *out)
{
    return n < 0 ? (*out = 0, true) : i64_choose(n + m - 1, m - 1, out);
}

static bool tool_multiset(const struct value *v)
{
    int64_t n = v[0].i, m = v[1].i, lo = v[2].given ? v[2].i : 0, left, r;
    if (n < 0 || m < 1 || lo < 0)
        return ui_error("give chosen >= 0, types >= 1");
    if (!i64_mul(m, lo, &left) || (left = n - left) < 0) {
        ui_answer("ways = 0");
        ui_say("at least %lld each needs %lld*%lld", lo, m, lo);
        return true;
    }
    if (lo)
        ui_step("give %lld to each type first: %lld left", lo, left);

    if (!v[3].given) {
        if (!stars_bars(left, m, &r))
            return ui_error(too_big);
        ui_answer("C(%lld,%lld) = %lld", left + m - 1, m - 1, r);
        ui_check("stars and bars: C(n+m-1, m-1)");
        return true;
    }

    /* At most hi of each: take away the ways where some type gets hi+1 or
     * more, with inclusion-exclusion over how many types do. */
    int64_t cap = v[3].i - lo, total = 0;
    if (cap < 0)
        return ui_error("at most is below at least");
    ui_step_head("j types over: (-1)^j C(m,j) C(..)");
    for (int64_t j = 0; j <= m; j++) {
        int64_t over, ways, pick;
        if (!i64_mul(j, cap + 1, &over) || over > left)
            break;
        if (!i64_choose(m, j, &pick) || !stars_bars(left - over, m, &ways)
            || !i64_mul(pick, ways, &ways))
            return ui_error(too_big);
        ui_step("j = %lld: %c%lld", j, j % 2 ? '-' : '+', ways);
        total += j % 2 ? -ways : ways;
    }
    ui_answer("ways = %lld", total);
    ui_check("all ways minus those over the limit");
    return true;
}

/* ---- multinomial ---- */

static const struct field multinomial_fields[] = {
    { "counts", F_LIST },
};

static bool tool_multinomial(const struct value *v)
{
    int64_t n = 0, r = 1, c;
    text_clear();
    for (uint8_t i = 0; i < v[0].n; i++) {
        if (!frac_is_int(v[0].list[i]) || v[0].list[i].num < 0)
            return ui_error("counts are whole numbers >= 0");
        n += v[0].list[i].num;
        text_int(v[0].list[i].num);
        text_add("!");
    }
    /* n!/(n1! n2! ...) as C(n, n1) * C(n-n1, n2) * ..., which stays small. */
    int64_t left = n;
    for (uint8_t i = 0; i < v[0].n; i++) {
        int64_t k = v[0].list[i].num;
        if (!i64_choose(left, k, &c) || !i64_mul(r, c, &r))
            return ui_error(too_big);
        ui_step("C(%lld,%lld) = %lld", left, k, c);
        left -= k;
    }
    ui_answer("%lld!/(%s) = %lld", n, text(), r);
    ui_check("arrangements of %lld items, alike ones", n);
    return true;
}

/* ---- functions A -> B ---- */

static const struct field maps_fields[] = {
    { "|A|", F_INT },
    { "|B|", F_INT },
};

static bool tool_maps(const struct value *v)
{
    int64_t a = v[0].i, b = v[1].i, r;
    if (a < 0 || b < 0)
        return ui_error("sizes are 0 or more");
    if (!i64_pow(b, a, &r))
        return ui_error(too_big);
    ui_answer("functions: %lld^%lld = %lld", b, a, r);
    if (i64_perm(b, a, &r))
        ui_answer("one-to-one: P(%lld,%lld) = %lld", b, a, r);

    /* Onto: all functions minus those that miss j of B's elements. */
    int64_t onto = 0;
    bool fits = true;
    for (int64_t j = 0; j <= b && fits; j++) {
        int64_t pick, maps;
        fits = i64_choose(b, j, &pick) && i64_pow(b - j, a, &maps) && i64_mul(pick, maps, &maps);
        onto += j % 2 ? -maps : maps;
        if (fits)
            ui_step("miss %lld: %c C(%lld,%lld)*%lld^%lld", j, j % 2 ? '-' : '+', b, j, b - j, a);
    }
    if (fits)
        ui_answer("onto: %lld", onto);
    if (a == b && i64_fact(a, &r))
        ui_say("both (a bijection): %lld! = %lld", a, r);
    ui_check("f: A -> B with |A| = %lld, |B| = %lld", a, b);
    return true;
}

/* ---- pigeonhole ---- */

static const struct field pigeon_fields[] = {
    { "boxes", F_INT },
    { "items", F_INT | F_OPT },
    { "want", F_INT | F_OPT },
};

static bool tool_pigeon(const struct value *v)
{
    int64_t k = v[0].i, r;
    if (k < 1)
        return ui_error("boxes must be 1 or more");
    if (!v[1].given && !v[2].given)
        return ui_error("give items, or want in one box");
    if (v[1].given) {
        int64_t n = v[1].i;
        ui_answer("some box has >= %lld", n <= 0 ? 0 : (n + k - 1) / k);
        ui_check("ceil(items/boxes) = ceil(%lld/%lld)", n, k);
    }
    if (v[2].given) {
        if (v[2].i < 1 || !i64_mul(k, v[2].i - 1, &r))
            return ui_error("want must be 1 or more");
        ui_answer("%lld items force %lld in a box", r + 1, v[2].i);
        ui_check("boxes*(want-1) + 1");
    }
    return true;
}

/* ---- inclusion-exclusion ---- */

static const struct field incexc_fields[] = {
    { "A", F_INT },
    { "B", F_INT },
    { "AnB", F_INT },
    { "C", F_INT | F_OPT },
    { "AnC", F_INT | F_OPT },
    { "BnC", F_INT | F_OPT },
    { "AnBnC", F_INT | F_OPT },
    { "all", F_INT | F_OPT },
};

static bool tool_incexc(const struct value *v)
{
    int64_t a = v[0].i, b = v[1].i, ab = v[2].i;
    if (!v[3].given) {
        int64_t u = a + b - ab;
        ui_answer("|AuB| = %lld + %lld - %lld = %lld", a, b, ab, u);
        ui_say("only A: %lld    only B: %lld", a - ab, b - ab);
        if (v[7].given)
            ui_say("neither: %lld - %lld = %lld", v[7].i, u, v[7].i - u);
        return true;
    }
    if (!v[4].given || !v[5].given || !v[6].given)
        return ui_error("3 sets need AnC, BnC and AnBnC");
    int64_t c = v[3].i, ac = v[4].i, bc = v[5].i, abc = v[6].i;
    int64_t u = a + b + c - ab - ac - bc + abc;
    ui_answer("|AuBuC| = %lld", u);
    ui_check("A+B+C - AnB-AnC-BnC + AnBnC");
    ui_say("only A: %lld  B: %lld  C: %lld", a - ab - ac + abc, b - ab - bc + abc,
           c - ac - bc + abc);
    if (v[7].given)
        ui_say("none: %lld - %lld = %lld", v[7].i, u, v[7].i - u);
    return true;
}

/* ---- lex order ---- */

static const struct field lex_fields[] = {
    { "a", F_LIST },
    { "b", F_LIST | F_OPT },
};

static void text_tuple(const int64_t *t, uint8_t n)
{
    text_clear();
    text_add("(");
    for (uint8_t i = 0; i < n; i++) {
        if (i)
            text_add(",");
        text_int(t[i]);
    }
    text_add(")");
}

static bool tool_lex(const struct value *v)
{
    static int64_t a[LIST_MAX];
    uint8_t n = v[0].n;
    for (uint8_t i = 0; i < n; i++) {
        if (!frac_is_int(v[0].list[i]))
            return ui_error("a: whole numbers only");
        a[i] = v[0].list[i].num;
    }

    if (v[1].given) {
        uint8_t i = 0;
        while (i < n && i < v[1].n && !frac_cmp(v[0].list[i], v[1].list[i]))
            i++;
        if (i == n && i == v[1].n) {
            ui_answer("a = b");
        } else if (i == n || i == v[1].n) {
            ui_answer(i == n ? "a < b: a ends first" : "a > b: b ends first");
        } else {
            int8_t c = frac_cmp(v[0].list[i], v[1].list[i]);
            ui_answer("a %c b at place %d: %s %c %s", c < 0 ? '<' : '>', i + 1,
                      frac_str(v[0].list[i]), c < 0 ? '<' : '>', frac_str(v[1].list[i]));
        }
        ui_check("the first place that differs decides");
    }

    /* The next permutation: from the right, the first a[i] < a[i+1]; swap it
     * with the smallest larger value to its right; reverse what follows. */
    int8_t i = (int8_t)n - 2;
    while (i >= 0 && a[i] >= a[i + 1])
        i--;
    if (i < 0) {
        ui_say("a is the last in lex order");
        return true;
    }
    uint8_t j = n - 1;
    while (a[j] <= a[i])
        j--;
    ui_step("from the right, a%d = %lld < a%d", i + 1, a[i], i + 2);
    ui_step("swap with a%d = %lld, the next larger", j + 1, a[j]);
    int64_t t = a[i];
    a[i] = a[j];
    a[j] = t;
    for (uint8_t lo = (uint8_t)(i + 1), hi = n - 1; lo < hi; lo++, hi--) {
        t = a[lo];
        a[lo] = a[hi];
        a[hi] = t;
    }
    ui_step("then reverse everything after place %d", i + 1);
    text_tuple(a, n);
    ui_say("next: %s", text());
    return true;
}

/* ---- decision tree ---- */

/* A question, or for a leaf, the rule and the tool (in tools[]) for it. */
struct node {
    const char *label;                  /* first, for ui_list: the rule */
    const char *question;
    int8_t yes, no;                     /* nodes; -1 at a leaf */
    int8_t tool;                        /* -1 for none */
};

enum { CHOOSE, MULTISET, MULTINOMIAL, MAPS, PIGEON, INCEXC };

static const struct node tree[] = {
    /*  0 */ { NULL, "A sequence of slots, one pick each?", 11, 1, -1 },
    /*  1 */ { NULL, "One pick from separate groups?", 12, 2, -1 },
    /*  2 */ { NULL, "Pick k of n distinct things?", 3, 7, -1 },
    /*  3 */ { NULL, "Does the order matter?", 4, 6, -1 },
    /*  4 */ { NULL, "All n of them, some alike?", 13, 5, -1 },
    /*  5 */ { NULL, "Can a thing repeat?", 14, 15, -1 },
    /*  6 */ { NULL, "Can a thing repeat?", 16, 17, -1 },
    /*  7 */ { NULL, "Must some sit together?", 18, 8, -1 },
    /*  8 */ { NULL, "Must some not be next to each other?", 19, 9, -1 },
    /*  9 */ { NULL, "At least, at most or not all?", 20, 10, -1 },
    /* 10 */ { NULL, "Overlapping groups joined by or?", 21, 22, -1 },
    /* 11 */ { "Product rule: multiply the counts", NULL, -1, -1, -1 },
    /* 12 */ { "Sum rule: add the counts", NULL, -1, -1, -1 },
    /* 13 */ { "n!/(n1! n2! ...)", NULL, -1, -1, MULTINOMIAL },
    /* 14 */ { "n^k", NULL, -1, -1, CHOOSE },
    /* 15 */ { "P(n,k) = n!/(n-k)!", NULL, -1, -1, CHOOSE },
    /* 16 */ { "Multiset: C(k+n-1, n-1)", NULL, -1, -1, MULTISET },
    /* 17 */ { "C(n,k) = n!/(k!(n-k)!)", NULL, -1, -1, CHOOSE },
    /* 18 */ { "Glue: blocks! * orders inside", NULL, -1, -1, -1 },
    /* 19 */ { "Gaps: k! * P(k+1, r)", NULL, -1, -1, CHOOSE },
    /* 20 */ { "Complement: all - the bad ones", NULL, -1, -1, -1 },
    /* 21 */ { "Inclusion-exclusion", NULL, -1, -1, INCEXC },
    /* 22 */ { "A repeat guaranteed: pigeonhole", NULL, -1, -1, PIGEON },
};

struct choice {
    const char *label;
};

static enum run_end run_tree(void)
{
    static const struct choice yes_no[] = { { "yes" }, { "no" } };
    int8_t path[sizeof tree / sizeof tree[0]];
    uint8_t depth = 0;
    path[0] = 0;

    for (;;) {
        const struct node *node = &tree[path[depth]];
        uint8_t pick = 0;
        enum ui_end end;
        if (node->question) {
            end = ui_list(node->question, yes_no, sizeof *yes_no, 2, "CLEAR goes back.", &pick);
            if (end == UI_PICK) {
                path[++depth] = pick ? node->no : node->yes;
                continue;
            }
        } else {
            const struct tool *tool = node->tool >= 0 ? &count_section.tools[node->tool] : NULL;
            const struct choice open = { tool ? tool->title : "Calculator" };
            end = ui_list(node->label, &open, sizeof open, 1, "1 opens it. CLEAR goes back.", &pick);
            if (end == UI_PICK) {
                if (!tool) {
                    console_clear();
                    calc_run();
                } else if (ui_tool(tool) == RUN_HOME) {
                    return RUN_HOME;
                }
                continue;
            }
        }
        if (end != UI_BACK)
            return RUN_HOME;
        if (!depth)
            return RUN_BACK;
        depth--;
    }
}

static const struct tool tools[] = {
    [CHOOSE] = { "nCr, nPr, n!", FIELDS(choose_fields), tool_choose, NOTES, "PICK FORMULA", NULL },
    [MULTISET] = { "multisets", FIELDS(multiset_fields), tool_multiset, NOTES, "STARS+BARS", NULL },
    [MULTINOMIAL] = { "multinomial", FIELDS(multinomial_fields), tool_multinomial, NOTES, "REPEATS", NULL },
    [MAPS] = { "functions A to B", FIELDS(maps_fields), tool_maps, NOTES, "TWO WORDS", NULL },
    [PIGEON] = { "pigeonhole", FIELDS(pigeon_fields), tool_pigeon, NOTES, "PIGEONHOLE", NULL },
    [INCEXC] = { "inclusion-exclusion", FIELDS(incexc_fields), tool_incexc, NOTES, "INCL-EXCL", NULL },
    { "lex order", FIELDS(lex_fields), tool_lex, NOTES, "LEX ORDER", NULL },
    { "decision tree", NULL, 0, NULL, NOTES, "PICK FORMULA", run_tree },
};

const struct section count_section = { "Counting", TOOLS(tools) };
