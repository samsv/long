#ifndef LONG_PATTERN_MATCH_H
#define LONG_PATTERN_MATCH_H

#include "sexpr.h"
#include "std/option.h"

typedef sv_vec_t(sexpr_t) cons_t;

typedef struct {
    cons_t pattern;
    sexpr_t expr;
} pattern_item_t;

typedef struct {
    sexpr_t* variables;
    int32_t var_len;

    pattern_item_t* patterns;
    int32_t pattern_size;

    sexpr_t default_expr;
} match_t;

sv_opt_def(match_t);

sv_opt_t(match_t) match_init(sexpr_t);
sexpr_t match_compile(match_t);

#endif
