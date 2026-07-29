#ifndef LONG_MAP_H
#define LONG_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include "../std/rc.h"
#include "../std/vector.h"
#include "../std/option.h"
#include "../common.h"
#include "../value.h"

typedef struct {
    value_t key;
    value_t value;
} kv_t;

sv_opt_def(kv_t);

typedef struct {
    int64_t sparse_index;
    value_t key;
    sv_opt_t(value_t) value;
} sparse_item_t;

sv_vec_def(sparse_item_t);
sv_vec_def(int64_t);

sv_rc_def(sv_vec_t(sparse_item_t));
sv_rc_def(sv_vec_t(int64_t));
typedef sv_rc_t(sv_vec_t(sparse_item_t)) dense_t;
typedef sv_rc_t(sv_vec_t(int64_t)) sparse_t;

typedef struct {
    dense_t dense;
    sparse_t sparse;
    int64_t len;
} sparse_set_t;

typedef struct map_node_t {
    sparse_set_t set;
    map_t child;
    int8_t depth;
} map_node_t;

sv_rc_cell_def(map_node_t);

typedef struct {
    map_t root;
    map_node_t node;
    int64_t index;
} map_flat_iter_t;

typedef struct {
    map_t root;
    map_node_t parent;
    map_t map;
    int64_t index;
    int8_t depth;
} map_depth_iter_t;

typedef enum {
    MAP_ITER_FLAT,
    MAP_ITER_DEPTH,
} map_iter_kind;

typedef struct {
    map_iter_kind kind;
    union {
        map_flat_iter_t flat;
        map_depth_iter_t depth;
    };
} map_iter_t;

/**
 * Initializes a new map from the key values array.
 */
map_t map_init(const kv_t*, int64_t, const sv_allocator_t*);
/**
 * Deinitializes the map.
 */
void map_deinit(map_t*, const sv_allocator_t*);

/**
 * Returns the optional value associated with the key. The value is not borrowed.
 */
sv_opt_t(value_t) map_get(map_t, value_t);
/**
 * Inserts or update the key into the map.
 */
map_t map_put(map_t, kv_t, const sv_allocator_t*);
/**
 * Removes the given key from the map.
 */
map_t map_delete(map_t, value_t, const sv_allocator_t*);

/**
 * Counts the amount of items in the map.
 */
int64_t map_count(map_t);

/**
 * Initializes an iterator viewing the map without borrowing it. The map must
 * outlive the iterator; no deinit is needed.
 */
map_iter_t map_iter_init_no_borrow(map_t);
/**
 * Initializes a new iterator from the map.
 */
map_iter_t map_iter_init(map_t);
/**
 * Deinitializes the iterator.
 */
void map_iter_deinit(map_iter_t*, const sv_allocator_t*);
/**
 * Next (non borrowed) kv value.
 */
sv_opt_t(kv_t) map_iter_next(map_iter_t*);

#endif
