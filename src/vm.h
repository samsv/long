#ifndef LONG_VM_H
#define LONG_VM_H

#include <stdint.h>
#include "error.h"
#include "std/logger.h"
#include "value.h"
#include "obj.h"
#include "common.h"
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

/**
 * A VM context to be passed to native functions.
 */
typedef struct vm_ctx_t {
    const sv_allocator_t* alloc;
    sv_logger_t logger;

    const char** record_key_names;
    uint32_t record_names_sizes;
} vm_ctx_t;

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

    vm_ctx_t ctx;
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
    OP_NOT_EQUALS,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_LIST,
    OP_HASHMAP,
    OP_RECORD,
    OP_TUPLE,
    OP_INDEX,
    OP_LENGTH,
    OP_RECORD_GET,
    OP_RECORD_GET_OR_NIL,
    OP_HASHMAP_GET_OR_NIL,
    OP_NOT,
    OP_DUP,
    OP_IS_STR,
    OP_IS_NUMBER,
    OP_IS_BOOL,
    OP_IS_NIL,
    OP_IS_LIST,
    OP_IS_NIL_LIST,
    OP_IS_CONS,
    /* The size is a bytecode operand because tuple_t.size and record_t.size are
     * both uint8_t, so a pattern can never need a larger one. */
    OP_IS_TUPLE,
    OP_IS_RECORD,
    OP_IS_RECORD_ANY,
    /* A hashmap has no such cap, so this one takes its size off the stack. */
    OP_IS_HASHMAP,
    OP_IS_HASHMAP_ANY,
    OP_HAS_FIELD,
    OP_HAS_KEY,
    OP_LIST_UNCONS,
    OP_ASSERT_MATCH,
    OP_NO_MATCH,
    OP_SWAP,
    OP_RETURN,
} vm_instructions;

typedef enum {
    VM_ERR_OOM,
    VM_ERR_UNDEFINED_VARIABLE,
    VM_ERR_OP_UNSUPPORTED_ARGS,
    VM_ERR_NOT_IMPLEMENTED,
    VM_ERR_NO_GROUP,
    VM_ERR_BAD_ARITY,
    VM_ERR_KEY_NOT_FOUND,
    VM_ERR_FIELD_NOT_FOUND,
    VM_ERR_WRONG_TYPE,
    VM_ERR_MATCH_FAILED,
    VM_ERR_NO_CLAUSE,
} vm_error_kinds;

/**
 * All VM error types must have this as their first argument.
 */
typedef struct {
    int64_t line;
} vm_err_t;

typedef struct {
    vm_err_t vm_err;
    value_t ops[2]; // for infix size is two, prefix size is 1
    int8_t ops_len;
} vm_op_err;

typedef struct {
    vm_err_t vm_err;
    uint8_t instruction;
} vm_instruction_err;

typedef struct {
    vm_err_t vm_err;
    uint8_t expected;
    uint8_t got;
} vm_arity_err;

typedef struct {
    vm_err_t vm_err;
    value_t got;
    value_kind expected_v;
    obj_kind expected_o;
} vm_wrong_type_err;

sv_opt_def(uint8_t);
sv_opt_def(int64_t);

typedef struct {
    vm_t vm;
} vm_builder_t;

chunk_t chunk_init(void);
void chunk_deinit(chunk_t*, const sv_allocator_t*);

vm_t vm_init(sv_str_t);
void vm_deinit(vm_t*, const sv_allocator_t*);

/**
 * Interprets the VM bytecode.
 */
sv_opt_t(error_t) vm_run(vm_t*);

/**
 * Frees the payload of an error returned by vm_run.
 */
void vm_err_deinit(error_t*, const sv_allocator_t*);

/**
 * Initializes a new builder wrapping an empty vm.
 */
vm_builder_t vmb_init(sv_str_t);

/**
 * Adds a global variable to the vm. Returns true on success, false otherwise.
 */
bool vmb_add_global(vm_builder_t*, value_t, const sv_allocator_t*);
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
