#include "debug.h"

#include "common.h"
#include "value.h"
#include "vm.h"
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
        case OP_TAIL_CALL: return "tail_call";
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
        case OP_LENGTH: return "length";
        case OP_RECORD_GET: return "record_get";
        case OP_RECORD_GET_OR_UNDEF: return "record_get?";
        case OP_HASHMAP_GET_OR_UNDEF: return "hashmap_get?";
        case OP_NOT: return "not";
        case OP_DUP: return "dup";
        case OP_IS_STR: return "is_str";
        case OP_IS_NUMBER: return "is_number";
        case OP_IS_BOOL: return "is_bool";
        case OP_IS_NIL: return "is_nil";
        case OP_IS_LIST: return "is_list";
        case OP_IS_NIL_LIST: return "is_nil_list";
        case OP_IS_CONS: return "is_cons";
        case OP_IS_TUPLE: return "is_tuple";
        case OP_IS_RECORD: return "is_record";
        case OP_IS_RECORD_ANY: return "is_record_any";
        case OP_IS_HASHMAP: return "is_hashmap";
        case OP_HAS_FIELD: return "has_field";
        case OP_HAS_KEY: return "has_key";
        case OP_LIST_UNCONS: return "list_uncons";
        case OP_ASSERT_MATCH: return "assert_match";
        case OP_NO_MATCH: return "no_match";
        case OP_SWAP: return "swap";
        case OP_IS_HASHMAP_ANY: return "is_hashmap_any";
        case OP_EXTENDED_ARG: return "extended_arg";
        case OP_RECORD_UPDATE: return "record_update";
        case OP_HASHMAP_UPDATE: return "hashmap_update";
        case OP_LIST_PREPEND: return "list_prepend";
        case OP_RETURN: return "return";
    }
    return "unknown";
}

static uint32_t read_operand(const uint8_t* p, uint8_t width)
{
    uint32_t arg = 0;
    for (uint8_t k = 0; k < width; k++)
        arg |= (uint32_t)p[k] << (8 * k);
    return arg;
}

static bool truncated(int64_t i, vm_instructions op, int64_t left, int64_t need)
{
    if (left >= need)
        return false;
    printf("%" PRId64 " [ %s ] truncated\n", i, op_name(op));
    return true;
}

void print_chunk(vm_t v)
{
    printf("========= INSTRUCTIONS =========\n");
    const uint8_t* code = v.chunk.bytecode.arr;
    int64_t size = v.chunk.bytecode.size;
    uint8_t width = 1;
    int64_t i = 0;
    while (i < size) {
        vm_instructions op = code[i];
        const uint8_t* p = code + i + 1;
        int64_t left = size - i - 1;
        switch (op) {
            case OP_EXTENDED_ARG:
                if (truncated(i, op, left, 1))
                    return;
                width = p[0];
                printf("%" PRId64 " [ %s ] width %u\n", i, op_name(op), width);
                i += 2;
                continue;
            case OP_JUMP:
            case OP_JUMP_BACK:
            case OP_JUMP_IF_FALSE: {
                if (truncated(i, op, left, 2))
                    return;
                uint16_t offset = (uint16_t)(p[0] | p[1] << 8);
                printf("%" PRId64 " [ %s ] offset %u\n", i, op_name(op), offset);
                i += 3;
                break;
            }
            case OP_RECORD:
            case OP_TUPLE:
            case OP_LIST:
            case OP_RECORD_UPDATE:
            case OP_LIST_PREPEND:
                if (truncated(i, op, left, width))
                    return;
                printf("%" PRId64 " [ %s ] size %" PRIu32 "\n", i, op_name(op), read_operand(p, width));
                i += 1 + width;
                break;
            case OP_HASHMAP:
            case OP_HASHMAP_UPDATE:
                if (truncated(i, op, left, width))
                    return;
                printf("%" PRId64 " [ %s ] pairs %" PRIu32 "\n", i, op_name(op), read_operand(p, width));
                i += 1 + width;
                break;
            case OP_LOAD_CLOSURE:
                if (truncated(i, op, left, width + 1))
                    return;
                printf("%" PRId64 " [ %s ] fn_index %" PRIu32 " n_closures %u\n",
                    i, op_name(op), read_operand(p, width), p[width]);
                i += 2 + width;
                break;
            case OP_CREATE_GROUP:
                if (truncated(i, op, left, width + 2))
                    return;
                printf("%" PRId64 " [ %s ] first %" PRIu32 " members %u upvalues %u\n",
                    i, op_name(op), read_operand(p, width), p[width], p[width + 1]);
                i += 3 + width;
                break;
            case OP_POP_LOCAL:
            case OP_LOAD_CONSTANT:
            case OP_GET_GLOBAL:
            case OP_GET_LOCAL:
            case OP_GET_UPVALUE:
            case OP_RECORD_GET:
            case OP_GET_MEMBER:
            case OP_RECORD_GET_OR_UNDEF:
            case OP_IS_TUPLE:
            case OP_IS_RECORD:
            case OP_HAS_FIELD:
                if (truncated(i, op, left, width))
                    return;
                printf("%" PRId64 " [ %s ] index %" PRIu32 "\n", i, op_name(op), read_operand(p, width));
                i += 1 + width;
                break;
            case OP_TAIL_CALL:
            case OP_CALL:
                if (truncated(i, op, left, width))
                    return;
                printf("%" PRId64 " [ %s ] args %" PRIu32 "\n", i, op_name(op), read_operand(p, width));
                i += 1 + width;
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
            case OP_LENGTH:
            case OP_NOT:
            case OP_DUP:
            case OP_IS_STR:
            case OP_IS_NUMBER:
            case OP_IS_BOOL:
            case OP_IS_NIL:
            case OP_IS_LIST:
            case OP_IS_NIL_LIST:
            case OP_IS_CONS:
            case OP_IS_RECORD_ANY:
            case OP_IS_HASHMAP:
            case OP_IS_HASHMAP_ANY:
            case OP_HASHMAP_GET_OR_UNDEF:
            case OP_HAS_KEY:
            case OP_LIST_UNCONS:
            case OP_ASSERT_MATCH:
            case OP_NO_MATCH:
            case OP_SWAP:
            case OP_RETURN:
            default:
                printf("%" PRId64 " [ %s ]\n", i, op_name(op));
                i += 1;
                break;
        }
        width = 1;
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
}
