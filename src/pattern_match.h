#ifndef LONG_PATTERN_MATCH_H
#define LONG_PATTERN_MATCH_H

#include "sexpr.h"
#include "std/option.h"

typedef sv_vec_t(sexpr_t) cons_t;

typedef struct {
    sexpr_t* variables;
    int32_t var_len;

    sexpr_t* patterns;
    sexpr_t* expr;
    int32_t pattern_size;

    sexpr_t default_expr;
} match_t;

sv_opt_def(match_t);

sv_opt_t(match_t) match_init(sexpr_t);
sexpr_t match_compile(match_t);

#endif
