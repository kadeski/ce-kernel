#include "discrete.h"
#include "../core/imath.h"

#define NOTES "BIGOR"

/* ---- bits and binary search ---- */

static const struct field bits_fields[] = {
    { "n", F_INT },
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
    { "n", F_INT },
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
    { "a", F_INT },
    { "b", F_INT },
    { "k", F_INT },
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

static const struct tool tools[] = {
    { "bits and binary search", FIELDS(bits_fields), tool_bits, NOTES, "SEARCH", NULL },
    { "sort step counts", FIELDS(sorts_fields), tool_sorts, NOTES, "SORTS", NULL },
    { "master theorem", FIELDS(master_fields), tool_master, NOTES, "RECURRENCES", NULL },
};

const struct section algo_section = { "Algorithms", TOOLS(tools) };
