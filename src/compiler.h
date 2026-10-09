#ifndef LONG_COMPILER_H
#define LONG_COMPILER_H

#include "ctx.h"
#include "vm.h"
#include "obj/map.h"
#include "obj/native_fns.h"
#include "globals.h"
#include "std/arena.h"

typedef struct locals_t {
    transient_hashmap_t name_indexes;
    struct locals_t* next;
    int64_t offset;
} locals_t;

typedef struct fail_target_t {
    sv_vec_t(int64_t) jumps;
    int64_t locals;
    struct fail_target_t* next;
} fail_target_t;

typedef struct {
    transient_hashmap_t compiled_modules; // holds as keys the full path name of the module to the compiled module_t
    transient_hashmap_t to_be_compiled_modules;
} module_map_t;

typedef struct module_scope_t {
    transient_hashmap_t var_to_modules;
    struct module_scope_t* outer;
} module_scope_t;

typedef struct compiler_t {
    globals_t globals;
    locals_t upvalues;
    locals_t* locals;
    transient_hashmap_t members;
    transient_hashmap_t* record_fields;
    transient_hashmap_t* atoms;
    fail_target_t* fail_targets;

    const char* current_path;
    module_map_t modules;
    module_scope_t* module_scope;

    fn_builder_t builder;
    sv_vec_t(value_t) global_values;

    sv_arena_t* arena;
    const sv_allocator_t* scratch;  // the arena as an allocator
    struct compiler_t* child;
} compiler_t;

typedef struct {
    int64_t max_frames;
    native_fn_t* native_funs;
    int64_t native_count;
} compile_opts_t;

#define COMPILER_DEFAULT_OPTS (compile_opts_t) { .max_frames = 1000000 }

/**
 * Compiles a file.
 */
vm_t compile(const char* base_path, compile_opts_t, ctx_t*);
/**
 * Compiles many files at once, each namespaced by its own name.
 */
vm_t compile_files(const char** files, int64_t count, compile_opts_t, ctx_t*);

typedef enum {
    C_ERR_OOM = 1,
    C_ERR_UNDEFINED_VARIABLE,
    C_ERR_REDEFINED,
    C_ERR_UNEXPECTED_SEXPR,
    C_ERR_IMPORT_CICLE,
    C_ERR_NOT_CALLABLE,
    C_ERR_NOT_IMPLEMENTED,
    C_ERR_JUMP_TOO_LONG,
    C_ERR_LIMIT_EXCEEDED,
} compiler_error_kind;

#endif
