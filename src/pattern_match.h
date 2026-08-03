#ifndef LONG_PATTERN_MATCH_H
#define LONG_PATTERN_MATCH_H

#include "ctx.h"
#include "sexpr.h"

typedef sv_vec_t(sexpr_t) cons_t;

/**
 * Lowers a `(match scrutinee (tuple pattern body)...)` cons into a `do` block
 * that binds the scrutinee and dispatches on pattern type. Takes ownership of
 * the input: on success the input spine is freed, on failure the whole input is
 * freed and an error atom is returned with ctx->err set.
 */
sexpr_t match_compile(sexpr_t, ctx_t*);

#endif
