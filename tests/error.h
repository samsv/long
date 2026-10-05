#ifndef SV_TESTS_ERROR_H
#define SV_TESTS_ERROR_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/error.h"

static inline void sv_test_error(sv_testing_t* t)
{
   error_t e = error_fmt(7, 3, &sv_gpa, "%s and %d", "text", 42);
   sv_test_run(t, e.error_code == 7);
   sv_test_run(t, sv_str_comp(e.msg, sv_str_init("Line 3: text and 42")));
   error_free(&e, &sv_gpa);
   sv_test_run(t, e.msg.size == 0);
   error_free(&e, &sv_gpa);

   error_t set = error_init();
   sv_test_run(t, !error_set(&set, 1, "copied", &sv_gpa));
   sv_test_run(t, sv_str_comp(set.msg, sv_str_init("copied")));
   error_free(&set, &sv_gpa);
}

#endif
