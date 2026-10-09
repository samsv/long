#ifndef SV_TESTS_ARENA_H
#define SV_TESTS_ARENA_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/std/arena.h"

static inline void sv_test_arena_mark_reset(sv_testing_t* t)
{
   sv_arena_t a = sv_arena_init(64, &sv_gpa, NULL);
   sv_test_run(t, a.data != NULL);

   char* p = sv_arena_malloc(&a, 16);
   sv_arena_mark_t m = sv_arena_mark(&a);
   char* q = sv_arena_malloc(&a, 16);
   char* r = sv_arena_malloc(&a, 48);
   sv_test_run(t, q == p + 16 && r != NULL && a.tail != NULL && a.tail == a.next);

   /* A reset returns to the mark and keeps the later block. */
   sv_arena_reset(&a, m);
   sv_test_run(t, a.tail == NULL && a.count == 16 && a.next != NULL && a.next->count == 0);
   char* q2 = sv_arena_malloc(&a, 16);
   char* r2 = sv_arena_malloc(&a, 48);
   sv_test_run(t, q2 == q && r2 == r);

   /* A mark on a later block. */
   sv_arena_mark_t m2 = sv_arena_mark(&a);
   sv_test_run(t, m2.block == a.next && m2.count == 48);
   char* s = sv_arena_malloc(&a, 64);
   sv_test_run(t, s != NULL && a.tail == a.next->next);
   sv_arena_reset(&a, m2);
   sv_test_run(t, a.tail == a.next && a.next->count == 48 && a.next->next->count == 0);

   sv_arena_deinit(&a);
   sv_test_run(t, a.data == NULL && a.tail == NULL && a.next == NULL);
}

static inline void sv_test_arena(sv_testing_t* t)
{
   sv_test_arena_mark_reset(t);
}

#endif
