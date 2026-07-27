#ifndef LONG_ITERATOR_H
#define LONG_ITERATOR_H

#include "../std/allocator.h"
#include "list.h"

typedef enum {
    ITER_LIST,
} iter_kind;

typedef struct {
    iter_kind kind;
    union {
        ll_iter_t list;
    };
} iter_t;

/**
 * Initializes an iterator from an iterable value. The value must hold an
 * iterable object (currently OBJ_LIST).
 */
iter_t iter_init(value_t);
/**
 * Deinitializes the iterator.
 */
void iter_deinit(iter_t*, const sv_allocator_t*);
/**
 * Next (non borrowed) value, nil when the iterator is exhausted.
 */
value_t iter_next(iter_t*);

#endif
