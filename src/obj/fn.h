#ifndef LONG_FN_H
#define LONG_FN_H

#include <stdint.h>
#include "../std/string.h"
#include "../std/option.h"
#include "../common.h"

sv_vec_def(fn_t);
sv_opt_def(uint32_t);
sv_opt_def(int64_t);

typedef struct {
    sv_vec_t(uint8_t) bytecode;
    sv_vec_t(int64_t) lines;
    sv_vec_t(value_t) constants;
    sv_vec_t(fn_t) functions;
} chunk_t;

typedef struct fn_t {
    sv_str_t name;
    uint8_t arity;
    chunk_t chunk;
} fn_t;

chunk_t chunk_init(void);
void chunk_deinit(chunk_t*, const sv_allocator_t*);

/**
 * Frees the function, its chunk and the functions nested in it.
 */
void fn_deinit(fn_t*, const sv_allocator_t*);

typedef struct {
    fn_t fn;
} fn_builder_t;

/**
 * Initializes a builder wrapping an empty function that owns `name`.
 */
fn_builder_t fnb_init(sv_str_t);
/**
 * Returns the built function and invalidates the builder.
 */
fn_t fnb_build(fn_builder_t*);
/**
 * Adds a byte to the bytecode with its source line.
 */
bool fnb_add_byte(fn_builder_t*, uint8_t, int64_t, const sv_allocator_t*);
/**
 * Adds two bytes to the bytecode, both tagged with the source line.
 */
bool fnb_add_bytes(fn_builder_t*, uint8_t, uint8_t, int64_t, const sv_allocator_t*);
/**
 * Emits an instruction with an operand of up to four bytes, prefixed with
 * OP_EXTENDED_ARG when it does not fit one.
 */
bool fnb_add_arg(fn_builder_t*, uint8_t, uint32_t, int64_t, const sv_allocator_t*);
/**
 * Registers a constant and emits its load instruction. Returns the constant
 * index, none on allocation failure or past the uint32 index space.
 */
sv_opt_t(uint32_t) fnb_add_constant(fn_builder_t*, value_t, const sv_allocator_t*);
/**
 * Registers a nested function without emitting anything. Returns its index,
 * none on allocation failure or past the uint32 index space.
 */
sv_opt_t(uint32_t) fnb_add_function(fn_builder_t*, fn_t, const sv_allocator_t*);
/**
 * Registers a nested function and emits its load instruction with the closure
 * argument count. Returns the function index, none on failure.
 */
sv_opt_t(uint32_t) fnb_add_closure(fn_builder_t*, uint8_t, fn_t, const sv_allocator_t*);
/**
 * Writes the jump offset over the placeholder at the given bytecode index.
 */
void fnb_patch_jump(fn_builder_t*, int64_t, uint16_t);
/**
 * Emits a jump with a placeholder offset. Returns the index to patch, none on
 * allocation failure.
 */
sv_opt_t(int64_t) fnb_add_jump(fn_builder_t*, int64_t, const sv_allocator_t*);
/**
 * Emits a jump back to the given bytecode index.
 */
bool fnb_add_jump_back(fn_builder_t*, int64_t, int64_t, const sv_allocator_t*);
/**
 * Emits a conditional jump with a placeholder offset. Returns the index to
 * patch, none on allocation failure.
 */
sv_opt_t(int64_t) fnb_add_jump_if_false(fn_builder_t*, int64_t, const sv_allocator_t*);

#endif
