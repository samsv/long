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
sv_vec_def(vm_t);

typedef sv_vec_t(value_t) value_arr;

typedef struct {
    sv_vec_t(uint8_t) bytecode;
    sv_vec_t(int64_t) lines;
    sv_vec_t(value_t) constants;
    sv_vec_t(vm_t) functions;
} chunk_t;

typedef struct vm_t {
    sv_str_t name;
    chunk_t chunk;
    value_arr globals;
    value_arr locals;
    value_arr stack;
    value_arr upvalues;
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
} vm_instructions;

typedef enum {
    VM_ERR_OOM,
    VM_ERR_UNDEFINED_VARIABLE,
    VM_ERR_OP_UNSUPPORTED_ARGS,
    VM_ERR_NOT_IMPLEMENTED,
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

chunk_t chunk_init(void);
void chunk_deinit(chunk_t*, const sv_allocator_t*);
vm_t vm_init(sv_str_t);
void vm_deinit(vm_t*, const sv_allocator_t*);
sv_opt_t(error_t) vm_run(vm_t*, const sv_allocator_t*);

#endif
