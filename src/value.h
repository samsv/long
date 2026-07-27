#ifndef LONG_VALUE_H
#define LONG_VALUE_H

#include <stdbool.h>
#include "obj.h"
#include "std/rc.h"
#include "std/option.h"

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

sv_opt_def(value_t);

void value_free(value_t*, const sv_allocator_t*);
value_t value_borrow(value_t);
bool value_eql(value_t, value_t);
/**
 * Creates a list value cloning the values (see ll_init). obj.cell is NULL on
 * allocation failure.
 */
value_t value_init_list(const value_t*, int64_t, const sv_allocator_t*);
/**
 * Creates an iterator value borrowing the given list value. The value must
 * hold an iterable object. obj.cell is NULL on allocation failure.
 */
value_t value_init_iter(value_t, const sv_allocator_t*);
/**
 * Creates a closure value over the function, borrowing the upvalues.
 * obj.cell is NULL on allocation failure.
 */
value_t value_init_closure(vm_t*, const value_t*, int64_t, const sv_allocator_t*);
/**
 * Creates a closure member value taking ownership of the group reference.
 * obj.cell is NULL on allocation failure.
 */
value_t value_init_closure_member(sv_rc_t(closure_group_t), int64_t, const sv_allocator_t*);

#endif
