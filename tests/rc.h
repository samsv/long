#ifndef SV_TESTS_RC_H
#define SV_TESTS_RC_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/std/string.h"
#include "../src/std/rc.h"
#include "string.h"

sv_rc_def(int);
sv_rc_def(sv_str_t);

static int sv_test_rc_frees = 0;

static void sv_test_rc_free_int(int* v, const sv_allocator_t* a)
{
   (void)v;
   (void)a;
   sv_test_rc_frees++;
}

static inline void sv_test_rc_counting(sv_testing_t* t)
{
   sv_test_rc_frees = 0;

   sv_rc_t(int) rc = sv_rc_init(int, 42, sv_test_rc_free_int, &sv_gpa);
   sv_test_run(t, rc.cell != NULL);
   sv_test_run(t, rc.cell->count == 1);
   sv_test_run(t, *sv_rc_get(rc) == 42);

   sv_rc_t(int) rc2 = sv_rc_borrow(rc);
   sv_test_run(t, rc2.cell == rc.cell);
   sv_test_run(t, rc.cell->count == 2);
   sv_test_run(t, *sv_rc_get(rc2) == 42);

   sv_rc_deinit(&rc, &sv_gpa);
   sv_test_run(t, rc.cell == NULL);
   sv_test_run(t, sv_test_rc_frees == 0);
   sv_test_run(t, rc2.cell->count == 1);

   sv_rc_deinit(&rc, &sv_gpa);
   sv_test_run(t, sv_test_rc_frees == 0);
   sv_test_run(t, rc2.cell->count == 1);

   sv_rc_t(int) stale = sv_rc_borrow(rc);
   sv_test_run(t, stale.cell == NULL);
   sv_test_run(t, sv_rc_get(stale) == NULL);

   sv_rc_deinit(&rc2, &sv_gpa);
   sv_test_run(t, rc2.cell == NULL);
   sv_test_run(t, sv_test_rc_frees == 1);

   sv_rc_deinit(&rc2, &sv_gpa);
   sv_test_run(t, sv_test_rc_frees == 1);
}

static inline void sv_test_rc_owned_value(sv_testing_t* t)
{
   sv_str_t owned = sv_str_copy(sv_str_init("counted"), &sv_gpa);
   sv_test_run(t, owned.size == 7);

   sv_rc_t(sv_str_t) rc = sv_rc_init(sv_str_t, owned, sv_str_deinit, &sv_gpa);
   sv_test_run(t, rc.cell != NULL);

   sv_rc_t(sv_str_t) rc2 = sv_rc_borrow(rc);
   sv_test_run(t, sv_str_comp(*sv_rc_get(rc2), sv_str_init("counted")));

   sv_rc_deinit(&rc2, &sv_gpa);
   sv_test_run(t, sv_str_comp(*sv_rc_get(rc), sv_str_init("counted")));

   sv_rc_deinit(&rc, &sv_gpa);
   sv_test_run(t, rc.cell == NULL);
}

static inline void sv_test_rc_oom(sv_testing_t* t)
{
   sv_test_rc_frees = 0;

   sv_rc_t(int) rc = sv_rc_init(int, 7, sv_test_rc_free_int, &sv_test_fail_alloc);
   sv_test_run(t, rc.cell == NULL);

   sv_rc_t(int) rc2 = sv_rc_borrow(rc);
   sv_test_run(t, rc2.cell == NULL);
   sv_test_run(t, sv_rc_get(rc) == NULL);
   sv_test_run(t, sv_rc_get(rc2) == NULL);

   sv_rc_deinit(&rc, &sv_test_fail_alloc);
   sv_rc_deinit(&rc2, &sv_test_fail_alloc);
   sv_test_run(t, sv_test_rc_frees == 0);
}

static inline void sv_test_rc(sv_testing_t* t)
{
   sv_test_rc_counting(t);
   sv_test_rc_owned_value(t);
   sv_test_rc_oom(t);
}

#endif
