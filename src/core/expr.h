#ifndef CORE_EXPR_H
#define CORE_EXPR_H

#include "frac.h"

#include <stdbool.h>
#include <stdint.h>

/* Longest expression expr_eval takes. */
#define EXPR_LEN_MAX 40

/*
 * Evaluates an expression in exact fractions: + - * / ^, unary minus,
 * parentheses, decimals such as 0.015 (read as 3/200), and functions such
 * as ncr(12,5). A letter or ( right after a value multiplies, so 3a and
 * 0.95(a+100) work. With vars, the names a, b and n stand for vars[0],
 * vars[1] and vars[2]; without, they are unknown names. On failure,
 * expr_error() says why.
 */
bool expr_eval(const char *s, const struct frac *vars, struct frac *out);
const char *expr_error(void);

/* The functions, in the order MATH lists them: "ncr", ...; NULL past the
 * last. */
const char *expr_function(uint8_t i);

#endif
