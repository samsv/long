#ifndef LONG_OBJ_H
#define LONG_OBJ_H

#include "std/string.h"
#include "obj/iterator.h"

typedef enum {
    OBJ_STR,
    OBJ_LIST,
    OBJ_ITER,
} obj_kind;

typedef struct {
    obj_kind kind;
    union {
        sv_str_t str;
        list_t list;
        iter_t iter;
    };
} obj_t;

#endif
