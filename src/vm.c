#include "vm.h"

static void arr_remove(value_arr* arr, const sv_allocator_t* a)
{
    value_t v = sv_vec_pop(*arr);
    value_free(&v, a);
}

static void arr_remove_n(value_arr* arr, int64_t n, const sv_allocator_t* a)
{
    for (int64_t i = arr->size - n; i < arr->size; i++)
        value_free(&arr->arr[i], a);
    arr->size -= n;
}

static void arr_deinit(value_arr* arr, const sv_allocator_t* a)
{
    sv_vec_foreach(value_t, v, arr)
        value_free(&v, a);
    sv_vec_deinit(arr, a);
}

static bool value_is_truthy(value_t v)
{
    switch (v.kind) {
        case VALUE_NIL: return false;
        case VALUE_BOOL: return v.boolean;
        case VALUE_NUMBER:
        case VALUE_OBJ: return true;
    }
    return true;
}

static int64_t vm_get_offset(const vm_t* vm, int64_t i)
{
    int64_t low = vm->chunk.bytecode.arr[i];
    int64_t high = vm->chunk.bytecode.arr[i + 1];
    return low + (high << 8);
}

chunk_t chunk_init(void)
{
    return (chunk_t){
        .bytecode = sv_vec_init(uint8_t),
        .lines = sv_vec_init(int64_t),
        .constants = sv_vec_init(value_t),
        .functions = sv_vec_init(vm_t),
    };
}

void chunk_deinit(chunk_t* c, const sv_allocator_t* a)
{
    arr_deinit(&c->constants, a);
    sv_vec_foreach(vm_t, f, &c->functions)
        vm_deinit(&f, a);
    sv_vec_deinit(&c->functions, a);
    sv_vec_deinit(&c->bytecode, a);
    sv_vec_deinit(&c->lines, a);
}

vm_t vm_init(sv_str_t name)
{
    return (vm_t){
        .name = name,
        .chunk = chunk_init(),
        .globals = sv_vec_init(value_t),
        .locals = sv_vec_init(value_t),
        .stack = sv_vec_init(value_t),
        .upvalues = sv_vec_init(value_t),
        .ip = 0,
    };
}

void vm_deinit(vm_t* vm, const sv_allocator_t* a)
{
    arr_deinit(&vm->stack, a);
    arr_deinit(&vm->locals, a);
    arr_deinit(&vm->globals, a);
    chunk_deinit(&vm->chunk, a);
}

sv_opt_t(error_t) vm_run(vm_t* vm, const sv_allocator_t* a)
{
#define TRY_PUSH(arr, v)                                                                                      \
    sv_vec_push(&(arr), v, &success, a);                                                                      \
    if (success == 0) {                                                                                       \
        err = (error_t){ .error_code = VM_ERR_OOM,                                                            \
                         .msg = sv_str_init("OOM when appending to vector"), };                               \
        goto error;                                                                                           \
    }

#define TRY_PUSH_STACK(v) TRY_PUSH(vm->stack, v)

#define SET(arr) {                                                                                            \
    value_t v = sv_vec_pop(vm->stack);                                                                        \
    TRY_PUSH(arr, v);                                                                                         \
    break; }

#define GET(src) {                                                                                            \
    uint8_t i = vm->chunk.bytecode.arr[vm->ip++];                                                             \
    TRY_PUSH_STACK(value_borrow(src.arr[i]));                                                                 \
    break; }

#define MATH_OP(op) {                                                                                         \
    value_t v2 = sv_vec_pop(vm->stack);                                                                       \
    value_t v1 = sv_vec_pop(vm->stack);                                                                       \
    if (v1.kind != VALUE_NUMBER || v2.kind != VALUE_NUMBER) {                                                 \
        op_err_payload = (vm_op_err){                                                                         \
            .line = vm->chunk.lines.arr[vm->ip-1],                                                            \
            .ops = { v1, v2 },                                                                                \
            .ops_len = 2 };                                                                                   \
        err = (error_t) { .error_code = VM_ERR_OP_UNSUPPORTED_ARGS,                                           \
                          .payload = &op_err_payload,                                                         \
                          .msg = sv_str_init("Unsupported args for " #op) };                                  \
        value_free(&v1, a);                                                                                   \
        value_free(&v2, a);                                                                                   \
        goto error;                                                                                           \
    }                                                                                                         \
    value_t res = {.kind = VALUE_NUMBER, .number = v1.number op v2.number};                                   \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

#define EQUALS() {                                                                                            \
    value_t v2 = sv_vec_pop(vm->stack);                                                                       \
    value_t v1 = sv_vec_pop(vm->stack);                                                                       \
    value_t res = {.kind = VALUE_BOOL, .boolean = value_eql(v1, v2)};                                         \
    value_free(&v1, a);                                                                                       \
    value_free(&v2, a);                                                                                       \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

    static vm_op_err op_err_payload = {0};
    static vm_instruction_err instruction_err_payload = {0};
    error_t err;
    int success = 0;
    while (vm->ip < vm->chunk.bytecode.size) switch (vm->chunk.bytecode.arr[vm->ip++]) {
        case OP_SET_LOCAL: SET(vm->locals)
        case OP_SET_GLOBAL: SET(vm->globals)
        case OP_GET_LOCAL: GET(vm->locals)
        case OP_GET_UPVALUE: GET(vm->upvalues)
        case OP_GET_GLOBAL: GET(vm->globals)
        case OP_LOAD_CONSTANT: GET(vm->chunk.constants)
        case OP_SUB: MATH_OP(-)
        case OP_ADD: MATH_OP(+)
        case OP_MUL: MATH_OP(*)
        case OP_DIV: MATH_OP(/)
        case OP_EQUALS: EQUALS()
        case OP_JUMP: vm->ip += vm_get_offset(vm, vm->ip); break;
        case OP_JUMP_BACK: vm->ip -= vm_get_offset(vm, vm->ip); break;
        case OP_JUMP_IF_FALSE: {
            value_t v = sv_vec_pop(vm->stack);
            vm->ip = value_is_truthy(v) ? vm->ip + 2 : vm->ip + vm_get_offset(vm, vm->ip);
            value_free(&v, a);
            break;
        }
        case OP_POP: arr_remove(&vm->stack, a); break;
        case OP_POP_LOCAL: arr_remove_n(&vm->locals, vm->chunk.bytecode.arr[vm->ip++], a); break;
        default: {
            instruction_err_payload = (vm_instruction_err){
                .line = vm->chunk.lines.arr[vm->ip - 1],
                .instruction = vm->chunk.bytecode.arr[vm->ip - 1],
            };
            err = (error_t){ .error_code = VM_ERR_NOT_IMPLEMENTED,
                             .payload = &instruction_err_payload,
                             .msg = sv_str_init("instruction not implemented") };
            goto error;
        }
    }

    return sv_opt_none_t(error_t);

error:
    return sv_opt_some_t(error_t, err);

#undef SET
#undef GET
#undef MATH_OP
#undef EQUALS
#undef TRY_PUSH
#undef TRY_PUSH_STACK
}
