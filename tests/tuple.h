#ifndef SV_TESTS_TUPLE_H
#define SV_TESTS_TUPLE_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/vm.h"
#include "string.h"

static inline value_t sv_test_tuple_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline void sv_test_tuple_values(sv_testing_t* t)
{
   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t items[] = {
      sv_test_tuple_num(0), sv_test_tuple_num(10),
      sv_test_tuple_num(2), s,
   };
   value_t tup = value_init_tuple(items, 2, &sv_gpa);
   sv_test_run(t, IS_TUPLE(tup));
   sv_test_run(t, s.obj.cell->count == 2);

   sv_opt_t(value_t) got = tuple_get(AS_TUPLE(tup), 0);
   sv_test_run(t, got.is_some && got.value.number == 10);
   got = tuple_get(AS_TUPLE(tup), 2);
   sv_test_run(t, got.is_some && got.value.obj.cell == s.obj.cell);
   sv_test_run(t, !tuple_get(AS_TUPLE(tup), 1).is_some);
   sv_test_run(t, !tuple_get(AS_TUPLE(tup), 99).is_some);

   value_t same = value_init_tuple(items, 2, &sv_gpa);
   sv_test_run(t, value_eql(tup, same));

   value_t other_value[] = { sv_test_tuple_num(0), sv_test_tuple_num(11), sv_test_tuple_num(2), s };
   value_t diff_value = value_init_tuple(other_value, 2, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_value));

   value_t other_id[] = { sv_test_tuple_num(1), sv_test_tuple_num(10), sv_test_tuple_num(2), s };
   value_t diff_id = value_init_tuple(other_id, 2, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_id));

   value_t shorter[] = { sv_test_tuple_num(0), sv_test_tuple_num(10) };
   value_t diff_size = value_init_tuple(shorter, 1, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_size));

   value_free(&same, &sv_gpa);
   value_free(&diff_value, &sv_gpa);
   value_free(&diff_id, &sv_gpa);
   value_free(&diff_size, &sv_gpa);
   value_free(&tup, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 1);
   value_free(&s, &sv_gpa);
}

static inline void sv_test_tuple_print(sv_testing_t* t)
{
   const char* names[] = { "x", "y", "z" };
   vm_ctx_t ctx = {
      .alloc = &sv_gpa,
      .logger = sv_std_logger,
      .tuple_key_names = names,
      .tuple_names_sizes = 3,
   };

   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t items[] = {
      sv_test_tuple_num(0), sv_test_tuple_num(10),
      sv_test_tuple_num(2), s,
   };
   value_t tup = value_init_tuple(items, 2, &sv_gpa);

   sv_str_t text = value_to_str(tup, &ctx);
   sv_test_run(t, sv_str_comp(text, sv_str_init("{x: 10, z: str}")));
   sv_str_deinit(&text, &sv_gpa);

   value_free(&tup, &sv_gpa);
   value_free(&s, &sv_gpa);
}

static inline void sv_test_tuple_vm_ops(sv_testing_t* t)
{
   vm_builder_t b = vmb_init(sv_str_init("tuple test"));
   sv_test_run(t, vmb_add_constant(&b, sv_test_tuple_num(0), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_constant(&b, sv_test_tuple_num(10), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_constant(&b, sv_test_tuple_num(1), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_constant(&b, sv_test_tuple_num(20), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_bytes(&b, OP_TUPLE, 2, 0, &sv_gpa));
   sv_test_run(t, vmb_add_bytes(&b, OP_TUPLE_GET, 1, 0, &sv_gpa));
   sv_test_run(t, vmb_add_byte(&b, OP_RETURN, 0, &sv_gpa));

   vm_t vm = vmb_build(&b);
   vm.ctx.alloc = &sv_gpa;
   sv_opt_t(error_t) err = vm_run(&vm);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, vm.stack.arr[0].kind == VALUE_NUMBER && vm.stack.arr[0].number == 20);
   vm_deinit(&vm, &sv_gpa);

   vm_builder_t mb = vmb_init(sv_str_init("tuple miss"));
   sv_test_run(t, vmb_add_constant(&mb, sv_test_tuple_num(0), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_constant(&mb, sv_test_tuple_num(10), &sv_gpa).is_some);
   sv_test_run(t, vmb_add_bytes(&mb, OP_TUPLE, 1, 0, &sv_gpa));
   sv_test_run(t, vmb_add_bytes(&mb, OP_TUPLE_GET, 7, 0, &sv_gpa));
   sv_test_run(t, vmb_add_byte(&mb, OP_RETURN, 0, &sv_gpa));

   vm_t miss = vmb_build(&mb);
   miss.ctx.alloc = &sv_gpa;
   sv_opt_t(error_t) merr = vm_run(&miss);
   sv_test_run(t, merr.is_some);
   sv_test_run(t, merr.value.error_code == VM_ERR_KEY_NOT_FOUND);
   vm_err_deinit(&merr.value, &sv_gpa);
   vm_deinit(&miss, &sv_gpa);
}

static inline void sv_test_tuple(sv_testing_t* t)
{
   sv_test_tuple_values(t);
   sv_test_tuple_print(t);
   sv_test_tuple_vm_ops(t);
}

#endif
