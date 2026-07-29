#ifndef LONG_OBJ_H
#define LONG_OBJ_H

#include "std/string.h"
#include "common.h"
#include "error.h"
#include "obj/iterator.h"
#include "obj/closure.h"
#include "obj/native_fns.h"

typedef enum {
    OBJ_STR,
    OBJ_LIST,
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
        list_t list;
        map_t map;
        iter_t iter;
        native_fn_t fn;
        closure_t closure;
        closure_member_t closure_member;
        error_t err;
    };
    obj_kind kind;
} obj_t;

#endif
