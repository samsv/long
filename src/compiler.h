#ifndef LONG_COMPILER_H
#define LONG_COMPILER_H

#include "ctx.h"
#include "vm.h"
#include "obj/map.h"
#include "obj/native_fns.h"

typedef struct {
    transient_hashmap_t name_indexes;
} globals_t;

typedef struct locals_t {
    transient_hashmap_t name_indexes;
    struct locals_t* next;
    int64_t offset;
} locals_t;

typedef struct {
    globals_t globals;
    locals_t upvalues;
    locals_t* locals;
    transient_hashmap_t members;
    transient_hashmap_t* tuple_fields;
    vm_builder_t builder;
} compiler_t;

vm_t compile(const char* source_code, ctx_t*);
bool add_native_fn(compiler_t*, native_fn_t, ctx_t*);

typedef enum {
    C_ERR_OOM,
    C_ERR_UNDEFINED_VARIABLE,
    C_ERR_REDEFINED,
    C_ERR_UNEXPECTED_SEXPR,
    C_ERR_NOT_CALLABLE,
    C_ERR_NOT_IMPLEMENTED,
    C_ERR_JUMP_TOO_LONG,
} compiler_error_kind;

#endif
