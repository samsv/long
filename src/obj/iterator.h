#ifndef LONG_ITERATOR_H
#define LONG_ITERATOR_H

#include "../std/allocator.h"
#include "list.h"
#include "str.h"

typedef enum {
    ITER_LIST,
    ITER_STR,
} iter_kind;

typedef struct {
    iter_kind kind;
    union {
        ll_iter_t list;
        str_iter_t str;
    };
} iter_t;

/**
 * Initializes an iterator from an iterable value. The value must hold an
 * iterable object (currently OBJ_LIST or OBJ_STR).
 */
iter_t iter_init(value_t);
/**
 * Deinitializes the iterator.
 */
void iter_deinit(iter_t*, const sv_allocator_t*);
/**
 * Next value, owned by the caller; nil when the iterator is exhausted.
 * obj.cell is NULL on allocation failure.
 */
value_t iter_next(iter_t*, const sv_allocator_t*);

#endif
