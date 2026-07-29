#ifndef LONG_OBJ_H
#define LONG_OBJ_H

#include "std/string.h"
#include "common.h"
#include "obj/iterator.h"
#include "obj/closure.h"

typedef enum {
    OBJ_STR,
    OBJ_LIST,
    OBJ_MAP,
    OBJ_ITER,
    OBJ_CLOSURE,
    OBJ_CLOSURE_MEMBER,
} obj_kind;

typedef struct obj_t {
    obj_kind kind;
    union {
        sv_str_t str;
        list_t list;
        map_t map;
        iter_t iter;
        closure_t closure;
        closure_member_t closure_member;
    };
} obj_t;

#endif
