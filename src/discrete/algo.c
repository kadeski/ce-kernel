#include "discrete.h"
#include "../core/imath.h"

#define NOTES "BIGOR"

/* ---- bits and binary search ---- */

static const struct field bits_fields[] = {
    { "n", F_INT, "whole number, 1 or more" },
};

static bool tool_bits(const struct value *v)
{
    int64_t n = v[0].i, bits = 0, up = 0;
    if (n < 1)
        return ui_error("n must be 1 or more");
    while (bits < 63 && n >> bits)
        bits++;
    while (up < 62 && ((int64_t)1 << up) < n)
        up++;
    ui_answer("bits: floor(log2 n) + 1 = %lld", bits);
    ui_say("2^%lld = %lld <= n < 2^%lld", bits - 1, (int64_t)1 << (bits - 1), bits);
    ui_say("binary search, worst: %lld compares", bits);
    ui_say("N := N/2 while N >= 1: %lld passes", bits);
    ui_say("i := 2i while i < n: %lld passes", up);
    ui_check("all of these are Theta(log n)");
    return true;
}

/* ---- sort step counts ---- */

static const struct field sorts_fields[] = {
    { "n", F_INT, "how many items, 1 to 3*10^9" },
};

static bool tool_sorts(const struct value *v)
{
    int64_t n = v[0].i, pairs, tri;
    if (n < 1 || n > 3000000000)
        return ui_error("n must be 1 to 3*10^9");
    pairs = n * (n - 1) / 2;
    tri = n * (n + 1) / 2;
    ui_answer("selection: %lld compares, always", pairs);
    ui_say("  and at most %lld swaps", n - 1);
    ui_answer("insertion: %lld best, %lld worst", n - 1, pairs);
    ui_say("bubble: %lld worst", pairs);
    ui_say("merge two lists of %lld: %lld", n, n - 1);
    ui_say("for i to n, j to i: %lld passes", tri);
    ui_check("n(n-1)/2 and n(n+1)/2: Theta(n^2)");
    return true;
}

/* ---- master theorem ---- */

static const struct field master_fields[] = {
    { "a", F_INT, "T(n) = a*T(n/b) + n^k, a >= 1" },
    { "b", F_INT, "T(n) = a*T(n/b) + n^k, b >= 2" },
    { "k", F_INT, "T(n) = a*T(n/b) + n^k, k >= 0" },
};

/* log2(x) in 16.16 fixed point for 1 <= x < 2^30, by repeated squaring. */
static int64_t log2_fixed(int64_t x)
{
    int64_t whole = 0;
    while (x >> (whole + 1))
        whole++;
    uint64_t y = ((uint64_t)x << 30) >> whole;     /* x / 2^whole, 2.30 fixed */
    int64_t r = whole << 16;
    for (int8_t bit = 15; bit >= 0; bit--) {
        y = (y * y) >> 30;
        if (y >= (uint64_t)2 << 30) {
            y >>= 1;
            r |= (int64_t)1 << bit;
        }
    }
    return r;
}

static bool tool_master(const struct value *v)
{
    int64_t a = v[0].i, b = v[1].i, k = v[2].i, bk;
    if (a < 1 || b < 2 || k < 0 || a >= 1 << 20 || b >= 1 << 20)
        return ui_error("need 1 <= a, 2 <= b, 0 <= k");
    ui_say("T(n) = %lldT(n/%lld) + n^%lld", a, b, k);
    if (!i64_pow(b, k, &bk) || a < bk) {
        ui_answer("Theta(n^%lld)", k);
        ui_check("a = %lld < b^k: the top level wins", a);
    } else if (a == bk) {
        if (k)
            ui_answer("Theta(n^%lld log n)", k);
        else
            ui_answer("Theta(log n)");
        ui_check("a = b^k = %lld: every level the same", a);
    } else {
        int64_t p = 1, j = 0;
        while (p < a) {
            p *= b;
            j++;
        }
        if (p == a) {
            ui_answer("Theta(n^%lld)", j);
        } else {
            int64_t la = log2_fixed(a), lb = log2_fixed(b), e = (la * 200 + lb) / (2 * lb);
            ui_answer("Theta(n^log%lld(%lld)) ~ n^%lld.%02lld", b, a, e / 100, e % 100);
        }
        ui_check("a = %lld > b^k = %lld: the leaves win", a, bk);
    }
    return true;
}

/* ---- state machines ---- */

/* States are the digits 0-9 (A = 0, B = 1, ...). A set of them is a bit mask,
 * so one table serves a DFA and an NFA. */
static const struct field fsm_fields[] = {
    { "on 0", F_DIGITS, "next on 0 for each state, like 1,2,-" },
    { "on 1", F_DIGITS, "next on 1 for each state, like 1,2,-" },
    { "start", F_INT | F_OPT, "optional: the start state (else 0)" },
    { "accept", F_DIGITS | F_OPT, "optional: accept states, like 2 or 05" },
    { "input", F_DIGITS, "0s and 1s; commas between words" },
};

#define FSM_STATES 10

/* Reads "1,2,-,03" into next[], one set per state; returns the count, or 0
 * after ui_error(). */
static uint8_t fsm_table(const char *s, const char *name, uint16_t *next)
{
    uint8_t n = 0;
    for (;;) {
        if (n == FSM_STATES)
            return ui_error("%s: at most %d states", name, FSM_STATES);
        next[n] = 0;
        for (; *s && *s != ','; s++) {
            if (*s >= '0' && *s <= '9')
                next[n] |= 1u << (*s - '0');
            else if (*s != '-')
                return ui_error("%s: digits, - and commas", name);
        }
        n++;
        if (!*s++)
            return n;
    }
}

static const char *fsm_set(uint16_t set)
{
    char one[5] = "0(A)";
    uint8_t count = 0;
    text_clear();
    for (uint8_t q = 0; q < FSM_STATES; q++) {
        if (!(set & (1u << q)))
            continue;
        if (count++)
            text_add(",");
        one[0] = (char)('0' + q);
        one[2] = (char)('A' + q);
        text_add(one);
    }
    if (!count)
        text_add("none: stuck");
    return text();
}

static bool tool_fsm(const struct value *v)
{
    static uint16_t on[2][FSM_STATES];
    uint8_t n = fsm_table(v[0].text, "on 0", on[0]);
    if (!n)
        return false;
    uint8_t n1 = fsm_table(v[1].text, "on 1", on[1]);
    if (!n1)
        return false;
    if (n1 != n)
        return ui_error("on 0 and on 1 need one entry per state");
    int64_t start = v[2].given ? v[2].i : 0;
    if (start < 0 || start >= n)
        return ui_error("start must be 0 to %d", n - 1);
    uint16_t accept = 0;
    for (const char *s = v[3].given ? v[3].text : ""; *s; s++)
        if (*s >= '0' && *s <= '9')
            accept |= 1u << (*s - '0');

    bool nfa = false;
    for (uint8_t q = 0; q < n; q++)
        for (uint8_t c = 0; c < 2; c++)
            if (on[c][q] & (on[c][q] - 1))
                nfa = true;

    static char word[FIELD_MAX + 1];
    for (const char *s = v[4].text; *s; ) {
        uint8_t len = 0;
        uint16_t set = 1u << start;
        ui_step_head("from %s", fsm_set(set));
        for (; *s && *s != ','; s++) {
            if (*s != '0' && *s != '1')
                return ui_error("input: 0s and 1s, commas between");
            word[len++] = *s;
            uint16_t to = 0;
            for (uint8_t q = 0; q < n; q++)
                if (set & (1u << q))
                    to |= on[*s - '0'][q];
            set = to;
            ui_step("%c -> %s", *s, fsm_set(set));
        }
        word[len] = '\0';
        bool ok = (set & accept) != 0;
        fsm_set(set);
        if (accept)
            ui_answer("%s: %s, %s", word, text(), ok ? "accept" : "reject");
        else
            ui_answer("%s: ends in %s", word, text());
        if (*s)
            s++;
    }
    ui_check(nfa ? "NFA: accept if any end state accepts"
                 : "DFA: one state at a time");
    return true;
}

static const struct tool tools[] = {
    { "bits and binary search", FIELDS(bits_fields), tool_bits, NOTES, "SEARCH", NULL },
    { "sort step counts", FIELDS(sorts_fields), tool_sorts, NOTES, "SORTS", NULL },
    { "master theorem", FIELDS(master_fields), tool_master, NOTES, "RECURRENCES", NULL },
    { "state machines", FIELDS(fsm_fields), tool_fsm, "FSMR", "TRACING", NULL },
};

const struct section algo_section = { "Algorithms", TOOLS(tools) };
