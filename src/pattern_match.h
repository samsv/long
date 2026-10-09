#ifndef LONG_PATTERN_MATCH_2_H
#define LONG_PATTERN_MATCH_2_H

#include "ctx.h"
#include "sexpr.h"

/**
 * Lowers a `(match val (tuple pattern body)...)` expr into a chain of
 * `if` `else` comparisons, allocated with the passed allocator.
 */
sexpr_t match_compile(sexpr_t, ctx_t*, const sv_allocator_t*);

#endif
