#ifndef LONG_COMMON_H
#define LONG_COMMON_H

#include "std/option.h"
#include "std/vector.h"
#include "std/rc.h"

sv_vec_def(int64_t);

typedef struct value_t value_t;
typedef struct sv_opt_t(value_t) sv_opt_t(value_t);

sv_vec_def(value_t);

typedef struct map_node_t map_node_t;
typedef struct sv_rc_cell_t(map_node_t) sv_rc_cell_t(map_node_t);
sv_rc_wrapper_def(map_node_t);
typedef sv_rc_t(map_node_t) hashmap_t;

#endif
