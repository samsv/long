#ifndef LONG_VALUE_H
#define LONG_VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include "common.h"
#include "std/rc.h"
#include "std/option.h"
#include "std/string.h"
#include "std/allocator.h"
#include "obj/native_fns.h"
#include "obj/closure.h"
#include "error.h"

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

#define IS_STR(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_STR)
#define IS_ERR(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_ERR)
#define IS_LIST(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_LIST)
#define IS_NATIVE(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_NATIVE_FN)
#define IS_CLOSURE(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_CLOSURE)
#define IS_CLOSURE_MEMBER(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_CLOSURE_MEMBER)
#define IS_ITER(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_ITER)
#define IS_RECORD(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_RECORD)
#define IS_TUPLE(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_TUPLE)
#define IS_MAP(v) ((v).kind == VALUE_OBJ && (v).obj.cell->value.kind == OBJ_MAP)

#define AS_STR(v) ((v).obj.cell->value.str)
#define AS_ERR(v) ((v).obj.cell->value.err)
#define AS_LIST(v) ((v).obj.cell->value.list)
#define AS_NATIVE(v) ((v).obj.cell->value.fn)
#define AS_CLOSURE(v) ((v).obj.cell->value.closure)
#define AS_CLOSURE_MEMBER(v) ((v).obj.cell->value.closure_member)
#define AS_ITER(v) ((v).obj.cell->value.iter)
#define AS_RECORD(v) ((v).obj.cell->value.record)
#define AS_TUPLE(v) ((v).obj.cell->value.tuple)
#define AS_MAP(v) ((v).obj.cell->value.map)

static const value_t value_nil = { .kind = VALUE_NIL };
static const value_t value_true = { .kind = VALUE_BOOL, .boolean = true };
static const value_t value_false = { .kind = VALUE_BOOL, .boolean = false };

sv_opt_def(value_t);

void value_free(value_t*, const sv_allocator_t*);
value_t value_borrow(value_t);
bool value_eql(value_t, value_t);

/**
 * Converts the value a string representation.
 */
sv_str_t value_to_str(value_t, const vm_ctx_t*);

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
 * Creates a new error value.
 */
value_t value_init_err(error_t, const sv_allocator_t*);

/**
 * Creates a new native function.
 */
value_t value_init_native(native_fn_t, const sv_allocator_t*);

/**
 * Creates a closure member value taking ownership of the group reference.
 * obj.cell is NULL on allocation failure.
 */
value_t value_init_closure_member(sv_rc_t(closure_group_t), int64_t, const sv_allocator_t*);
/**
 * Creates a string value copying the given string. obj.cell is NULL on
 * allocation failure.
 */
value_t value_init_str(sv_str_t, const sv_allocator_t*);
/**
 * Creates a string value taking ownership of the given string. obj.cell is
 * NULL on allocation failure (the string is freed).
 */
value_t value_init_str_own(sv_str_t, const sv_allocator_t*);
/**
 * Creates a map value borrowing n_pairs (key, value) pairs from the flat
 * array. Later duplicate keys win. obj.cell is NULL on allocation failure.
 */
value_t value_init_map(const value_t*, int64_t, const sv_allocator_t*);
/**
 * Creates a tuple value borrowing the values. The value array order must be (id, value).
 * obj.cell is NULL on allocation failure.
 */
value_t value_init_record(const value_t*, uint8_t, const sv_allocator_t*);
/**
 * Creates a tuple value borrowing the values. obj.cell is NULL on allocation
 * failure.
 */
value_t value_init_tuple(const value_t*, uint8_t, const sv_allocator_t*);

#endif
