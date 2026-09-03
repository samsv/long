#ifndef LONG_PATTERN_SHAPE_H
#define LONG_PATTERN_SHAPE_H

#include "sexpr.h"

/**
 * True when the list pattern ends with a `(.. tail)` expression.
 */
bool list_has_tail(sexpr_t);
int64_t list_n_fixed(sexpr_t);
/**
 * True when the record pattern ends in the `..` marker.
 */
bool record_is_open(sexpr_t);
int64_t record_n_fields(sexpr_t);
/**
 * True when the hashmap pattern ends in the `..` marker.
 */
bool hashmap_is_open(sexpr_t);
int64_t hashmap_n_keys(sexpr_t);

#endif
