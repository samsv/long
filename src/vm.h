#ifndef LONG_VM_H
#define LONG_VM_H

#include <stdint.h>
#include "error.h"
#include "std/logger.h"
#include "value.h"
#include "obj.h"
#include "obj/fn.h"
#include "common.h"
#include "std/vector.h"

typedef struct vm_t vm_t;

typedef sv_vec_t(value_t) value_arr;

/**
 * A VM context to be passed to native functions.
 */
typedef struct vm_ctx_t {
    const sv_allocator_t* alloc;
    sv_logger_t logger;

    const char** record_key_names;
    uint32_t record_names_sizes;
} vm_ctx_t;

typedef struct {
    fn_t* fn;
    int64_t ip;

    int64_t locals_offset;
    int64_t stack_offset;

    sv_rc_t(closure_group_t) group;
    value_arr upvalues;
} call_frame_t;

sv_vec_def(call_frame_t);

typedef struct vm_t {
    sv_vec_t(call_frame_t) call_frames;
    int64_t max_call_frames;

    hashmap_t globals_names_to_index;

    fn_t fn;

    value_arr globals;
    value_arr locals;
    value_arr stack;

    vm_ctx_t ctx;
} vm_t;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQUALS,
    OP_CALL,
    OP_TAIL_CALL,
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
    OP_RECORD_GET_OR_UNDEF,
    OP_HASHMAP_GET_OR_UNDEF,
    OP_NOT,
    OP_DUP,
    OP_IS_STR,
    OP_IS_NUMBER,
    OP_IS_BOOL,
    OP_IS_NIL,
    OP_IS_LIST,
    OP_IS_NIL_LIST,
    OP_IS_CONS,
    OP_IS_TUPLE,
    OP_IS_RECORD,
    OP_IS_RECORD_ANY,
    OP_IS_HASHMAP,
    OP_IS_HASHMAP_ANY,
    OP_EXTENDED_ARG,
    OP_HAS_FIELD,
    OP_HAS_KEY,
    OP_LIST_UNCONS,
    OP_ASSERT_MATCH,
    OP_NO_MATCH,
    OP_SWAP,
    OP_RECORD_UPDATE,
    OP_HASHMAP_UPDATE,
    OP_LIST_PREPEND,
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
    VM_ERR_STACK_OVERFLOW,
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

/**
 * Creates a vm that owns and runs `fn`.
 */
vm_t vm_init(fn_t, int64_t max_call_frames, hashmap_t globals_names_to_index);
void vm_deinit(vm_t*, const sv_allocator_t*);

/**
 * Interprets the VM bytecode.
 */
sv_opt_t(error_t) vm_run(vm_t*);

/**
 * Frees the payload of an error returned by vm_run.
 */
void vm_err_deinit(error_t*, const sv_allocator_t*);

#endif
