#ifndef LONG_OBJ_H
#define LONG_OBJ_H

#include "std/string.h"

typedef enum {
    OBJ_STR,
} obj_kind;

typedef struct {
    obj_kind kind;
    union {
        sv_str_t str;
    };
} obj_t;

#endif
