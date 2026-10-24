#include "imath.h"

bool i64_add(int64_t a, int64_t b, int64_t *out)
{
    if (b > 0 ? a > INT64_MAX - b : a < -INT64_MAX - b)
        return false;
    *out = a + b;
    return true;
}

bool i64_sub(int64_t a, int64_t b, int64_t *out)
{
    return i64_add(a, -b, out);
}

/* Checked with a division rather than __builtin_mul_overflow, whose runtime
 * helper might not be in the pure libcrt the ROM build links. */
bool i64_mul(int64_t a, int64_t b, int64_t *out)
{
    if (a && b && i64_abs(a) > INT64_MAX / i64_abs(b))
        return false;
    *out = a * b;
    return true;
}

/* By squaring. If a square overflows while exponent bits remain, the result
 * would too. */
bool i64_pow(int64_t base, int64_t exp, int64_t *out)
{
    int64_t result = 1;
    while (exp) {
        if ((exp & 1) && !i64_mul(result, base, &result))
            return false;
        exp >>= 1;
        if (exp && !i64_mul(base, base, &base))
            return false;
    }
    *out = result;
    return true;
}

int64_t i64_abs(int64_t a)
{
    return a < 0 ? -a : a;
}

int64_t i64_gcd(int64_t a, int64_t b)
{
    a = i64_abs(a);
    b = i64_abs(b);
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int64_t i64_isqrt(int64_t n)
{
    int64_t lo = 0, hi = 3037000499;   /* the largest r whose square fits */
    while (lo < hi) {
        int64_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid <= n)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void i64_divmod(int64_t x, int64_t n, int64_t *q, int64_t *r)
{
    *q = x / n;
    *r = x % n;
    if (*r < 0) {
        *r += n;
        (*q)--;
    }
}

int64_t i64_mod(int64_t x, int64_t n)
{
    int64_t q, r;
    i64_divmod(x, n, &q, &r);
    return r;
}

/* Small factors multiply directly; larger ones add and double, so nothing
 * passes 2n. */
int64_t mulmod(int64_t a, int64_t b, int64_t n)
{
    if (a < INT32_MAX && b < INT32_MAX)
        return a * b % n;
    uint64_t r = 0, x = (uint64_t)a, m = (uint64_t)n;
    for (uint64_t y = (uint64_t)b; y; y >>= 1) {
        if (y & 1)
            r = (r + x) % m;
        x = (x + x) % m;
    }
    return (int64_t)r;
}

int64_t powmod(int64_t x, int64_t y, int64_t n)
{
    int64_t p = 1 % n, s = i64_mod(x, n);
    for (; y; y >>= 1) {
        if (y & 1)
            p = mulmod(p, s, n);
        s = mulmod(s, s, n);
    }
    return p;
}

bool invmod(int64_t x, int64_t n, int64_t *out)
{
    int64_t r0 = n, t0 = 0, r1 = i64_mod(x, n), t1 = 1;
    if (i64_gcd(r1, n) != 1)
        return false;
    /* The t values stay below n in size, so nothing overflows. */
    while (r1 != 1) {
        int64_t q = r0 / r1, r2 = r0 - q * r1, t2 = t0 - q * t1;
        r0 = r1, t0 = t1, r1 = r2, t1 = t2;
    }
    *out = i64_mod(t1, n);
    return true;
}

/* Each step is C(n-k+i, i), a whole number. Dividing by gcd first keeps the
 * product from overflowing when the result fits. */
bool i64_choose(int64_t n, int64_t k, int64_t *out)
{
    if (k < 0 || k > n) {
        *out = 0;
        return true;
    }
    if (k > n - k)
        k = n - k;
    int64_t r = 1;
    for (int64_t i = 1; i <= k; i++) {
        int64_t g = i64_gcd(r, i);
        if (!i64_mul(r / g, (n - k + i) / (i / g), &r))
            return false;
    }
    *out = r;
    return true;
}

bool i64_perm(int64_t n, int64_t k, int64_t *out)
{
    int64_t r = k > n ? 0 : 1;
    for (int64_t i = 0; i < k && r; i++)
        if (!i64_mul(r, n - i, &r))
            return false;
    *out = r;
    return true;
}

bool i64_fact(int64_t n, int64_t *out)
{
    return i64_perm(n, n, out);
}

char *i64_to_str(int64_t v, char *out)
{
    char digits[19];
    uint8_t n = 0;
    if (v < 0) {
        *out++ = '-';
        v = -v;
    }
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *out++ = digits[--n];
    *out = '\0';
    return out;
}
