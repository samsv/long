#ifndef LONG_PATTERN_MATCH_H
#define LONG_PATTERN_MATCH_H

#include "ctx.h"
#include "sexpr.h"

/**
 * Lowers a `(match scrutinee (tuple pattern body)...)` cons into a `do` block
 * that binds the scrutinee and dispatches on the pattern matrix. Takes
 * ownership of the input: on success the input spine is freed, on failure the
 * whole input is freed and an error atom is returned with ctx->err set.
 */
sexpr_t match_compile(sexpr_t, ctx_t*);

/**
 * Rewrites every `(match ...)` node in the tree, children first, so a match
 * nested in a clause body is lowered before its parent. Same ownership contract
 * as match_compile.
 */
sexpr_t match_lower_tree(sexpr_t, ctx_t*);

/**
 * Shape predicates over pattern conses, shared with the destructuring compiler.
 * `..` in a list is a `(.. tail)` cons in the last position; in a record it is a
 * bare `..` atom there.
 */
bool list_has_tail(sexpr_t);
int64_t list_n_fixed(sexpr_t);
bool record_is_open(sexpr_t);
int64_t hashmap_n_keys(sexpr_t);
bool hashmap_is_open(sexpr_t);
int64_t record_n_fields(sexpr_t);

#endif
