#ifndef LONG_PATTERN_MATCH_2_H
#define LONG_PATTERN_MATCH_2_H

#include "ctx.h"
#include "sexpr.h"
#include "std/arena.h"

/**
 * Lowers a `(match val (tuple pattern body)...)` expr into a chain of
 * `if` `else` comparisons. The sexpr is allocated into the passed arena.
 */
sexpr_t match_compile_2(sexpr_t, ctx_t*, sv_arena_t*);

#endif
