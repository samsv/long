#ifndef LONG_PATTERN_SHAPE_H
#define LONG_PATTERN_SHAPE_H

#include "sexpr.h"

/**
 * The expression a trailing `(.. e)` carries, or NULL when the items end without one. A
 * list pattern's tail and a spread are the same shape.
 */
const sexpr_t* spread_of(const sexpr_t* items, int64_t n);
/**
 * True when the list pattern ends with a `(.. tail)` expression.
 */
bool list_has_tail(sexpr_t);
int64_t list_n_fixed(sexpr_t);
/**
 * True for a name or a list, the only shapes a list tail can match.
 */
bool is_list_tail(sexpr_t);
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

/**
 * True for a plain name, an identifier atom.
 */
bool pattern_is_name(sexpr_t);
/**
 * True for an alias pattern, `(= name p)`. One side is always a name.
 */
bool pattern_is_alias(sexpr_t);
/**
 * The name an alias binds: the right side when it is a name, so `x = y` binds `y` and
 * keeps `x` as the pattern variable.
 */
sexpr_t alias_name(sexpr_t);
/**
 * The side of an alias that goes on matching.
 */
sexpr_t alias_pattern(sexpr_t);

#endif
