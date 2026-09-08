#include "fn.h"
#include "../value.h"
#include "../vm.h"

chunk_t chunk_init(void)
{
    return (chunk_t){
        .bytecode = sv_vec_init(uint8_t),
        .lines = sv_vec_init(int64_t),
        .constants = sv_vec_init(value_t),
        .functions = sv_vec_init(fn_t),
    };
}

void chunk_deinit(chunk_t* c, const sv_allocator_t* a)
{
    value_arr_deinit(&c->constants, a);
    sv_vec_foreach(fn_t, f, &c->functions)
        fn_deinit(&f, a);
    sv_vec_deinit(&c->functions, a);
    sv_vec_deinit(&c->bytecode, a);
    sv_vec_deinit(&c->lines, a);
}

void fn_deinit(fn_t* fn, const sv_allocator_t* a)
{
    sv_str_deinit(&fn->name, a);
    chunk_deinit(&fn->chunk, a);
}

fn_builder_t fnb_init(sv_str_t name)
{
    return (fn_builder_t){ .fn = { .name = name, .arity = 0, .chunk = chunk_init() } };
}

fn_t fnb_build(fn_builder_t* b)
{
    fn_t fn = b->fn;
    b->fn = (fn_t){0};
    return fn;
}

bool fnb_add_byte(fn_builder_t* b, uint8_t byte, int64_t line, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->fn.chunk.bytecode, byte, &success, a);
    if (!success)
        return false;
    sv_vec_push(&b->fn.chunk.lines, line, &success, a);
    return success;
}

bool fnb_add_bytes(fn_builder_t* b, uint8_t b1, uint8_t b2, int64_t line, const sv_allocator_t* a)
{
    return fnb_add_byte(b, b1, line, a) && fnb_add_byte(b, b2, line, a);
}

bool fnb_add_arg(fn_builder_t* b, uint8_t op, uint32_t arg, int64_t line, const sv_allocator_t* a)
{
    uint8_t width = arg > 0xFFFFFF ? 4 : arg > 0xFFFF ? 3 : arg > 0xFF ? 2 : 1;
    if (width > 1 && !fnb_add_bytes(b, OP_EXTENDED_ARG, width, line, a))
        return false;
    if (!fnb_add_byte(b, op, line, a))
        return false;
    for (uint8_t i = 0; i < width; i++)
        if (!fnb_add_byte(b, (uint8_t)(arg >> (8 * i)), line, a))
            return false;
    return true;
}

sv_opt_t(uint32_t) fnb_add_constant(fn_builder_t* b, value_t c, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->fn.chunk.constants, c, &success, a);
    if (!success)
        return sv_opt_none_t(uint32_t);

    int64_t i = b->fn.chunk.constants.size - 1;
    if (i > UINT32_MAX || !fnb_add_arg(b, OP_LOAD_CONSTANT, (uint32_t)i, 0, a))
        return sv_opt_none_t(uint32_t);
    return sv_opt_some_t(uint32_t, (uint32_t)i);
}

sv_opt_t(uint32_t) fnb_add_function(fn_builder_t* b, fn_t fn, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&b->fn.chunk.functions, fn, &success, a);
    if (!success)
        return sv_opt_none_t(uint32_t);

    int64_t i = b->fn.chunk.functions.size - 1;
    if (i > UINT32_MAX)
        return sv_opt_none_t(uint32_t);
    return sv_opt_some_t(uint32_t, (uint32_t)i);
}

sv_opt_t(uint32_t) fnb_add_closure(fn_builder_t* b, uint8_t cls_args_n, fn_t fn, const sv_allocator_t* a)
{
    sv_opt_t(uint32_t) i = fnb_add_function(b, fn, a);
    if (!i.is_some || !fnb_add_arg(b, OP_LOAD_CLOSURE, i.value, 0, a) || !fnb_add_byte(b, cls_args_n, 0, a))
        return sv_opt_none_t(uint32_t);
    return i;
}

void fnb_patch_jump(fn_builder_t* b, int64_t index, uint16_t value)
{
    b->fn.chunk.bytecode.arr[index] = (uint8_t)value;
    b->fn.chunk.bytecode.arr[index + 1] = (uint8_t)(value >> 8);
}

sv_opt_t(int64_t) fnb_add_jump(fn_builder_t* b, int64_t line, const sv_allocator_t* a)
{
    if (!fnb_add_byte(b, OP_JUMP, line, a) || !fnb_add_bytes(b, 255, 255, line, a))
        return sv_opt_none_t(int64_t);
    return sv_opt_some_t(int64_t, b->fn.chunk.bytecode.size - 2);
}

bool fnb_add_jump_back(fn_builder_t* b, int64_t to, int64_t line, const sv_allocator_t* a)
{
    if (!fnb_add_byte(b, OP_JUMP_BACK, line, a))
        return false;
    uint16_t offset = (uint16_t)(b->fn.chunk.bytecode.size - to);
    return fnb_add_bytes(b, (uint8_t)offset, (uint8_t)(offset >> 8), line, a);
}

sv_opt_t(int64_t) fnb_add_jump_if_false(fn_builder_t* b, int64_t line, const sv_allocator_t* a)
{
    if (!fnb_add_byte(b, OP_JUMP_IF_FALSE, line, a) || !fnb_add_bytes(b, 255, 255, line, a))
        return sv_opt_none_t(int64_t);
    return sv_opt_some_t(int64_t, b->fn.chunk.bytecode.size - 2);
}
