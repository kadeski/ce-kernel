#ifndef KERNEL_IMATH_H
#define KERNEL_IMATH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 64-bit integer math for calc and the study tools. Values stay within
 * +-INT64_MAX, so negating one never overflows. Each function that could
 * leave that range returns false instead of a wrapped value.
 */

bool i64_add(int64_t a, int64_t b, int64_t *out);
bool i64_sub(int64_t a, int64_t b, int64_t *out);
bool i64_mul(int64_t a, int64_t b, int64_t *out);
/* base^exp for exp >= 0; 0^0 is 1. */
bool i64_pow(int64_t base, int64_t exp, int64_t *out);

int64_t i64_abs(int64_t a);
/* Always >= 0; gcd(0, 0) is 0. */
int64_t i64_gcd(int64_t a, int64_t b);
/* Largest r with r*r <= n, for n >= 0. */
int64_t i64_isqrt(int64_t n);

/* Floor division: x = q*n + r with 0 <= r < n, for n > 0. C's / and %
 * truncate toward zero instead. */
void i64_divmod(int64_t x, int64_t n, int64_t *q, int64_t *r);
int64_t i64_mod(int64_t x, int64_t n);

/* a*b mod n and x^y mod n, for 0 <= a, b < n, y >= 0 and n >= 1. */
int64_t mulmod(int64_t a, int64_t b, int64_t n);
int64_t powmod(int64_t x, int64_t y, int64_t n);
/* The s in 1..n-1 with s*x mod n = 1, for n >= 2. False if gcd(x, n) != 1. */
bool invmod(int64_t x, int64_t n, int64_t *out);

/* C(n, k), P(n, k) and n! for n, k >= 0. C and P are 0 when k > n. */
bool i64_choose(int64_t n, int64_t k, int64_t *out);
bool i64_perm(int64_t n, int64_t k, int64_t *out);
bool i64_fact(int64_t n, int64_t *out);

/* Writes v in decimal at out and returns the end, where it puts a NUL. */
char *i64_to_str(int64_t v, char *out);

#endif
