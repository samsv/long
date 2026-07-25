#ifndef LONG_VALUE_H
#define LONG_VALUE_H

#include <stdbool.h>
#include "obj.h"
#include "std/rc.h"

sv_rc_def(obj_t);

typedef enum {
    VALUE_NUMBER,
    VALUE_NIL,
    VALUE_BOOL,
    VALUE_OBJ,
} value_kind;

typedef struct value_t {
    value_kind kind;
    union {
        double number;
        bool boolean;
        sv_rc_t(obj_t) obj;
    };
} value_t;

#endif
