#ifndef LONG_VM_H
#define LONG_VM_H

#include <stdint.h>
#include "error.h"
#include "value.h"
#include "obj/list.h"
#include "obj/map.h"
#include "std/string.h"
#include "std/vector.h"

typedef struct vm_t vm_t;

sv_vec_def(uint8_t);

typedef sv_vec_t(value_t) value_arr;

typedef struct {
    sv_vec_t(uint8_t) bytecode;
    sv_vec_t(int64_t) lines;
    sv_vec_t(value_t) constants;
    sv_vec_t(vm_t) functions;
} chunk_t;

typedef struct vm_t {
    sv_str_t name;
    uint8_t arity;
    chunk_t chunk;
    value_arr globals;
    value_arr locals;
    value_arr stack;
    value_arr upvalues;
    sv_rc_t(closure_group_t) group;
    int64_t ip;
} vm_t;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQUALS,
    OP_CALL,
    OP_LOAD_CLOSURE,
    OP_CREATE_GROUP,
    OP_GET_MEMBER,
    OP_SET_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_LOCAL,
    OP_GET_LOCAL,
    OP_GET_UPVALUE,
    OP_POP_LOCAL,
    OP_ITER_CREATE,
    OP_ITER_NEXT,
    OP_LOAD_CONSTANT,
    OP_NEGATE,
    OP_POP,
    OP_JUMP,
    OP_JUMP_BACK,
    OP_JUMP_IF_FALSE,
    OP_LIST,
    OP_NOT_EQUALS,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
} vm_instructions;

typedef enum {
    VM_ERR_OOM,
    VM_ERR_UNDEFINED_VARIABLE,
    VM_ERR_OP_UNSUPPORTED_ARGS,
    VM_ERR_NOT_IMPLEMENTED,
    VM_ERR_NO_GROUP,
    VM_ERR_BAD_ARITY,
} vm_error_kinds;

typedef struct {
    int64_t line;
    value_t ops[2]; // for infix size is two, prefix size is 1
    int8_t ops_len;
} vm_op_err;

typedef struct {
    int64_t line;
    uint8_t instruction;
} vm_instruction_err;

typedef struct {
    int64_t line;
    uint8_t expected;
    uint8_t got;
} vm_arity_err;

sv_opt_def(uint8_t);
sv_opt_def(int64_t);

typedef struct {
    vm_t vm;
} vm_builder_t;

chunk_t chunk_init(void);
void chunk_deinit(chunk_t*, const sv_allocator_t*);
vm_t vm_init(sv_str_t);
void vm_deinit(vm_t*, const sv_allocator_t*);
sv_opt_t(error_t) vm_run(vm_t*, const sv_allocator_t*);
/**
 * Frees the payload of an error returned by vm_run.
 */
void vm_err_deinit(error_t*, const sv_allocator_t*);

/**
 * Initializes a new builder wrapping an empty vm.
 */
vm_builder_t vmb_init(sv_str_t);
/**
 * Returns the built vm and invalidates the builder.
 */
vm_t vmb_build(vm_builder_t*);
/**
 * Adds a byte to the bytecode with its source line.
 */
bool vmb_add_byte(vm_builder_t*, uint8_t, int64_t, const sv_allocator_t*);
/**
 * Adds two bytes to the bytecode, both tagged with the source line.
 */
bool vmb_add_bytes(vm_builder_t*, uint8_t, uint8_t, int64_t, const sv_allocator_t*);
/**
 * Registers a constant and emits its load instruction. Returns the constant
 * index, none on allocation failure.
 */
sv_opt_t(uint8_t) vmb_add_constant(vm_builder_t*, value_t, const sv_allocator_t*);
/**
 * Registers a function vm and emits its load instruction with the closure
 * argument count. Returns the function index, none on allocation failure.
 */
sv_opt_t(uint8_t) vmb_add_closure(vm_builder_t*, uint8_t, vm_t, const sv_allocator_t*);
/**
 * Writes the jump offset over the placeholder at the given bytecode index.
 */
void vmb_patch_jump(vm_builder_t*, int64_t, uint16_t);
/**
 * Emits a jump with a placeholder offset. Returns the index to patch, none on
 * allocation failure.
 */
sv_opt_t(int64_t) vmb_add_jump(vm_builder_t*, int64_t, const sv_allocator_t*);
/**
 * Emits a jump back to the given bytecode index.
 */
bool vmb_add_jump_back(vm_builder_t*, int64_t, int64_t, const sv_allocator_t*);
/**
 * Emits a conditional jump with a placeholder offset. Returns the index to
 * patch, none on allocation failure.
 */
sv_opt_t(int64_t) vmb_add_jump_if_false(vm_builder_t*, int64_t, const sv_allocator_t*);

#endif
