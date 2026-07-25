#ifndef LONG_LIST_H
#define LONG_LIST_H

#include "../std/rc.h"
#include "../std/vector.h"

typedef struct value_t value_t;
typedef struct node_t node_t;

typedef struct sv_rc_cell_t(node_t) sv_rc_cell_t(node_t);
sv_rc_wrapper_def(node_t);
typedef sv_rc_t(node_t) list_t;

sv_vec_def(value_t);
typedef sv_vec_t(value_t) bucket_t;

typedef struct node_t {
    bucket_t bucket;
    int64_t start;
    int64_t len;
    list_t tail;
} node_t;

sv_rc_cell_def(node_t);

/**
 * Initializes a new list from the value array. Clones the values into the new list.
 */
list_t ll_init(const value_t*, int64_t, const sv_allocator_t*);
/**
 * Initializes a new list from the value vector. Takes ownership from the vector.
 */
list_t ll_init_from_vec(sv_vec_t(value_t), const sv_allocator_t*);
/**
 * Returns the empty list.
 */
list_t ll_empty(void);
/**
 * Deinitializes the list.
 */
void ll_deinit(list_t*, const sv_allocator_t*);

/**
 * Gets the element at position.
 */
value_t ll_get(list_t, int64_t);

/**
 * Prepends element to list.
 */
list_t ll_prepend(list_t, value_t, const sv_allocator_t*);
/**
 * Prepends array to list.
 */
list_t ll_prepend_arr(list_t, const value_t*, int64_t, const sv_allocator_t*);
/**
 * Adds two lists together.
 */
list_t ll_add(list_t, list_t, const sv_allocator_t*);

/**
 * Inserts element at position
 */
list_t ll_insert(list_t, value_t, int64_t, const sv_allocator_t*);
/**
 * Updates element at position
 */
list_t ll_update(list_t, value_t, int64_t, const sv_allocator_t*);
/**
 * Deletes element at position
 */
list_t ll_delete_at(list_t, int64_t, const sv_allocator_t*);

/**
 * Returns a pointer to the list head or NULL for an empty list.
 */
const value_t* ll_head(list_t);
/**
 * Returns the list tail.
 */
list_t ll_tail(list_t);

/**
 * Returns the number of elements in the list.
 */
int64_t ll_count(list_t);

#endif
