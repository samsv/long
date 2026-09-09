#ifndef LONG_RECORD_H
#define LONG_RECORD_H

#include "../common.h"
#include "../value.h"

typedef struct {
    value_t value;
    uint32_t id; // we choose an uint32 as a (64 bit) double can safely represented all uint32 values.
} record_item_t;

typedef struct record_t {
    record_item_t* items;
    uint8_t size;
} record_t;

/**
 * Initializes the tuple with the given value array. The value array must contain the id as a double value and
 * its corresponding value. The size parameter must be equal to the array length / 2. The pairs must be sorted
 * ascending by id: lookups binary-search and equality compares positionally.
 */
record_t record_init(const value_t*, uint8_t, const sv_allocator_t*);
/**
 * Deinits the tuples values and its value array.
 */
void record_deinit(record_t*, const sv_allocator_t*);
/**
 * Checks if two tuples are equal.
 */
bool record_eql(record_t, record_t);
/**
 * Returns the element from the tuple.
 */
sv_opt_t(value_t) record_get(record_t, uint32_t);
/**
 * A copy of the record with the given fields replaced.
 * items is NULL on failure: missing is the id of the first field the record does
 * not have, or -1 when the allocation failed.
 */
record_t record_update(record_t, const value_t* pairs, uint8_t n, int64_t* missing,
                       const sv_allocator_t*);

#endif
