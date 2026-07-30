#ifndef SV_TESTS_STR_ITER_H
#define SV_TESTS_STR_ITER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/obj.h"

static inline void sv_test_str_iter(sv_testing_t* t)
{
   value_t s = value_init_str(sv_str_init("aé😀"), &sv_gpa);
   value_t it = value_init_iter(s, &sv_gpa);

   const char* expected[] = { "a", "é", "😀" };
   for (int i = 0; i < 3; i++) {
      value_t c = iter_next(&AS_ITER(it), &sv_gpa);
      sv_test_run(t, IS_STR(c));
      sv_test_run(t, sv_str_comp(AS_STR(c), sv_str_init(expected[i])));
      value_free(&c, &sv_gpa);
   }
   value_t end = iter_next(&AS_ITER(it), &sv_gpa);
   sv_test_run(t, end.kind == VALUE_NIL);

   value_free(&it, &sv_gpa);
   value_free(&s, &sv_gpa);
}

#endif
