#ifndef LONG_TUPLE_H
#define LONG_TUPLE_H

#include "../common.h"
#include "../value.h"

typedef struct {
    value_t value;
    uint32_t id; // we choose an uint32 as a (64 bit) double can safely represented all uint32 values.
} tuple_item_t;

typedef struct tuple_t {
    tuple_item_t* items;
    uint8_t size;
} tuple_t;

/**
 * Initializes the tuple with the given value array. The value array must contain the id as a double value and
 * its corresponding value. The size parameter must be equal to the array length / 2. The pairs must be sorted
 * ascending by id: lookups binary-search and equality compares positionally.
 */
tuple_t tuple_init(const value_t*, uint8_t, const sv_allocator_t*);
/**
 * Deinits the tuples values and its value array.
 */
void tuple_deinit(tuple_t*, const sv_allocator_t*);
/**
 * Checks if two tuples are equal.
 */
bool tuple_eql(tuple_t, tuple_t);
/**
 * Returns the element from the tuple.
 */
sv_opt_t(value_t) tuple_get(tuple_t, uint32_t);

#endif
