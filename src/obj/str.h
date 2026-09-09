#ifndef LONG_STR_H
#define LONG_STR_H

#include <stdint.h>
#include "../common.h"
#include "../std/string.h"
#include "../std/allocator.h"

typedef struct {
    sv_str_t* str;
    int64_t i;
} str_iter_t;

/**
 * Initializes an iterator over the unicode characters of a string obj,
 * borrowing the obj through the str pointer: the string must be the str
 * member of a refcounted obj_t, whose union-first layout makes the pointer
 * castable to its rc cell.
 */
str_iter_t str_iter_init(sv_str_t*);
/**
 * Deinitializes the iterator, releasing the string obj.
 */
void str_iter_deinit(str_iter_t*, const sv_allocator_t*);
/**
 * Next character as a new owned string value, nil when the iterator is
 * exhausted. obj.cell is NULL on allocation failure.
 */
value_t str_iter_next(str_iter_t*, const sv_allocator_t*);

#endif
