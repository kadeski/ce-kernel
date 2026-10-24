#ifndef DISCRETE_DISCRETE_H
#define DISCRETE_DISCRETE_H

#include "../kernel/ui.h"

/* The discrete math tools, one section per file, each a list of forms. */
extern const struct section nt_section;         /* numtheory.c */
extern const struct section count_section;      /* counting.c */
extern const struct section recur_section;      /* recur.c */
extern const struct section prob_section;       /* prob.c */
extern const struct section algo_section;       /* algo.c */

/* For the tables of fields and tools in each file. */
#define FIELDS(f) f, sizeof f / sizeof f[0]
#define TOOLS(t)  t, sizeof t / sizeof t[0]

/* A line of output built in pieces, such as a list of terms, for one
 * ui_say() or ui_answer(). It stops with "..." when it gets too long. */
void text_clear(void);
void text_add(const char *s);
void text_int(int64_t v);
const char *text(void);

/* Trial division, for n below 2^32. */
bool is_prime(int64_t n);

#endif
