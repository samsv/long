#include "debug.h"

#include "common.h"
#include "value.h"
#include <inttypes.h>
#include <stdio.h>

static void print_arr(sv_vec_t(value_t) vec, const char* name, vm_t vm)
{
    printf("========= %s =========\n", name);
    printf("[");
    for (int64_t i = 0; i < vec.size; i++) {
        sv_str_t s = value_to_str(vec.arr[i], &vm.ctx);
        printf("%.*s", (int)s.size, s.chars);
        sv_str_deinit(&s, vm.ctx.alloc);
        if (i < vec.size - 1)
            printf(", ");
    }
    printf("]\n");
}

static const char* op_name(vm_instructions op)
{
    switch (op) {
        case OP_ADD: return "add";
        case OP_SUB: return "sub";
        case OP_MUL: return "mul";
        case OP_DIV: return "div";
        case OP_EQUALS: return "equals";
        case OP_NOT_EQUALS: return "not_equals";
        case OP_GREATER: return "greater";
        case OP_GREATER_EQUAL: return "greater_equal";
        case OP_LESS: return "less";
        case OP_LESS_EQUAL: return "less_equal";
        case OP_CALL: return "call";
        case OP_LOAD_CLOSURE: return "load_closure";
        case OP_CREATE_GROUP: return "create_group";
        case OP_GET_MEMBER: return "get_member";
        case OP_SET_GLOBAL: return "set_global";
        case OP_GET_GLOBAL: return "get_global";
        case OP_SET_LOCAL: return "set_local";
        case OP_GET_LOCAL: return "get_local";
        case OP_GET_UPVALUE: return "get_upvalue";
        case OP_POP_LOCAL: return "pop_local";
        case OP_ITER_CREATE: return "iter_create";
        case OP_ITER_NEXT: return "iter_next";
        case OP_LOAD_CONSTANT: return "load_constant";
        case OP_NEGATE: return "negate";
        case OP_POP: return "pop";
        case OP_JUMP: return "jump";
        case OP_JUMP_BACK: return "jump_back";
        case OP_JUMP_IF_FALSE: return "jump_if_false";
        case OP_LIST: return "list";
        case OP_HASHMAP: return "hashmap";
        case OP_RECORD: return "record";
        case OP_TUPLE: return "tuple";
        case OP_INDEX: return "index";
        case OP_RECORD_GET: return "record_get";
        case OP_NOT: return "not";
        case OP_DUP: return "dup";
        case OP_RETURN: return "return";
    }
    return "unknown";
}

void print_chunk(vm_t v)
{
    printf("========= INSTRUCTIONS =========\n");
    const uint8_t* code = v.chunk.bytecode.arr;
    int64_t i = 0;
    while (i < v.chunk.bytecode.size) {
        vm_instructions op = code[i];
        switch (op) {
            case OP_JUMP:
            case OP_JUMP_BACK:
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = (uint16_t)(code[i + 1] | code[i + 2] << 8);
                printf("%" PRId64 " [ %s ] offset %u\n", i, op_name(op), offset);
                i += 3;
                break;
            }
            case OP_RECORD:
            case OP_TUPLE:
            case OP_LIST:
                printf("%" PRId64 " [ %s ] size %u\n", i, op_name(op), code[i + 1]);
                i += 2;
                break;
            case OP_HASHMAP:
                printf("%" PRId64 " [ %s ] pairs %u\n", i, op_name(op), code[i + 1]);
                i += 2;
                break;
            case OP_LOAD_CLOSURE:
                printf("%" PRId64 " [ %s ] fn_index %u n_closures %u\n", i, op_name(op), code[i + 1], code[i + 2]);
                i += 3;
                break;
            case OP_CREATE_GROUP:
                printf("%" PRId64 " [ %s ] first %u members %u upvalues %u\n",
                    i, op_name(op), code[i + 1], code[i + 2], code[i + 3]);
                i += 4;
                break;
            case OP_POP_LOCAL:
            case OP_LOAD_CONSTANT:
            case OP_GET_GLOBAL:
            case OP_GET_LOCAL:
            case OP_GET_UPVALUE:
            case OP_RECORD_GET:
            case OP_GET_MEMBER:
                printf("%" PRId64 " [ %s ] index %u\n", i, op_name(op), code[i + 1]);
                i += 2;
                break;
            case OP_CALL:
                printf("%" PRId64 " [ %s ] args %u\n", i, op_name(op), code[i + 1]);
                i += 2;
                break;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_EQUALS:
            case OP_NOT_EQUALS:
            case OP_GREATER:
            case OP_GREATER_EQUAL:
            case OP_LESS:
            case OP_LESS_EQUAL:
            case OP_SET_GLOBAL:
            case OP_SET_LOCAL:
            case OP_ITER_CREATE:
            case OP_ITER_NEXT:
            case OP_NEGATE:
            case OP_POP:
            case OP_INDEX:
            case OP_NOT:
            case OP_DUP:
            case OP_RETURN:
            default:
                printf("%" PRId64 " [ %s ]\n", i, op_name(op));
                i += 1;
                break;
        }
    }
}

void print_globals(vm_t v)
{
    print_arr(v.globals, "GLOBALS", v);
}

void print_locals(vm_t v)
{
    print_arr(v.locals, "LOCALS", v);
}

void print_upvalues(vm_t v)
{
    print_arr(v.upvalues, "UPVALUES", v);
}

void print_stack(vm_t v)
{
    print_arr(v.stack, "STACK", v);
}

void print_vm(vm_t v)
{
    print_chunk(v);
    print_globals(v);
    print_locals(v);
    print_stack(v);
    print_upvalues(v);
}
