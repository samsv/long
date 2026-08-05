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

void sexpr_free(sexpr_t*, const sv_allocator_t*);
sv_str_t sexpr_format(sexpr_t, const sv_allocator_t*);

sexpr_t atom_sexpr(token_t);
sexpr_t cons_sexpr(sv_vec_t(sexpr_t));
/**
 * True when the sexpr is the error atom the parser and the match lowering
 * return to signal failure.
 */
bool is_error_sexpr(sexpr_t);

/**
 * Deep copies a tree. An atom copies by value because it owns nothing: its
 * strings view the source or a static buffer, which is why freeing one is a
 * no-op.
 */
bool sexpr_clone(sexpr_t*, sexpr_t, const sv_allocator_t*);

#endif
