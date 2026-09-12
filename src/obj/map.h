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

/**
 * Arrays shared by the versions of one node: dense holds the items in
 * insertion order, sparse maps hash slots to dense indices (-1 is empty).
 */
typedef struct {
    sv_vec_t(sparse_item_t) dense;
    sv_vec_t(int64_t) sparse;
} set_store_t;

sv_rc_def(set_store_t);

typedef struct {
    sv_rc_t(set_store_t) store;
    int64_t len;
} sparse_set_t;

typedef struct map_node_t {
    sparse_set_t set;
    hashmap_t child;
    int8_t depth;
} map_node_t;

typedef map_node_t transient_hashmap_t;

sv_rc_cell_def(map_node_t);

typedef struct {
    hashmap_t root;
    map_node_t node;
    int64_t index;
} map_flat_iter_t;

typedef struct {
    hashmap_t root;
    map_node_t parent;
    hashmap_t map;
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
 * Initializes a new map from the (key, values) pair array.
 */
hashmap_t map_init(const value_t*, int64_t, const sv_allocator_t*);
/**
 * Deinitializes the map.
 */
void map_deinit(hashmap_t*, const sv_allocator_t*);

/**
 * Returns the optional value associated with the key. The value is not borrowed.
 */
sv_opt_t(value_t) map_get(hashmap_t, value_t);
/**
 * Inserts or update the key into the map.
 */
hashmap_t map_put(hashmap_t, kv_t, const sv_allocator_t*);
/**
 * Removes the given key from the map.
 */
hashmap_t map_delete(hashmap_t, value_t, const sv_allocator_t*);

/**
 * Counts the amount of items in the map.
 */
int64_t map_count(hashmap_t);

/**
 * Initializes an iterator viewing the map without borrowing it. The map must
 * outlive the iterator; no deinit is needed.
 */
map_iter_t map_iter_init_no_borrow(hashmap_t);
/**
 * Initializes a new iterator from the map.
 */
map_iter_t map_iter_init(hashmap_t);
/**
 * Deinitializes the iterator.
 */
void map_iter_deinit(map_iter_t*, const sv_allocator_t*);
/**
 * Next (non borrowed) kv value.
 */
sv_opt_t(kv_t) map_iter_next(map_iter_t*);

/**
 * Initializes an iterator viewing the transient hashmap; no deinit is
 * needed.
 */
map_iter_t thm_iter_init(transient_hashmap_t);
/**
 * Initializes an empty transient hashmap sized for the expected number of
 * items. Transients are mutated in place and must never be shared; wrap
 * them with transient_to_map to share the result.
 */
transient_hashmap_t thm_init(int64_t expected, const sv_allocator_t*);
/**
 * Deinitializes a transient hashmap.
 */
void thm_deinit(transient_hashmap_t*, const sv_allocator_t*);
/**
 * Returns the optional value associated with the key. The value is not
 * borrowed.
 */
sv_opt_t(value_t) thm_get(transient_hashmap_t, value_t);
/**
 * Counts the items in the transient hashmap.
 */
int64_t thm_count(transient_hashmap_t);
/**
 * Inserts or updates the key in place, borrowing the key and value. Grows
 * the storage when needed.
 */
bool thm_put(transient_hashmap_t*, kv_t, const sv_allocator_t*);
/**
 * Removes the key in place, freeing the removed value; the key slot remains
 * as a tombstone until the next growth. Returns whether the key was present.
 */
bool thm_delete(transient_hashmap_t*, value_t, const sv_allocator_t*);

/**
 * Turns a persistent map into a transient, consuming the handle. Only valid
 * when this is the map's only reference: a shared map is left intact and the
 * error transient (NULL store cell) is returned. Adopts the storage in O(1)
 * when it is exclusively owned; only a map whose storage is still woven into
 * sibling versions is flattened into a fresh node.
 */
transient_hashmap_t map_to_transient(hashmap_t*, const sv_allocator_t*);
/**
 * Wraps a transient into a persistent map in O(1), consuming the transient;
 * the storage is adopted, never copied. The map is the error map on
 * allocation failure.
 */
hashmap_t transient_to_map(transient_hashmap_t*, const sv_allocator_t*);

#endif
