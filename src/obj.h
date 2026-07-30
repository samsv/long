#ifndef LONG_OBJ_H
#define LONG_OBJ_H

#include "std/string.h"
#include "common.h"
#include "error.h"
#include "obj/iterator.h"
#include "obj/closure.h"
#include "obj/native_fns.h"
#include "obj/tuple.h"

typedef enum {
    OBJ_STR,
    OBJ_LIST,
    OBJ_TUPLE,
    OBJ_MAP,
    OBJ_ITER,
    OBJ_NATIVE_FN,
    OBJ_CLOSURE,
    OBJ_CLOSURE_MEMBER,
    OBJ_ERR,
} obj_kind;

typedef struct obj_t {
    union {
        sv_str_t str;
        tuple_t tuple;
        list_t list;
        hashmap_t map;
        iter_t iter;
        native_fn_t fn;
        closure_t closure;
        closure_member_t closure_member;
        error_t err;
    };
    obj_kind kind;
} obj_t;

sv_rc_cell_def(obj_t);

#endif
