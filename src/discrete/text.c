#include "discrete.h"
#include "../kernel/imath.h"

#include <string.h>

#define TEXT_MAX 100                    /* about three lines on the form */

static char buf[TEXT_MAX + 4];
static size_t len;

void text_clear(void)
{
    len = 0;
    buf[0] = '\0';
}

void text_add(const char *s)
{
    size_t n = strlen(s);
    if (len > TEXT_MAX)
        return;                         /* already ended with "..." */
    if (len + n > TEXT_MAX) {
        strcpy(&buf[len], "...");
        len = TEXT_MAX + 1;
        return;
    }
    memcpy(&buf[len], s, n + 1);
    len += n;
}

void text_int(int64_t v)
{
    char num[21];
    i64_to_str(v, num);
    text_add(num);
}

const char *text(void)
{
    return buf;
}

/* 32-bit division, which is much faster than 64-bit on the eZ80. */
bool is_prime(int64_t n)
{
    if (n < 2)
        return false;
    uint32_t m = (uint32_t)n;
    if (m % 2 == 0)
        return m == 2;
    for (uint32_t d = 3; d <= m / d; d += 2)
        if (m % d == 0)
            return false;
    return true;
}
