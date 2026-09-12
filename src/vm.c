#include "vm.h"
#include "obj/list.h"
#include "obj/map.h"
#include "std/logger.h"
#include "value.h"
#include "ctx.h"

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

static bool value_is_truthy(value_t v)
{
    switch (v.kind) {
        case VALUE_UNDEFINED:
        case VALUE_NIL: return false;
        case VALUE_BOOL: return v.boolean;
        case VALUE_NUMBER:
        case VALUE_OBJ: return true;
    }
    return true;
}

static error_t vm_oom_err(const char* msg)
{
    return (error_t){ .error_code = VM_ERR_OOM, .msg = sv_str_init(msg) };
}

vm_t vm_init(fn_t fn, int64_t max_call_frames, globals_t globals_names_to_index, const sv_allocator_t* a)
{
    return (vm_t){
        .call_frames = sv_vec_init(call_frame_t),
        .max_call_frames = max_call_frames,
        .globals_names_to_index = globals_names_to_index,
        .fn = fn,
        .globals = sv_vec_init(value_t),
        .locals = sv_vec_init(value_t),
        .stack = sv_vec_init(value_t),
        .ctx = { .alloc = a, .logger = sv_std_logger, .record_key_names = NULL, .record_names_sizes = 0 },
    };
}

void vm_deinit(vm_t* vm)
{
    const sv_allocator_t* a = vm->ctx.alloc;
    if (vm->ctx.record_key_names != NULL) {
        for (uint32_t i = 0; i < vm->ctx.record_names_sizes; i++)
            sv_free(a, (void*)vm->ctx.record_key_names[i]);
        sv_free(a, (void*)vm->ctx.record_key_names);
    }
    value_arr_deinit(&vm->globals, a);
    value_arr_deinit(&vm->stack, a);
    value_arr_deinit(&vm->locals, a);
    sv_vec_deinit(&vm->call_frames, a);
    thm_deinit(&vm->globals_names_to_index.name_indexes, a);
    map_deinit(&vm->ctx.record_fields, a);
    fn_deinit(&vm->fn, a);
}

static call_frame_t init_frame(fn_t* fn, int64_t locals_offset, int64_t stack_offset,
                               value_arr upvalues, sv_rc_t(closure_group_t) group)
{
    return (call_frame_t){
        .fn = fn,
        .ip = 0,
        .locals_offset = locals_offset,
        .stack_offset = stack_offset,
        .group = group,
        .upvalues = upvalues,
    };
}

sv_opt_t(uint32_t) vm_get_record_idx(vm_t vm, sv_str_t s)
{
    if (vm.ctx.record_fields.cell == NULL)
        return sv_opt_none_t(uint32_t);

    obj_t str_obj = { .kind = OBJ_STR, .str = s };
    sv_rc_cell_t(obj_t) cell = { .value = str_obj };
    sv_rc_t(obj_t) obj = { .cell =  &cell };
    value_t v = { .kind = VALUE_OBJ, .obj = obj };
    sv_opt_t(value_t) index_val = map_get(vm.ctx.record_fields, v);
    sv_opt_t(uint32_t) index = index_val.is_some ?
        sv_opt_some_t(uint32_t, (uint32_t)AS_NUMBER(index_val.value))
        : sv_opt_none_t(uint32_t);
    return index;
}

void vm_err_deinit(error_t* err, const sv_allocator_t* a)
{
    sv_free(a, err->payload);
    err->payload = NULL;
}

static sv_opt_t(error_t) vm_run_frame(vm_t* vm);

value_t vm_call(vm_t* vm, uint32_t index, value_t* args, uint8_t arg_count)
{
#define ERROR(_msg, code) (error_t) {.msg = sv_str_init(_msg), .error_code = code}
    const sv_allocator_t* a = vm->ctx.alloc;
    if (index >= vm->globals.size)
        return value_init_err(ERROR("Undefined global", VM_ERR_UNDEFINED_VARIABLE), a);

    value_t value = vm->globals.arr[index];
    if (!IS_CLOSURE(value) && !IS_NATIVE(value) && !IS_CLOSURE_MEMBER(value))
        return value_init_err(ERROR("Type is not callable", VM_ERR_WRONG_TYPE), a);

    if (IS_NATIVE(value)) {
        native_fn_t fn = AS_NATIVE(value);
        if (fn.arity != arg_count)
            return value_init_err(ERROR("Wrong number of arguments", VM_ERR_BAD_ARITY), a);
        return fn.fn(args, arg_count, &vm->ctx);
    }

    fn_t* fn;
    value_arr upvalues;
    sv_rc_t(closure_group_t) group = { 0 };
    if (IS_CLOSURE(value)) {
        closure_t* cls = &AS_CLOSURE(value);
        fn = cls_get_vm(*cls);
        upvalues = cls->upvalues;
    } else {
        closure_member_t* member = &AS_CLOSURE_MEMBER(value);
        fn = clsm_get_vm(*member);
        upvalues = member->group.cell->value.upvalues;
        group = member->group;
    }
    if (arg_count != fn->arity)
        return value_init_err(ERROR("Wrong number of arguments", VM_ERR_BAD_ARITY), a);

    int success;
    sv_vec_push(&vm->call_frames, init_frame(fn, 0, 0, upvalues, group), &success, a);
    if (!success)
        return value_init_err(ERROR("OOM when creating call frame", VM_ERR_OOM), a);

    if (arg_count > 0) {
        sv_vec_push_many(&vm->locals, args, arg_count, &success, a);
        if (!success)
            return value_init_err(ERROR("OOM when passing arguments", VM_ERR_OOM), a);
    }
    sv_vec_push(&vm->locals, value, &success, a);
    if (!success)
        return value_init_err(ERROR("OOM when passing arguments", VM_ERR_OOM), a);

    sv_opt_t(error_t) ret = vm_run_frame(vm);
    if (ret.is_some)
        return value_init_err(ret.value, a);

    return vm->stack.arr[vm->stack.size--];
}

value_t vm_call_name(vm_t* vm, value_t* args, uint8_t arg_count,
                     sv_str_t function_name, sv_str_t file_name)
{
    ctx_t ctx = { .logger = vm->ctx.logger, .alloc = *vm->ctx.alloc };
    sv_opt_t(uint32_t) index = globals_get(vm->globals_names_to_index, function_name, file_name, &ctx);
    if (!index.is_some)
        return value_init_err(ERROR("Undefined global", VM_ERR_UNDEFINED_VARIABLE), vm->ctx.alloc);
    return vm_call(vm, index.value, args, arg_count);
#undef ERROR
}

sv_opt_t(error_t) vm_run(vm_t* vm)
{
    const sv_allocator_t* a = vm->ctx.alloc;

    error_t err = {0};
    int success = 0;
    sv_vec_push(
        &vm->call_frames,
        init_frame(&vm->fn, 0, 0, sv_vec_init(value_t), (sv_rc_t(closure_group_t)){0}),
        &success,
        a
    );
    if (!success)
        return sv_opt_some_t(error_t, err);

    return vm_run_frame(vm);
}

static sv_opt_t(error_t) vm_run_frame(vm_t* vm)
{
#define TRY_NOT_NULL(v, err_msg) TRY_OR((v) != NULL, (void)0, err_msg)

#define LINE() (frame->fn->chunk.lines.arr[(ip - code) - 1])

#define TRY_OR(cond, cleanup, err_msg) {                                                                      \
    if (!(cond)) {                                                                                            \
        cleanup;                                                                                              \
        err = vm_oom_err(err_msg);                                                                            \
        goto error;                                                                                           \
    } }

#define TRY_PUSH(arr, v)                                                                                      \
    sv_vec_push(&(arr), v, &success, a);                                                                      \
    TRY_OR(success, (void)0, "OOM when appending to vector")


#define TRY_PUSH_STACK(v) TRY_PUSH(stack, v)

#define TRY_PUSH_OWNED(v)                                                                                     \
    sv_vec_push(&stack, v, &success, a);                                                                      \
    TRY_OR(success, value_free(&v, a), "OOM when appending to vector")

#define OP_ERR_1(code, v, err_msg) do {                                                                       \
    vm_op_err* op_err_payload = sv_malloc(a, sizeof(vm_op_err));                                              \
    if (op_err_payload != NULL)                                                                               \
        *op_err_payload = (vm_op_err){                                                                        \
            .vm_err = { .line = LINE() },                                                                     \
            .ops = { v },                                                                                     \
            .ops_len = 1 };                                                                                   \
    err = (error_t) { .error_code = code,                                                                     \
                      .payload = op_err_payload,                                                              \
                      .msg = sv_str_init(err_msg) };                                                          \
    value_free(&v, a);                                                                                        \
    goto error; } while (0)

#define UNSUPPORTED_1(v, err_msg) OP_ERR_1(VM_ERR_OP_UNSUPPORTED_ARGS, v, err_msg)

#define READ_BYTE() (*ip++)

#define OFFSET() ((int64_t)ip[0] | ((int64_t)ip[1] << 8))

#define LOAD_CODE() (code = frame->fn->chunk.bytecode.arr, ip = code + frame->ip)

#define READ_NARROW(name) do { name = READ_BYTE(); } while (0)

#define READ_ARG(name) do {                                                                                   \
    name = READ_BYTE();                                                                                       \
    for (int shift = 8; arg_bytes > 1; arg_bytes--, shift += 8)                                               \
        name |= (uint32_t)READ_BYTE() << shift;                                                               \
} while (0)

#define SET(arr) {                                                                                            \
    value_t v = sv_vec_pop(stack);                                                                            \
    TRY_PUSH(arr, v);                                                                                         \
    break; }

#define GET(src, base) {                                                                                      \
    uint32_t i = 0;                                                                                           \
    READ_ARG(i);                                                                                              \
    TRY_PUSH_STACK(value_borrow((src).arr[(base) + i]));                                                      \
    break; }

#define OP_ERR_2(code, v1, v2, err_msg) {                                                                     \
    vm_op_err* op_err_payload = sv_malloc(a, sizeof(vm_op_err));                                              \
    if (op_err_payload != NULL)                                                                               \
        *op_err_payload = (vm_op_err){                                                                        \
            .vm_err = { .line = LINE() },                                                                     \
            .ops = { v1, v2 },                                                                                \
            .ops_len = 2 };                                                                                   \
    err = (error_t) { .error_code = code,                                                                     \
                      .payload = op_err_payload,                                                              \
                      .msg = sv_str_init(err_msg) };                                                          \
    value_free(&v1, a);                                                                                       \
    value_free(&v2, a);                                                                                       \
    goto error; }

#define UNSUPPORTED_2(v1, v2, err_msg) OP_ERR_2(VM_ERR_OP_UNSUPPORTED_ARGS, v1, v2, err_msg)

#define IS_KIND(test) {                                                                                       \
    value_t v = sv_vec_pop(stack);                                                                            \
    value_t res = { .kind = VALUE_BOOL, .boolean = (test) };                                                  \
    value_free(&v, a);                                                                                        \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

#define IS_SIZED(is, as, field) {                                                                             \
    uint8_t want = READ_BYTE();                                                                               \
    value_t v = sv_vec_pop(stack);                                                                            \
    value_t res = { .kind = VALUE_BOOL, .boolean = is(v) && as(v).field == want };                            \
    value_free(&v, a);                                                                                        \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

#define NUM_BIN_OP(op, res_kind, res_field) {                                                                 \
    value_t v2 = sv_vec_pop(stack);                                                                           \
    value_t v1 = sv_vec_pop(stack);                                                                           \
    if (!IS_NUMBER(v1) || !IS_NUMBER(v2))                                                                     \
        UNSUPPORTED_2(v1, v2, "Unsupported args for " #op)                                                    \
    value_t res = {.kind = res_kind, .res_field = v1.number op v2.number};                                    \
    TRY_PUSH_STACK(res);                                                                                      \
    break; }

#define MATH_OP(op) NUM_BIN_OP(op, VALUE_NUMBER, number)
#define CMP_OP(op) NUM_BIN_OP(op, VALUE_BOOL, boolean)

#define EQUALS(want) {                                                                                        \
    value_t v2 = sv_vec_pop(stack);                                                                           \
    value_t v1 = sv_vec_pop(stack);                                                                           \
    value_t res = {.kind = VALUE_BOOL, .boolean = value_eql(v1, v2) == want};                                 \
    value_free(&v1, a);                                                                                       \
    value_free(&v2, a);                                                                                       \
    TRY_PUSH_STACK(res);                                                                                      \
break; }

    const sv_allocator_t* a = vm->ctx.alloc;

    error_t err;
    int success = 0;
    uint8_t arg_bytes = 1;

    value_arr stack = vm->stack;
    call_frame_t* frame = &sv_vec_last(vm->call_frames);

    const uint8_t* code = frame->fn->chunk.bytecode.arr;
    const uint8_t* ip = code + frame->ip;

    while (1) switch (READ_BYTE()) {
        case OP_SET_LOCAL: SET(vm->locals)
        case OP_SET_GLOBAL: SET(vm->globals)
        case OP_GET_LOCAL: GET(vm->locals, frame->locals_offset)
        case OP_GET_UPVALUE: GET(frame->upvalues, 0)
        case OP_GET_GLOBAL: GET(vm->globals, 0)
        case OP_LOAD_CONSTANT: GET(frame->fn->chunk.constants, 0)
        case OP_SUB: MATH_OP(-)
        case OP_MUL: MATH_OP(*)
        case OP_DIV: MATH_OP(/)
        case OP_EQUALS: EQUALS(true)
        case OP_NOT_EQUALS: EQUALS(false)
        case OP_GREATER: CMP_OP(>)
        case OP_GREATER_EQUAL: CMP_OP(>=)
        case OP_LESS: CMP_OP(<)
        case OP_LESS_EQUAL: CMP_OP(<=)
        case OP_JUMP: ip += OFFSET(); break;
        case OP_JUMP_BACK: ip -= OFFSET(); break;
        case OP_JUMP_IF_FALSE: {
            value_t v = sv_vec_pop(stack);
            ip = value_is_truthy(v) ? ip + 2 : ip + OFFSET();
            value_free(&v, a);
            break;
        }
        case OP_POP: arr_remove(&stack, a); break;
        case OP_POP_LOCAL: {
            uint32_t n = 0;
            READ_ARG(n);
            arr_remove_n(&vm->locals, n, a);
            break;
        }
        case OP_NEGATE: {
            value_t v = sv_vec_pop(stack);
            if (!IS_NUMBER(v))
                UNSUPPORTED_1(v, "Unsupported args for negate");
            value_t res = {.kind = VALUE_NUMBER, .number = -v.number};
            TRY_PUSH_STACK(res);
            break;
        }

#define VALUE_FROM_ARR(mult, init_fn, n_type, read) {                                                         \
            n_type n = 0;                                                                                     \
            read(n);                                                                                          \
            value_t arr = init_fn(&stack.arr[stack.size - (mult) * n], n, a);                                 \
            TRY_NOT_NULL(arr.obj.cell, "OOM when creating collection");                                       \
            arr_remove_n(&stack, (mult) * n, a);                                                              \
            TRY_PUSH_OWNED(arr);                                                                              \
            break; }

        case OP_LIST: VALUE_FROM_ARR(1, value_init_list, uint32_t, READ_ARG);
        case OP_HASHMAP: VALUE_FROM_ARR(2, value_init_map, uint32_t, READ_ARG);
        case OP_RECORD: VALUE_FROM_ARR(2, value_init_record, uint8_t, READ_NARROW);
        case OP_TUPLE: VALUE_FROM_ARR(1, value_init_tuple, uint8_t, READ_NARROW);
        case OP_RECORD_UPDATE: {
            uint8_t n = READ_BYTE();
            value_t base = sv_vec_pop(stack);
            if (!IS_RECORD(base))
                UNSUPPORTED_1(base, "Record update needs a record");

            int64_t missing = -1;
            record_t updated = record_update(AS_RECORD(base), &stack.arr[stack.size - 2 * n],
                                             n, &missing, a);
            if (missing >= 0) {
                value_t id = { .kind = VALUE_NUMBER, .number = (double)missing };
                OP_ERR_2(VM_ERR_FIELD_NOT_FOUND, base, id, "Field not found in record update");
            }
            TRY_OR(updated.items != NULL, value_free(&base, a), "OOM when updating a record");
            value_t out = value_wrap_record(updated, a);
            TRY_OR(out.obj.cell != NULL, record_deinit(&updated, a); value_free(&base, a),
                   "OOM when updating a record");

            arr_remove_n(&stack, 2 * n, a);
            value_free(&base, a);
            TRY_PUSH_OWNED(out);
            break;
        }
        case OP_HASHMAP_UPDATE: {
            uint32_t n = 0;
            READ_ARG(n);
            value_t base = sv_vec_pop(stack);
            if (!IS_MAP(base))
                UNSUPPORTED_1(base, "Hashmap update needs a hashmap");

            // map_put borrows the map and the pair, the stack keeps owning them
            hashmap_t cur = sv_rc_borrow(AS_MAP(base));
            value_free(&base, a);
            const value_t* kvs = &stack.arr[stack.size - 2 * n];
            for (uint32_t i = 0; i < n; i++) {
                hashmap_t next = map_put(cur, (kv_t){ .key = kvs[2 * i], .value = kvs[2 * i + 1] }, a);
                map_deinit(&cur, a);
                TRY_NOT_NULL(next.cell, "OOM when updating a hashmap");
                cur = next;
            }
            value_t out = value_wrap_map(cur, a);
            TRY_OR(out.obj.cell != NULL, map_deinit(&cur, a), "OOM when updating a hashmap");

            arr_remove_n(&stack, 2 * n, a);
            TRY_PUSH_OWNED(out);
            break;
        }
        case OP_LIST_PREPEND: {
            uint32_t n = 0;
            READ_ARG(n);
            value_t tail = sv_vec_pop(stack);
            if (!IS_LIST(tail))
                UNSUPPORTED_1(tail, "Spread into a list needs a list");

            list_t list = ll_prepend_arr(AS_LIST(tail), &stack.arr[stack.size - n], n, a);
            TRY_OR(list.cell != NULL, value_free(&tail, a), "OOM when building a list");
            value_t out = value_wrap_list(list, a);
            TRY_OR(out.obj.cell != NULL, ll_deinit(&list, a); value_free(&tail, a),
                   "OOM when building a list");

            arr_remove_n(&stack, n, a);
            value_free(&tail, a);
            TRY_PUSH_OWNED(out);
            break;
        }
        case OP_ADD: {
            value_t v2 = sv_vec_pop(stack);
            value_t v1 = sv_vec_pop(stack);
            if (IS_NUMBER(v1) && IS_NUMBER(v2)) {
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
            value_t v = sv_vec_pop(stack);
            value_t res = {.kind = VALUE_BOOL, .boolean = !value_is_truthy(v)};
            value_free(&v, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_SWAP: {
            value_t top = sv_vec_pop(stack);
            value_t under = sv_vec_pop(stack);
            TRY_PUSH_OWNED(top);
            TRY_PUSH_OWNED(under);
            break;
        }
        case OP_DUP: {
            value_t top = value_borrow(sv_vec_last(stack));
            TRY_PUSH_STACK(top);
            break;
        }
        case OP_INDEX: {
            value_t key = sv_vec_pop(stack);
            value_t container = sv_vec_pop(stack);
            if (!IS_MAP(container) && !IS_LIST(container) && !IS_TUPLE(container))
                UNSUPPORTED_2(container, key, "Type is not indexable")

            sv_opt_t(value_t) res;
            if (IS_MAP(container)) {
                res = map_get(AS_MAP(container), key);
            } else {
                if (!IS_NUMBER(key) || key.number != (double)(int64_t)key.number)
                    UNSUPPORTED_2(container, key, "Index is not an integer")
                res = IS_LIST(container)
                    ? ll_get(AS_LIST(container), (int64_t)key.number)
                    : tuple_get(AS_TUPLE(container), (int64_t)key.number);
            }
            if (!res.is_some)
                OP_ERR_2(VM_ERR_KEY_NOT_FOUND, container, key, "Key not found")

            value_t out = value_borrow(res.value);
            value_free(&container, a);
            value_free(&key, a);
            TRY_PUSH_OWNED(out);
            break;
        }
        case OP_HASHMAP_GET_OR_UNDEF: {
            value_t key = sv_vec_pop(stack);
            value_t container = sv_vec_pop(stack);
            if (!IS_MAP(container))
                UNSUPPORTED_2(container, key, "Type is not indexable")

            sv_opt_t(value_t) res;
            if (IS_MAP(container))
                res = map_get(AS_MAP(container), key);

            value_t out = res.is_some ? value_borrow(res.value) : value_undefined;
            value_free(&container, a);
            value_free(&key, a);
            TRY_PUSH_OWNED(out);
            break;
        }
#define RECORD_GET(...) do {                                                                                  \
    value_t maybe_tuple = sv_vec_pop(stack);                                                                  \
    uint8_t id = READ_BYTE();                                                                                 \
    if (!IS_RECORD(maybe_tuple))                                                                              \
        UNSUPPORTED_1(maybe_tuple, "Type is not subscriptable");                                              \
    record_t tuple = AS_RECORD(maybe_tuple);                                                                  \
    sv_opt_t(value_t) v = record_get(tuple, id);                                                              \
    if (!v.is_some) {                                                                                         \
        __VA_ARGS__;                                                                                          \
    }                                                                                                         \
    value_t out = value_borrow(v.value);                                                                      \
    value_free(&maybe_tuple, a);                                                                              \
    TRY_PUSH_OWNED(out);                                                                                      \
} while (0)
        case OP_RECORD_GET:
            RECORD_GET({
                value_t n = {.kind = VALUE_NUMBER, .number = (double)id};
                OP_ERR_2(VM_ERR_FIELD_NOT_FOUND, maybe_tuple, n, "Field not found");
            });
            break;
        case OP_RECORD_GET_OR_UNDEF:
            RECORD_GET(v = sv_opt_some_t(value_t, value_undefined));
            break;
#undef RECORD_GET
        case OP_LENGTH: {
            value_t val = sv_vec_pop(stack);
            value_t ret = { .kind = VALUE_NUMBER };
            if (IS_RECORD(val))
                ret.number = AS_RECORD(val).size;
            else if (IS_STR(val))
                ret.number = AS_STR(val).size;
            else if (IS_LIST(val))
                ret.number = AS_LIST(val).cell->count;
            else if (IS_MAP(val))
                ret.number =  map_count(AS_MAP(val));
            else
                UNSUPPORTED_1(val, "Type has no length");
            value_free(&val, a);
            TRY_PUSH_STACK(ret);
            break;
        }
        case OP_ITER_CREATE: {
            value_t v = sv_vec_pop(stack);
            if (!IS_LIST(v) && !IS_STR(v) && !IS_MAP(v))
                UNSUPPORTED_1(v, "Type is not iterable");
            value_t iter = value_init_iter(v, a);
            value_free(&v, a);
            TRY_NOT_NULL(iter.obj.cell, "OOM when creating iterator");
            TRY_PUSH_OWNED(iter);
            break;
        }
        case OP_ITER_NEXT: {
            value_t v = sv_vec_pop(stack);
            if (!IS_ITER(v))
                UNSUPPORTED_1(v, "Type is not an iterator");
            value_t res = iter_next(&AS_ITER(v), a);
            value_free(&v, a);
            TRY_OR(!(res.kind == VALUE_OBJ && res.obj.cell == NULL), (void)0,
                   "OOM when advancing iterator");
            TRY_PUSH_OWNED(res);
            break;
        }
        case OP_LOAD_CLOSURE: {
            uint32_t i = 0;
            READ_ARG(i);
            uint8_t n_cls = READ_BYTE();
            value_t cls = value_init_closure(
                &frame->fn->chunk.functions.arr[i],
                &stack.arr[stack.size - n_cls],
                n_cls,
                a);
            TRY_NOT_NULL(cls.obj.cell, "OOM when creating closure");
            arr_remove_n(&stack, n_cls, a);
            TRY_PUSH_OWNED(cls);
            break;
        }
        case OP_CREATE_GROUP: {
            uint32_t first = 0;
            READ_ARG(first);
            uint8_t n_members = READ_BYTE();
            uint8_t n_upvalues = READ_BYTE();

            sv_vec_grow_cap(&stack, stack.size + n_members, &success, a);
            TRY_OR(success, (void)0, "OOM when creating closure group")

            closure_group_t g = {
                .members = {
                    .arr = &frame->fn->chunk.functions.arr[first],
                    .size = n_members,
                    .capacity = n_members,
                    .element_size = sizeof(fn_t),
                },
                .upvalues = sv_vec_init(value_t),
            };
            if (n_upvalues > 0) {
                g.upvalues = (sv_vec_t(value_t))sv_vec_init_capacity(value_t, n_upvalues, a);
                TRY_NOT_NULL(g.upvalues.arr, "OOM when creating closure group");
                g.upvalues.size = n_upvalues;
                for (int64_t i = 0; i < n_upvalues; i++)
                    g.upvalues.arr[i] = value_borrow(stack.arr[stack.size - n_upvalues + i]);
            }

            sv_rc_t(closure_group_t) group = sv_rc_init(closure_group_t, g, clsg_deinit, a);
            TRY_OR(group.cell != NULL, clsg_deinit(&g, a), "OOM when creating closure group")
            arr_remove_n(&stack, n_upvalues, a);

            for (int64_t i = n_members - 1; i >= 0; i--) {
                value_t member = value_init_closure_member(sv_rc_borrow(group), i, a);
                TRY_OR(member.obj.cell != NULL, sv_rc_deinit(&group, a), "OOM when creating closure member")
                stack.arr[stack.size++] = member;
            }
            sv_rc_deinit(&group, a);
            break;
        }
        case OP_GET_MEMBER: {
            uint8_t i = READ_BYTE();
            if (frame->group.cell == NULL) {
                vm_instruction_err* p = sv_malloc(a, sizeof(vm_instruction_err));
                if (p != NULL)
                    *p = (vm_instruction_err){
                        .vm_err = { .line = LINE() },
                        .instruction = OP_GET_MEMBER,
                    };
                err = (error_t){ .error_code = VM_ERR_NO_GROUP,
                                 .payload = p,
                                 .msg = sv_str_init("no closure group in scope") };
                goto error;
            }
            value_t member = value_init_closure_member(sv_rc_borrow(frame->group), i, a);
            TRY_NOT_NULL(member.obj.cell, "OOM when creating closure member");
            TRY_PUSH_OWNED(member);
            break;
        }
#define ERR_WRONG_ARITY(arity) do {                                                                           \
    vm_arity_err* p = sv_malloc(a, sizeof(vm_arity_err));                                                     \
    if (p != NULL)                                                                                            \
        *p = (vm_arity_err){ .vm_err = { .line = LINE() }, .expected = arity, .got = arg_count };             \
    err = (error_t){ .error_code = VM_ERR_BAD_ARITY,                                                          \
                     .payload = p,                                                                            \
                     .msg = sv_str_init("wrong number of arguments") };                                       \
    value_free(&value, a);                                                                                    \
    goto error; } while (0)

#define LOAD_FN()                                                                                             \
    value_t value = sv_vec_pop(stack);                                                                        \
    uint8_t arg_count = READ_BYTE();                                                                          \
    if (!IS_CLOSURE(value) && !IS_NATIVE(value) && !IS_CLOSURE_MEMBER(value))                                 \
        UNSUPPORTED_1(value, "Type is not callable");                                                         \
    const value_t* args = &stack.arr[stack.size - arg_count];                                                 \
    if (IS_NATIVE(value)) {                                                                                   \
        CALL_NATIVE(value);                                                                                   \
        break;                                                                                                \
    }

#define CALL_NATIVE(value) do {                                                                               \
    native_fn_t fn = AS_NATIVE(value);                                                                        \
    if (fn.arity != arg_count)                                                                                \
        ERR_WRONG_ARITY(fn.arity);                                                                            \
    value_t ret = fn.fn(args, arg_count, &vm->ctx);                                                           \
    if (IS_ERR(ret)) {                                                                                        \
        err = AS_ERR(ret);                                                                                    \
        vm_err_t* vm_err = err.payload;                                                                       \
        vm_err->line = LINE();                                                                                \
        value_free(&value, a);                                                                                \
        goto error;                                                                                           \
    }                                                                                                         \
    arr_remove_n(&stack, arg_count, a);                                                                       \
    value_free(&value, a);                                                                                    \
    TRY_PUSH_OWNED(ret);                                                                                      \
} while (0)

#define INIT_FN_VM(fn, upvalues, group)                                                                       \
    fn_t* fn;                                                                                                 \
    value_arr upvalues;                                                                                       \
    sv_rc_t(closure_group_t) group = { 0 };                                                                   \
    do {                                                                                                      \
    if (IS_CLOSURE(value)) {                                                                                  \
        closure_t* cls = &AS_CLOSURE(value);                                                                  \
        fn = cls_get_vm(*cls);                                                                                \
        upvalues = cls->upvalues;                                                                             \
    } else {                                                                                                  \
        closure_member_t* member = &AS_CLOSURE_MEMBER(value);                                                 \
        fn = clsm_get_vm(*member);                                                                            \
        upvalues = member->group.cell->value.upvalues;                                                        \
        group = member->group;                                                                                \
    }                                                                                                         \
    if (arg_count != fn->arity)                                                                               \
        ERR_WRONG_ARITY(fn->arity);                                                                           \
} while (0)

#define LOAD_ARGS(locals_offset)                                                                              \
    int64_t locals_offset = vm->locals.size;                                                                  \
    do {                                                                                                      \
    if (arg_count > 0) {                                                                                      \
        sv_vec_push_many(&vm->locals, args, arg_count, &success, a);                                          \
        TRY_OR(success, value_free(&value, a), "OOM when passing arguments");                                 \
        stack.size -= arg_count;                                                                              \
    }                                                                                                         \
    sv_vec_push(&vm->locals, value, &success, a);                                                             \
    TRY_OR(success, value_free(&value, a), "OOM when passing arguments");                                     \
} while (0)
        case OP_CALL: {
            LOAD_FN();
            INIT_FN_VM(fn, upvalues, group);

            if (vm->call_frames.size >= vm->max_call_frames) {
                vm_err_t* p = sv_malloc(a, sizeof(vm_err_t));
                if (p != NULL)
                    *p = (vm_err_t){ .line = LINE() };
                err = (error_t){ .error_code = VM_ERR_STACK_OVERFLOW,
                                 .payload = p,
                                 .msg = sv_str_init("call stack overflow") };
                value_free(&value, a);
                goto error;
            }

            // The arguments move into the callee's locals and the callee takes the
            // self slot, which keeps its upvalues and group alive for the frame.
            LOAD_ARGS(locals_offset);

            frame->ip = ip - code;
            TRY_PUSH(vm->call_frames, init_frame(fn, locals_offset, stack.size, upvalues, group));
            frame = &sv_vec_last(vm->call_frames);
            LOAD_CODE();
            break;
        }
        case OP_TAIL_CALL: {
            LOAD_FN();
            INIT_FN_VM(fn, upvalues, group);

            // free locals
            arr_remove_n(&vm->locals, vm->locals.size - frame->locals_offset, a);
            LOAD_ARGS(locals_offset);
            // now free stack
            arr_remove_n(&stack, stack.size - frame->stack_offset, a);

            sv_vec_last(vm->call_frames) = init_frame(fn, locals_offset, stack.size, upvalues, group);
            frame = &sv_vec_last(vm->call_frames);
            LOAD_CODE();
            break;
#undef ERR_WRONG_ARITY
#undef LOAD_ARGS
#undef LOAD_FN
#undef INIT_FN_VM
#undef CALL_NATIVE
        }
        case OP_IS_STR: IS_KIND(IS_STR(v))
        case OP_IS_NUMBER: IS_KIND(IS_NUMBER(v))
        case OP_IS_BOOL: IS_KIND(IS_BOOL(v))
        case OP_IS_NIL: IS_KIND(IS_NIL(v))
        case OP_IS_LIST: IS_KIND(IS_LIST(v))
        case OP_IS_NIL_LIST: IS_KIND(IS_LIST(v) && ll_count(AS_LIST(v)) == 0)
        case OP_IS_CONS: IS_KIND(IS_CONS(v))
        case OP_IS_RECORD_ANY: IS_KIND(IS_RECORD(v))
        case OP_IS_TUPLE: IS_SIZED(IS_TUPLE, AS_TUPLE, size)
        case OP_IS_RECORD: IS_SIZED(IS_RECORD, AS_RECORD, size)
        case OP_IS_HASHMAP: {
            value_t v = sv_vec_pop(stack);
            value_t want = sv_vec_pop(stack);
            value_t res = { .kind = VALUE_BOOL,
                            .boolean = IS_MAP(v) && (double)map_count(AS_MAP(v)) == want.number };
            value_free(&v, a);
            value_free(&want, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_IS_HASHMAP_ANY: {
            value_t v = sv_vec_pop(stack);
            value_t res = { .kind = VALUE_BOOL, .boolean = IS_MAP(v) };
            value_free(&v, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_HAS_FIELD: {
            value_t v = sv_vec_pop(stack);
            uint8_t id = READ_BYTE();
            value_t res = { .kind = VALUE_BOOL,
                            .boolean = IS_RECORD(v) && record_get(AS_RECORD(v), id).is_some };
            value_free(&v, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_HAS_KEY: {
            value_t key = sv_vec_pop(stack);
            value_t v = sv_vec_pop(stack);
            value_t res = { .kind = VALUE_BOOL,
                            .boolean = IS_MAP(v) && map_get(AS_MAP(v), key).is_some };
            value_free(&key, a);
            value_free(&v, a);
            TRY_PUSH_STACK(res);
            break;
        }
        case OP_LIST_UNCONS: {
            value_t v = sv_vec_last(stack);
            if (!IS_CONS(v))
                UNSUPPORTED_1(v, "Cannot take the head of an empty list");

            list_t rest = ll_tail(AS_LIST(v), a);
            TRY_OR(rest.cell != NULL, value_free(&v, a), "OOM when taking a list tail")
            value_t tail = value_wrap_list(rest, a);
            TRY_OR(tail.obj.cell != NULL, ll_deinit(&rest, a); value_free(&v, a),
                   "OOM when taking a list tail")

            value_t first = value_borrow(ll_head(AS_LIST(v)).value);
            TRY_PUSH_OWNED(tail);
            TRY_PUSH_OWNED(first);
            break;
        }
        case OP_ASSERT_MATCH: {
            value_t test = sv_vec_pop(stack);
            bool passed = IS_BOOL(test) && test.boolean;
            value_free(&test, a);
            if (passed)
                break;

            value_t subject = value_borrow(sv_vec_last(stack));
            OP_ERR_1(VM_ERR_MATCH_FAILED, subject, "Value does not match the pattern");
        }
        case OP_NO_MATCH: {
            value_t subject = value_borrow(sv_vec_last(stack));
            OP_ERR_1(VM_ERR_NO_CLAUSE, subject, "No clause matched");
        }
        case OP_EXTENDED_ARG:
            arg_bytes = READ_BYTE();
            break;
        case OP_RETURN: {
            if (vm->call_frames.size == 1) {
                vm->stack = stack;
                vm->call_frames.size = 0;
                return sv_opt_none_t(error_t);
            }
            value_t res = sv_vec_pop(stack);
            arr_remove_n(&stack, stack.size - frame->stack_offset, a);
            arr_remove_n(&vm->locals, vm->locals.size - frame->locals_offset, a);
            vm->call_frames.size--;
            frame = &sv_vec_last(vm->call_frames);
            LOAD_CODE();
            TRY_PUSH_OWNED(res);
            break;
        }
        default: {
            vm_instruction_err* instruction_err_payload = sv_malloc(a, sizeof(vm_instruction_err));
            if (instruction_err_payload != NULL)
                *instruction_err_payload = (vm_instruction_err){
                    .vm_err = { .line = LINE() },
                    .instruction = ip[-1],
                };
            err = (error_t){ .error_code = VM_ERR_NOT_IMPLEMENTED,
                             .payload = instruction_err_payload,
                             .msg = sv_str_init("instruction not implemented") };
            goto error;
        }
    }

    return sv_opt_none_t(error_t);

error:
    vm->stack = stack;
    vm->call_frames.size = 0;
    return sv_opt_some_t(error_t, err);

#undef SET
#undef GET
#undef MATH_OP
#undef CMP_OP
#undef IS_KIND
#undef IS_SIZED
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
#undef LINE
#undef OFFSET
#undef LOAD_CODE
}
