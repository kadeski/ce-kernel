#ifndef CORE_FRAC_H
#define CORE_FRAC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * An exact fraction: den > 0 and gcd(num, den) = 1 after every step. Pure
 * integer math, since the toolchain's float routines call TI's boot code.
 * Each operation returns false on overflow, so a wrong answer never shows.
 */
struct frac {
    int64_t num;
    int64_t den;
};

struct frac frac_int(int64_t v);
/* num/den reduced; false if den is 0. */
bool frac_make(int64_t num, int64_t den, struct frac *out);
bool frac_is_int(struct frac a);

bool frac_add(struct frac a, struct frac b, struct frac *out);
bool frac_sub(struct frac a, struct frac b, struct frac *out);
bool frac_mul(struct frac a, struct frac b, struct frac *out);  /* cancels across first */
bool frac_div(struct frac a, struct frac b, struct frac *out);  /* false if b is 0 */
bool frac_pow(struct frac a, int64_t exp, struct frac *out);    /* exp < 0 inverts */

/* a rounded to the nearest multiple of 2^-bits (bits <= 40), for a >= 0:
 * close to a, with a denominator small enough to keep multiplying. */
bool frac_round(struct frac a, uint8_t bits, struct frac *out);

/* -1, 0 or 1. Exact, and never overflows. */
int8_t frac_cmp(struct frac a, struct frac b);

/*
 * Text for a fraction, in one of a few static buffers that later calls
 * reuse: frac_str gives "5" or "-2/7"; frac_show adds the decimal, as in
 * "19/100 = 0.19" or "200/497 ~ 0.4024" ("=" when it is exact).
 */
const char *frac_str(struct frac f);
const char *frac_show(struct frac f);
/* Just the decimal, as in "= 0.19" or "~ 0.4024". */
const char *frac_decimal(struct frac f);

#endif
