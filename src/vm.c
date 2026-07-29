#include "vm.h"
#include "obj/list.h"
#include "obj/map.h"

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

static error_t vm_oom_err(const char* msg)
{
    return (error_t){ .error_code = VM_ERR_OOM, .msg = sv_str_init(msg) };
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
        .arity = 0,
        .chunk = chunk_init(),
        .globals = sv_vec_init(value_t),
        .locals = sv_vec_init(value_t),
        .stack = sv_vec_init(value_t),
        .upvalues = sv_vec_init(value_t),
        .group = {0},
        .ip = 0,
    };
}

static void vm_fn_deinit(vm_t* vm, const sv_allocator_t* a)
{
    arr_deinit(&vm->stack, a);
    arr_deinit(&vm->locals, a);
}

void vm_deinit(vm_t* vm, const sv_allocator_t* a)
{
    arr_deinit(&vm->globals, a);
    vm_fn_deinit(vm, a);
    chunk_deinit(&vm->chunk, a);
}

void vm_err_deinit(error_t* err, const sv_allocator_t* a)
{
    sv_free(a, err->payload);
    err->payload = NULL;
}

sv_opt_t(error_t) vm_run(vm_t* vm, const sv_allocator_t* a)
{
#define TRY_OR(cond, cleanup, err_msg) {                                                                      \
    if (!(cond)) {                                                                                            \
        cleanup;                                                                                              \
        err = vm_oom_err(err_msg);                                                                            \
        goto error;                                                                                           \
    } }

#define TRY_NOT_NULL(v, err_msg) TRY_OR((v) != NULL, (void)0, err_msg)

#define TRY_PUSH(arr, v)                                                                                      \
    sv_vec_push(&(arr), v, &success, a);                                                                      \
    TRY_OR(success, (void)0, "OOM when appending to vector")

#define TRY_PUSH_STACK(v) TRY_PUSH(vm->stack, v)

#define TRY_PUSH_OWNED(v)                                                                                     \
    sv_vec_push(&vm->stack, v, &success, a);                                                                  \
    TRY_OR(success, value_free(&v, a), "OOM when appending to vector")

#define UNSUPPORTED_1(v, err_msg) {                                                                           \
    vm_op_err* op_err_payload = sv_malloc(a, sizeof(vm_op_err));                                              \
    if (op_err_payload != NULL)                                                                               \
        *op_err_payload = (vm_op_err){                                                                        \
            .vm_err = { .line = vm->chunk.lines.arr[vm->ip-1] },                                              \
            .ops = { v },                                                                                     \
            .ops_len = 1 };                                                                                   \
    err = (error_t) { .error_code = VM_ERR_OP_UNSUPPORTED_ARGS,                                               \
                      .payload = op_err_payload,                                                              \
                      .msg = sv_str_init(err_msg) };                                                          \
    value_free(&v, a);                                                                                        \
    goto error; }

#define SET(arr) {                                                                                            \
    value_t v = sv_vec_pop(vm->stack);                                                                        \
    TRY_PUSH(arr, v);                                                                                         \
    break; }

#define GET(src) {                                                                                            \
    uint8_t i = vm->chunk.bytecode.arr[vm->ip++];                                                             \
    TRY_PUSH_STACK(value_borrow(src.arr[i]));                                                                 \
    break; }

#define OP_ERR_2(code, v1, v2, err_msg) {                                                                     \
    vm_op_err* op_err_payload = sv_malloc(a, sizeof(vm_op_err));                                              \
    if (op_err_payload != NULL)                                                                               \
        *op_err_payload = (vm_op_err){                                                                        \
            .vm_err = { .line = vm->chunk.lines.arr[vm->ip-1] },                                              \
            .ops = { v1, v2 },                                                                                \
            .ops_len = 2 };                                                                                   \
    err = (error_t) { .error_code = code,                                                                     \
                      .payload = op_err_payload,                                                              \
                      .msg = sv_str_init(err_msg) };                                                          \
    value_free(&v1, a);                                                                                       \
    value_free(&v2, a);                                                                                       \
    goto error; }

#define UNSUPPORTED_2(v1, v2, err_msg) OP_ERR_2(VM_ERR_OP_UNSUPPORTED_ARGS, v1, v2, err_msg)

#define NUM_BIN_OP(op, res_kind, res_field) {                                                                 \
    value_t v2 = sv_vec_pop(vm->stack);                                                                       \
    value_t v1 = sv_vec_pop(vm->stack);                                                                       \
    if (v1.kind != VALUE_NUMBER || v2.kind != VALUE_NUMBER)                                                   \
        UNSUPPORTED_2(v1, v2, "Unsupported args for " #op)                                                    \
    value_t res = {.kind = res_kind, .res_field = v1.number op v2.number};                                    \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

#define MATH_OP(op) NUM_BIN_OP(op, VALUE_NUMBER, number)
#define CMP_OP(op) NUM_BIN_OP(op, VALUE_BOOL, boolean)

#define EQUALS(want) {                                                                                        \
    value_t v2 = sv_vec_pop(vm->stack);                                                                       \
    value_t v1 = sv_vec_pop(vm->stack);                                                                       \
    value_t res = {.kind = VALUE_BOOL, .boolean = value_eql(v1, v2) == want};                                 \
    value_free(&v1, a);                                                                                       \
    value_free(&v2, a);                                                                                       \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

    error_t err;
    int success = 0;
    while (1) switch (vm->chunk.bytecode.arr[vm->ip++]) {
        case OP_SET_LOCAL: SET(vm->locals)
        case OP_SET_GLOBAL: SET(vm->globals)
        case OP_GET_LOCAL: GET(vm->locals)
        case OP_GET_UPVALUE: GET(vm->upvalues)
        case OP_GET_GLOBAL: GET(vm->globals)
        case OP_LOAD_CONSTANT: GET(vm->chunk.constants)
        case OP_SUB: MATH_OP(-)
        case OP_MUL: MATH_OP(*)
        case OP_DIV: MATH_OP(/)
        case OP_EQUALS: EQUALS(true)
        case OP_NOT_EQUALS: EQUALS(false)
        case OP_GREATER: CMP_OP(>)
        case OP_GREATER_EQUAL: CMP_OP(>=)
        case OP_LESS: CMP_OP(<)
        case OP_LESS_EQUAL: CMP_OP(<=)
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
        case OP_NEGATE: {
            value_t v = sv_vec_pop(vm->stack);
            if (v.kind != VALUE_NUMBER)
                UNSUPPORTED_1(v, "Unsupported args for negate")
            value_t res = {.kind = VALUE_NUMBER, .number = -v.number};
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_LIST: {
            uint8_t n = vm->chunk.bytecode.arr[vm->ip++];
            value_t list = value_init_list(&vm->stack.arr[vm->stack.size - n], n, a);
            TRY_NOT_NULL(list.obj.cell, "OOM when creating list");
            arr_remove_n(&vm->stack, n, a);
            TRY_PUSH_OWNED(list);
            break;
        }
        case OP_HASHMAP: {
            uint8_t n = vm->chunk.bytecode.arr[vm->ip++];
            value_t map = value_init_map(&vm->stack.arr[vm->stack.size - 2 * n], n, a);
            TRY_NOT_NULL(map.obj.cell, "OOM when creating map");
            arr_remove_n(&vm->stack, 2 * n, a);
            TRY_PUSH_OWNED(map);
            break;
        }
        case OP_ADD: {
            value_t v2 = sv_vec_pop(vm->stack);
            value_t v1 = sv_vec_pop(vm->stack);
            if (v1.kind == VALUE_NUMBER && v2.kind == VALUE_NUMBER) {
                value_t res = {.kind = VALUE_NUMBER, .number = v1.number + v2.number};
                TRY_PUSH_STACK(res);
                break;
            }
            if (v1.kind == VALUE_OBJ && v2.kind == VALUE_OBJ
                && v1.obj.cell->value.kind == OBJ_STR && v2.obj.cell->value.kind == OBJ_STR
            ) {
                sv_str_t s = sv_str_add(v1.obj.cell->value.str, v2.obj.cell->value.str, a);
                value_free(&v1, a);
                value_free(&v2, a);
                TRY_OR(s.size >= 0, (void)0, "OOM when concatenating strings");
                value_t res = value_init_str_own(s, a);
                TRY_NOT_NULL(res.obj.cell, "OOM when creating string");
                TRY_PUSH_OWNED(res);
                break;
            }
            UNSUPPORTED_2(v1, v2, "Unsupported args for +")
        }
        case OP_NOT: {
            value_t v = sv_vec_pop(vm->stack);
            value_t res = {.kind = VALUE_BOOL, .boolean = !value_is_truthy(v)};
            value_free(&v, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_DUP: {
            value_t top = value_borrow(sv_vec_last(vm->stack));
            TRY_PUSH_STACK(top);
            break;
        }
        case OP_INDEX: {
            value_t key = sv_vec_pop(vm->stack);
            value_t container = sv_vec_pop(vm->stack);
            if (container.kind != VALUE_OBJ
                || (container.obj.cell->value.kind != OBJ_MAP
                    && container.obj.cell->value.kind != OBJ_LIST))
                UNSUPPORTED_2(container, key, "Type is not indexable")

            sv_opt_t(value_t) res;
            if (container.obj.cell->value.kind == OBJ_MAP) {
                res = map_get(container.obj.cell->value.map, key);
            } else {
                if (key.kind != VALUE_NUMBER || key.number != (double)(int64_t)key.number)
                    UNSUPPORTED_2(container, key, "List index is not an integer")
                res = ll_get(container.obj.cell->value.list, (int64_t)key.number);
            }
            if (!res.is_some)
                OP_ERR_2(VM_ERR_KEY_NOT_FOUND, container, key, "Key not found")

            value_t out = value_borrow(res.value);
            value_free(&container, a);
            value_free(&key, a);
            TRY_PUSH_OWNED(out);
            break;
        }
        case OP_ITER_CREATE: {
            value_t v = sv_vec_pop(vm->stack);
            if (!IS_LIST(v) && !IS_STR(v))
                UNSUPPORTED_1(v, "Type is not iterable")
            value_t iter = value_init_iter(v, a);
            value_free(&v, a);
            TRY_NOT_NULL(iter.obj.cell, "OOM when creating iterator");
            TRY_PUSH_OWNED(iter);
            break;
        }
        case OP_ITER_NEXT: {
            value_t v = sv_vec_pop(vm->stack);
            if (!IS_ITER(v))
                UNSUPPORTED_1(v, "Type is not an iterator")
            value_t res = iter_next(&AS_ITER(v), a);
            value_free(&v, a);
            TRY_OR(!(res.kind == VALUE_OBJ && res.obj.cell == NULL), (void)0,
                   "OOM when advancing iterator");
            TRY_PUSH_OWNED(res);
            break;
        }
        case OP_LOAD_CLOSURE: {
            uint8_t i = vm->chunk.bytecode.arr[vm->ip++];
            uint8_t n_cls = vm->chunk.bytecode.arr[vm->ip++];
            value_t cls = value_init_closure(
                &vm->chunk.functions.arr[i],
                &vm->stack.arr[vm->stack.size - n_cls],
                n_cls,
                a);
            TRY_NOT_NULL(cls.obj.cell, "OOM when creating closure");
            arr_remove_n(&vm->stack, n_cls, a);
            TRY_PUSH_OWNED(cls);
            break;
        }
        case OP_CREATE_GROUP: {
            uint8_t first = vm->chunk.bytecode.arr[vm->ip++];
            uint8_t n_members = vm->chunk.bytecode.arr[vm->ip++];
            uint8_t n_upvalues = vm->chunk.bytecode.arr[vm->ip++];

            sv_vec_grow_cap(&vm->stack, vm->stack.size + n_members, &success, a);
            TRY_OR(success, (void)0, "OOM when creating closure group")

            closure_group_t g = {
                .members = {
                    .arr = &vm->chunk.functions.arr[first],
                    .size = n_members,
                    .capacity = n_members,
                    .element_size = sizeof(vm_t),
                },
                .upvalues = sv_vec_init(value_t),
            };
            if (n_upvalues > 0) {
                g.upvalues = (sv_vec_t(value_t))sv_vec_init_capacity(value_t, n_upvalues, a);
                TRY_NOT_NULL(g.upvalues.arr, "OOM when creating closure group");
                g.upvalues.size = n_upvalues;
                for (int64_t i = 0; i < n_upvalues; i++)
                    g.upvalues.arr[i] = value_borrow(vm->stack.arr[vm->stack.size - n_upvalues + i]);
            }

            sv_rc_t(closure_group_t) group = sv_rc_init(closure_group_t, g, clsg_deinit, a);
            TRY_OR(group.cell != NULL, clsg_deinit(&g, a), "OOM when creating closure group")
            arr_remove_n(&vm->stack, n_upvalues, a);

            for (int64_t i = n_members - 1; i >= 0; i--) {
                value_t member = value_init_closure_member(sv_rc_borrow(group), i, a);
                TRY_OR(member.obj.cell != NULL, sv_rc_deinit(&group, a), "OOM when creating closure member")
                vm->stack.arr[vm->stack.size++] = member;
            }
            sv_rc_deinit(&group, a);
            break;
        }
        case OP_GET_MEMBER: {
            uint8_t i = vm->chunk.bytecode.arr[vm->ip++];
            if (vm->group.cell == NULL) {
                vm_instruction_err* p = sv_malloc(a, sizeof(vm_instruction_err));
                if (p != NULL)
                    *p = (vm_instruction_err){
                        .vm_err = { .line = vm->chunk.lines.arr[vm->ip - 1] },
                        .instruction = OP_GET_MEMBER,
                    };
                err = (error_t){ .error_code = VM_ERR_NO_GROUP,
                                 .payload = p,
                                 .msg = sv_str_init("no closure group in scope") };
                goto error;
            }
            value_t member = value_init_closure_member(sv_rc_borrow(vm->group), i, a);
            TRY_NOT_NULL(member.obj.cell, "OOM when creating closure member");
            TRY_PUSH_OWNED(member);
            break;
        }
        case OP_CALL: {
#define ERR_WRONG_ARITY(arity) do {                                                                               \
    vm_arity_err* p = sv_malloc(a, sizeof(vm_arity_err));                                                     \
    if (p != NULL)                                                                                            \
        *p = (vm_arity_err){                                                                                  \
            .vm_err = { .line = vm->chunk.lines.arr[vm->ip - 1] },                                            \
            .expected = arity,                                                                                \
            .got = arg_count,                                                                                 \
        };                                                                                                    \
    err = (error_t){ .error_code = VM_ERR_BAD_ARITY,                                                          \
        .payload = p,                                                                                         \
        .msg = sv_str_init("wrong number of arguments") };                                                    \
    sv_vec_deinit(&args, a);                                                                                  \
    value_free(&value, a);                                                                                    \
    goto error; } while (0)

            value_t value = sv_vec_pop(vm->stack);
            uint8_t arg_count = vm->chunk.bytecode.arr[vm->ip++];

            if (!IS_CLOSURE(value) && !IS_NATIVE(value) && !IS_CLOSURE_MEMBER(value))
                UNSUPPORTED_1(value, "Type is not callable")

            value_arr args = (value_arr)sv_vec_init_capacity(value_t, arg_count + 1, a);
            TRY_OR(args.arr != NULL, value_free(&value, a), "OOM when passing arguments")
            for (int64_t i = 0; i < arg_count; i++)
                args.arr[i] = vm->stack.arr[vm->stack.size - arg_count + i];
            args.size = arg_count;

            if (IS_NATIVE(value)) {
                native_fn_t fn = AS_NATIVE(value);
                if (fn.arity != arg_count)
                    ERR_WRONG_ARITY(fn.arity);

                value_t ret = fn.fn(args.arr, args.size, a);
                if (IS_ERR(ret)) {
                    err = AS_ERR(ret);
                    vm_err_t* vm_err = err.payload;
                    vm_err->line = vm->chunk.lines.arr[vm->ip-1];
                    sv_vec_deinit(&args, a);
                    value_free(&value, a);
                    goto error;
                }
                arr_deinit(&args, a);
                vm->stack.size -= arg_count;
                TRY_PUSH_STACK(ret);
                break;
            }

            vm_t fn_vm;
            if (IS_CLOSURE(value)) {
                closure_t* cls = &AS_CLOSURE(value);
                fn_vm = cls_get_vm(*cls);
                fn_vm.upvalues = cls->upvalues;
            } else {
                closure_member_t* member = &AS_CLOSURE_MEMBER(value);
                fn_vm = clsm_get_vm(*member);
                fn_vm.upvalues = member->group.cell->value.upvalues;
                fn_vm.group = member->group;
            }

            if (arg_count != fn_vm.arity)
                ERR_WRONG_ARITY(fn_vm.arity);

            fn_vm.globals = vm->globals;

            fn_vm.locals = args;
            fn_vm.locals.arr[arg_count] = value_borrow(value);
            fn_vm.locals.size++;

            vm->stack.size -= arg_count;

            sv_opt_t(error_t) fn_err = vm_run(&fn_vm, a);
            if (fn_err.is_some) {
                vm_fn_deinit(&fn_vm, a);
                value_free(&value, a);
                err = fn_err.value;
                goto error;
            }

            value_t res = sv_vec_pop(fn_vm.stack);
            vm_fn_deinit(&fn_vm, a);
            value_free(&value, a);
            TRY_PUSH_OWNED(res);
            break;
#undef ERR_WRONG_ARITY
        }
        case OP_RETURN:
            return sv_opt_none_t(error_t);
        default: {
            vm_instruction_err* instruction_err_payload = sv_malloc(a, sizeof(vm_instruction_err));
            if (instruction_err_payload != NULL)
                *instruction_err_payload = (vm_instruction_err){
                    .vm_err = { .line = vm->chunk.lines.arr[vm->ip - 1] },
                    .instruction = vm->chunk.bytecode.arr[vm->ip - 1],
                };
            err = (error_t){ .error_code = VM_ERR_NOT_IMPLEMENTED,
                             .payload = instruction_err_payload,
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
#undef CMP_OP
#undef NUM_BIN_OP
#undef OP_ERR_2
#undef UNSUPPORTED_2
#undef EQUALS
#undef TRY_OR
#undef TRY_PUSH
#undef TRY_PUSH_STACK
#undef TRY_PUSH_OWNED
#undef TRY_NOT_NULL
#undef UNSUPPORTED_1
}

vm_builder_t vmb_init(sv_str_t name)
{
    return (vm_builder_t){ .vm = vm_init(name) };
}

vm_t vmb_build(vm_builder_t* b)
{
    vm_t vm = b->vm;
    b->vm = (vm_t){0};
    return vm;
}

bool vmb_add_byte(vm_builder_t* b, uint8_t byte, int64_t line, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->vm.chunk.bytecode, byte, &success, a);
    if (!success)
        return false;
    sv_vec_push(&b->vm.chunk.lines, line, &success, a);
    return success;
}

bool vmb_add_bytes(vm_builder_t* b, uint8_t b1, uint8_t b2, int64_t line, const sv_allocator_t* a)
{
    return vmb_add_byte(b, b1, line, a) && vmb_add_byte(b, b2, line, a);
}

sv_opt_t(uint8_t) vmb_add_constant(vm_builder_t* b, value_t c, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->vm.chunk.constants, c, &success, a);
    if (!success)
        return sv_opt_none_t(uint8_t);

    uint8_t i = (uint8_t)(b->vm.chunk.constants.size - 1);
    if (!vmb_add_bytes(b, OP_LOAD_CONSTANT, i, 0, a))
        return sv_opt_none_t(uint8_t);
    return sv_opt_some_t(uint8_t, i);
}

sv_opt_t(uint8_t) vmb_add_closure(vm_builder_t* b, uint8_t cls_args_n, vm_t fn_vm, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->vm.chunk.functions, fn_vm, &success, a);
    if (!success)
        return sv_opt_none_t(uint8_t);

    uint8_t i = (uint8_t)(b->vm.chunk.functions.size - 1);
    if (!vmb_add_bytes(b, OP_LOAD_CLOSURE, i, 0, a) || !vmb_add_byte(b, cls_args_n, 0, a))
        return sv_opt_none_t(uint8_t);
    return sv_opt_some_t(uint8_t, i);
}

void vmb_patch_jump(vm_builder_t* b, int64_t index, uint16_t value)
{
    b->vm.chunk.bytecode.arr[index] = (uint8_t)value;
    b->vm.chunk.bytecode.arr[index + 1] = (uint8_t)(value >> 8);
}

sv_opt_t(int64_t) vmb_add_jump(vm_builder_t* b, int64_t line, const sv_allocator_t* a)
{
    if (!vmb_add_byte(b, OP_JUMP, line, a) || !vmb_add_bytes(b, 255, 255, line, a))
        return sv_opt_none_t(int64_t);
    return sv_opt_some_t(int64_t, b->vm.chunk.bytecode.size - 2);
}

bool vmb_add_jump_back(vm_builder_t* b, int64_t to, int64_t line, const sv_allocator_t* a)
{
    if (!vmb_add_byte(b, OP_JUMP_BACK, line, a))
        return false;
    uint16_t offset = (uint16_t)(b->vm.chunk.bytecode.size - to);
    return vmb_add_bytes(b, (uint8_t)offset, (uint8_t)(offset >> 8), line, a);
}

sv_opt_t(int64_t) vmb_add_jump_if_false(vm_builder_t* b, int64_t line, const sv_allocator_t* a)
{
    if (!vmb_add_byte(b, OP_JUMP_IF_FALSE, line, a) || !vmb_add_bytes(b, 255, 255, line, a))
        return sv_opt_none_t(int64_t);
    return sv_opt_some_t(int64_t, b->vm.chunk.bytecode.size - 2);
}

bool vmb_add_global(vm_builder_t* b, value_t v, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->vm.globals, v, &success, a);
    return success > 0;
}
