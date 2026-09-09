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

static inline void sv_test_tuple(sv_testing_t* t)
{
   value_t s = value_init_str(sv_str_init("str"), &sv_gpa);
   value_t items[] = { sv_test_tuple_num(1), s, sv_test_tuple_num(3) };
   value_t tup = value_init_tuple(items, 3, &sv_gpa);
   sv_test_run(t, IS_TUPLE(tup));
   sv_test_run(t, s.obj.cell->count == 2);

   sv_opt_t(value_t) got = tuple_get(AS_TUPLE(tup), 0);
   sv_test_run(t, got.is_some && got.value.number == 1);
   got = tuple_get(AS_TUPLE(tup), 1);
   sv_test_run(t, got.is_some && got.value.obj.cell == s.obj.cell);
   sv_test_run(t, !tuple_get(AS_TUPLE(tup), 3).is_some);
   sv_test_run(t, !tuple_get(AS_TUPLE(tup), -1).is_some);

   value_t same = value_init_tuple(items, 3, &sv_gpa);
   sv_test_run(t, value_eql(tup, same));

   value_t swapped_items[] = { s, sv_test_tuple_num(1), sv_test_tuple_num(3) };
   value_t swapped = value_init_tuple(swapped_items, 3, &sv_gpa);
   sv_test_run(t, !value_eql(tup, swapped));

   value_t shorter = value_init_tuple(items, 2, &sv_gpa);
   sv_test_run(t, !value_eql(tup, shorter));

   vm_ctx_t ctx = { .alloc = &sv_gpa, .logger = sv_std_logger };
   sv_str_t text = value_to_str(tup, &ctx);
   sv_test_run(t, sv_str_comp(text, sv_str_init("(1, str, 3)")));
   sv_str_deinit(&text, &sv_gpa);

   value_t one_items[] = { sv_test_tuple_num(7) };
   value_t one = value_init_tuple(one_items, 1, &sv_gpa);
   sv_str_t one_text = value_to_str(one, &ctx);
   sv_test_run(t, sv_str_comp(one_text, sv_str_init("(7,)")));
   sv_str_deinit(&one_text, &sv_gpa);

   value_free(&one, &sv_gpa);
   value_free(&shorter, &sv_gpa);
   value_free(&swapped, &sv_gpa);
   value_free(&same, &sv_gpa);
   value_free(&tup, &sv_gpa);
   sv_test_run(t, s.obj.cell->count == 1);
   value_free(&s, &sv_gpa);
}

#endif
