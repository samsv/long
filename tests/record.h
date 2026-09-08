#ifndef SV_TESTS_RECORD_H
#define SV_TESTS_RECORD_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/vm.h"
#include "string.h"

static inline value_t sv_test_record_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline void sv_test_record_values(sv_testing_t* t)
{
   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t items[] = {
      sv_test_record_num(0), sv_test_record_num(10),
      sv_test_record_num(2), s,
   };
   value_t tup = value_init_record(items, 2, &sv_gpa);
   sv_test_run(t, IS_RECORD(tup));
   sv_test_run(t, s.obj.cell->count == 2);

   sv_opt_t(value_t) got = record_get(AS_RECORD(tup), 0);
   sv_test_run(t, got.is_some && got.value.number == 10);
   got = record_get(AS_RECORD(tup), 2);
   sv_test_run(t, got.is_some && got.value.obj.cell == s.obj.cell);
   sv_test_run(t, !record_get(AS_RECORD(tup), 1).is_some);
   sv_test_run(t, !record_get(AS_RECORD(tup), 99).is_some);

   value_t same = value_init_record(items, 2, &sv_gpa);
   sv_test_run(t, value_eql(tup, same));

   value_t other_value[] = { sv_test_record_num(0), sv_test_record_num(11), sv_test_record_num(2), s };
   value_t diff_value = value_init_record(other_value, 2, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_value));

   value_t other_id[] = { sv_test_record_num(1), sv_test_record_num(10), sv_test_record_num(2), s };
   value_t diff_id = value_init_record(other_id, 2, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_id));

   value_t shorter[] = { sv_test_record_num(0), sv_test_record_num(10) };
   value_t diff_size = value_init_record(shorter, 1, &sv_gpa);
   sv_test_run(t, !value_eql(tup, diff_size));

   value_free(&same, &sv_gpa);
   value_free(&diff_value, &sv_gpa);
   value_free(&diff_id, &sv_gpa);
   value_free(&diff_size, &sv_gpa);
   value_free(&tup, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 1);
   value_free(&s, &sv_gpa);
}

static inline void sv_test_record_print(sv_testing_t* t)
{
   const char* names[] = { "x", "y", "z" };
   vm_ctx_t ctx = {
      .alloc = &sv_gpa,
      .logger = sv_std_logger,
      .record_key_names = names,
      .record_names_sizes = 3,
   };

   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t items[] = {
      sv_test_record_num(0), sv_test_record_num(10),
      sv_test_record_num(2), s,
   };
   value_t tup = value_init_record(items, 2, &sv_gpa);

   sv_str_t text = value_to_str(tup, &ctx);
   sv_test_run(t, sv_str_comp(text, sv_str_init("{x: 10, z: str}")));
   sv_str_deinit(&text, &sv_gpa);

   value_free(&tup, &sv_gpa);
   value_free(&s, &sv_gpa);
}

static inline void sv_test_record_vm_ops(sv_testing_t* t)
{
   fn_builder_t b = fnb_init(sv_str_copy(sv_str_init("tuple test"), &sv_gpa));
   sv_test_run(t, fnb_add_constant(&b, sv_test_record_num(0), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_constant(&b, sv_test_record_num(10), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_constant(&b, sv_test_record_num(1), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_constant(&b, sv_test_record_num(20), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_bytes(&b, OP_RECORD, 2, 0, &sv_gpa));
   sv_test_run(t, fnb_add_bytes(&b, OP_RECORD_GET, 1, 0, &sv_gpa));
   sv_test_run(t, fnb_add_byte(&b, OP_RETURN, 0, &sv_gpa));

   vm_t vm = vm_init(fnb_build(&b), 1000000, map_init(NULL, 0, &sv_gpa));
   vm.ctx.alloc = &sv_gpa;
   sv_opt_t(error_t) err = vm_run(&vm);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, vm.stack.arr[0].kind == VALUE_NUMBER && vm.stack.arr[0].number == 20);
   vm_deinit(&vm, &sv_gpa);

   fn_builder_t mb = fnb_init(sv_str_copy(sv_str_init("tuple miss"), &sv_gpa));
   sv_test_run(t, fnb_add_constant(&mb, sv_test_record_num(0), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_constant(&mb, sv_test_record_num(10), &sv_gpa).is_some);
   sv_test_run(t, fnb_add_bytes(&mb, OP_RECORD, 1, 0, &sv_gpa));
   sv_test_run(t, fnb_add_bytes(&mb, OP_RECORD_GET, 7, 0, &sv_gpa));
   sv_test_run(t, fnb_add_byte(&mb, OP_RETURN, 0, &sv_gpa));

   vm_t miss = vm_init(fnb_build(&mb), 1000000, map_init(NULL, 0, &sv_gpa));
   miss.ctx.alloc = &sv_gpa;
   sv_opt_t(error_t) merr = vm_run(&miss);
   sv_test_run(t, merr.is_some);
   sv_test_run(t, merr.value.error_code == VM_ERR_KEY_NOT_FOUND);
   vm_err_deinit(&merr.value, &sv_gpa);
   vm_deinit(&miss, &sv_gpa);
}

static inline void sv_test_record_update(sv_testing_t* t)
{
   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t s2 = value_init_str(sv_str_init("new"), &sv_gpa);
   value_t items[] = {
      sv_test_record_num(0), sv_test_record_num(10),
      sv_test_record_num(2), s,
      sv_test_record_num(5), sv_test_record_num(50),
   };
   value_t base = value_init_record(items, 3, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 2);

   /* The first and last field in one merge; the middle one is carried over. */
   value_t pairs[] = {
      sv_test_record_num(0), sv_test_record_num(11),
      sv_test_record_num(5), s2,
   };
   int64_t missing = 0;
   record_t up = record_update(AS_RECORD(base), pairs, 2, &missing, &sv_gpa);
   sv_test_run(t, up.items != NULL && missing == -1 && up.size == 3);
   sv_test_run(t, record_get(up, 0).value.number == 11);
   sv_test_run(t, record_get(up, 2).value.obj.cell == s.obj.cell);
   sv_test_run(t, record_get(up, 5).value.obj.cell == s2.obj.cell);
   sv_test_run(t, s.obj.cell->count == 3 && s2.obj.cell->count == 2);
   sv_test_run(t, record_get(AS_RECORD(base), 0).value.number == 10);
   sv_test_run(t, record_get(AS_RECORD(base), 5).value.number == 50);
   record_deinit(&up, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 2 && s2.obj.cell->count == 1);

   /* An id between two fields is missing: nothing is built and no borrow is left behind. */
   value_t gap[] = {
      sv_test_record_num(0), sv_test_record_num(1),
      sv_test_record_num(3), sv_test_record_num(3),
   };
   record_t none = record_update(AS_RECORD(base), gap, 2, &missing, &sv_gpa);
   sv_test_run(t, none.items == NULL && missing == 3);
   sv_test_run(t, s.obj.cell->count == 2);

   value_free(&base, &sv_gpa);
   value_free(&s2, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 1);
   value_free(&s, &sv_gpa);
}

static inline void sv_test_record(sv_testing_t* t)
{
   sv_test_record_values(t);
   sv_test_record_update(t);
   sv_test_record_print(t);
   sv_test_record_vm_ops(t);
}

#endif
