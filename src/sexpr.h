#ifndef LONG_SEXPR_H
#define LONG_SEXPR_H

#include <stdint.h>
#include "token.h"
#include "std/vector.h"

typedef enum {
    S_ATOM,
    S_CONS,
} sexpr_kind;

struct sexpr_t;
typedef struct sexpr_t sexpr_t;
sv_vec_def(sexpr_t);

typedef struct sexpr_t {
    sexpr_kind tag;
    union {
        token_t atom;
        sv_vec_t(sexpr_t) cons;
    };
} sexpr_t;

void sexpr_free(sexpr_t*, sv_allocator_t*);
const char* sexpr_format(sexpr_t, sv_allocator_t*);

#endif
