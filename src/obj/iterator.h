#ifndef LONG_ITERATOR_H
#define LONG_ITERATOR_H

#include "../std/allocator.h"
#include "list.h"
#include "map.h"
#include "str.h"

typedef enum {
    ITER_LIST,
    ITER_STR,
    ITER_MAP,
} iter_kind;

typedef struct {
    iter_kind kind;
    union {
        ll_iter_t list;
        str_iter_t str;
        map_iter_t map;
    };
} iter_t;

/**
 * Initializes an iterator from an iterable value.
 */
iter_t iter_init(value_t);
/**
 * Deinitializes the iterator.
 */
void iter_deinit(iter_t*, const sv_allocator_t*);
/**
 * Next value, owned by the caller; nil when the iterator is exhausted. A map
 * yields a `{key, value}` record per entry.
 */
value_t iter_next(iter_t*, const sv_allocator_t*);

#endif
