#ifndef SV_TESTS_VECTOR_H
#define SV_TESTS_VECTOR_H

#include "../src/std/test.h"
#include "../src/std/vector.h"
#include "../src/std/allocator_std.h"

sv_vec_def(int);

static int sv_test_cmp_int(int a, int b)
{
   return a == b;
}

static inline void sv_test_vector_init(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   sv_test_run(t, vs.arr == NULL);
   sv_test_run(t, vs.size == 0);
   sv_test_run(t, vs.capacity == 0);
   sv_vec_deinit(&vs, &sv_gpa);

   sv_vec_t(int) with_cap = sv_vec_init_capacity(int, 16, &sv_gpa);
   sv_test_run(t, with_cap.arr != NULL);
   sv_test_run(t, with_cap.size == 0);
   sv_test_run(t, with_cap.capacity == 16);
   sv_vec_deinit(&with_cap, &sv_gpa);
}

static inline void sv_test_vector_push(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);

   int ok = 0;
   int all = 1;
   for (int i = 0; i < 100; i++) {
      sv_vec_push(&vs, i * 2, &ok, &sv_gpa);
      all = all && ok;
   }
   sv_test_run(t, all == 1);
   sv_test_run(t, vs.size == 100);
   sv_test_run(t, vs.capacity >= vs.size);
   sv_test_run(t, sv_vec_at(vs, 0) == 0);
   sv_test_run(t, sv_vec_at(vs, 50) == 100);
   sv_test_run(t, sv_vec_at(vs, 99) == 198);

   sv_vec_set_at(&vs, 7, 0);
   sv_test_run(t, sv_vec_at(vs, 0) == 7);

   sv_vec_deinit(&vs, &sv_gpa);
}

static inline void sv_test_vector_push_many(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   int xs[] = { 1, 2, 3, 4, 16, 15, 1021, 415 };

   int ok = 0;
   sv_vec_push_many(&vs, xs, 8, &ok, &sv_gpa);
   sv_test_run(t, ok == 1);
   sv_test_run(t, vs.size == 8);
   sv_test_run(t, sv_vec_at(vs, 0) == 1);
   sv_test_run(t, sv_vec_at(vs, 7) == 415);

   sv_vec_push_many(&vs, xs, 8, &ok, &sv_gpa);
   sv_test_run(t, ok == 1);
   sv_test_run(t, vs.size == 16);
   sv_test_run(t, sv_vec_at(vs, 7) == 415);
   sv_test_run(t, sv_vec_at(vs, 8) == 1);
   sv_test_run(t, sv_vec_at(vs, 15) == 415);

   sv_vec_push_many(&vs, xs, 0, &ok, &sv_gpa);
   sv_test_run(t, ok == 1);
   sv_test_run(t, vs.size == 16);

   sv_vec_push_many(&vs, xs, -1, &ok, &sv_gpa);
   sv_test_run(t, ok == 0);
   sv_test_run(t, vs.size == 16);

   sv_vec_deinit(&vs, &sv_gpa);

   sv_vec_t(int) big = sv_vec_init(int, &sv_gpa);
   int many[100];
   for (int i = 0; i < 100; i++)
      many[i] = i;
   sv_vec_push_many(&big, many, 100, &ok, &sv_gpa);
   sv_test_run(t, ok == 1);
   sv_test_run(t, big.size == 100);
   sv_test_run(t, sv_vec_at(big, 99) == 99);
   sv_vec_deinit(&big, &sv_gpa);
}

static inline void sv_test_vector_boundary(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init_capacity(int, 8, &sv_gpa);
   int ok = 0;
   int all = 1;
   for (int i = 0; i < 8; i++) {
      sv_vec_push(&vs, i * 3, &ok, &sv_gpa);
      all = all && ok;
   }
   sv_test_run(t, all == 1);
   sv_test_run(t, vs.size == 8);
   sv_test_run(t, vs.capacity > 8);
   sv_test_run(t, sv_vec_at(vs, 0) == 0);
   sv_test_run(t, sv_vec_at(vs, 7) == 21);
   sv_vec_deinit(&vs, &sv_gpa);
}

static inline void sv_test_vector_remove(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   int ok = 0;
   for (int i = 0; i < 8; i++)
      sv_vec_push(&vs, i, &ok, &sv_gpa);

   sv_vec_remove_swap(&vs, 2, &sv_gpa);
   sv_test_run(t, vs.size == 7);
   sv_test_run(t, sv_vec_at(vs, 2) == 7);

   int found = 0;
   sv_vec_is_in_auto(&vs, 2, &found);
   sv_test_run(t, found == 0);
   sv_vec_is_in_auto(&vs, 7, &found);
   sv_test_run(t, found == 1);

   sv_vec_remove_swap(&vs, vs.size - 1, &sv_gpa);
   sv_test_run(t, vs.size == 6);
   sv_test_run(t, sv_vec_at(vs, 0) == 0);

   while (vs.size > 2)
      sv_vec_remove_swap(&vs, 0, &sv_gpa);
   sv_test_run(t, vs.size == 2);
   sv_test_run(t, vs.capacity >= vs.size);

   int64_t sum = 0;
   sv_vec_foreach(int, el, &vs) {
      sum += el;
   }
   sv_test_run(t, sum == sv_vec_at(vs, 0) + sv_vec_at(vs, 1));

   sv_vec_deinit(&vs, &sv_gpa);

   sv_vec_t(int) ws = sv_vec_init(int, &sv_gpa);
   int xs[] = { 10, 20, 30, 40, 50 };
   sv_vec_push_many(&ws, xs, 5, &ok, &sv_gpa);

   sv_vec_remove_linear(&ws, 1, &sv_gpa);
   sv_test_run(t, ws.size == 4);
   sv_test_run(t, sv_vec_at(ws, 0) == 10);
   sv_test_run(t, sv_vec_at(ws, 1) == 30);
   sv_test_run(t, sv_vec_at(ws, 2) == 40);
   sv_test_run(t, sv_vec_at(ws, 3) == 50);

   sv_vec_remove_linear(&ws, 0, &sv_gpa);
   sv_test_run(t, ws.size == 3);
   sv_test_run(t, sv_vec_at(ws, 0) == 30);

   sv_vec_remove_linear(&ws, ws.size - 1, &sv_gpa);
   sv_test_run(t, ws.size == 2);
   sv_test_run(t, sv_vec_at(ws, 0) == 30);
   sv_test_run(t, sv_vec_at(ws, 1) == 40);

   int64_t wsum = 0;
   sv_vec_foreach(int, el, &ws) {
      wsum += el;
   }
   sv_test_run(t, wsum == 70);

   sv_vec_deinit(&ws, &sv_gpa);
}

static inline void sv_test_vector_empty_and_reuse(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   int ok = 0;
   for (int i = 0; i < 4; i++)
      sv_vec_push(&vs, i, &ok, &sv_gpa);

   while (vs.size > 0)
      sv_vec_remove_swap(&vs, 0, &sv_gpa);
   sv_test_run(t, vs.size == 0);
   sv_test_run(t, vs.capacity > 0);

   sv_vec_push(&vs, 77, &ok, &sv_gpa);
   sv_test_run(t, ok == 1);
   sv_test_run(t, vs.size == 1);
   sv_test_run(t, sv_vec_at(vs, 0) == 77);

   int64_t visits = 0;
   sv_vec_foreach(int, el, &vs) {
      (void)el;
      visits++;
   }
   sv_test_run(t, visits == 1);

   int64_t idx = -2;
   sv_vec_index_of_auto(&vs, 77, &idx);
   sv_test_run(t, idx == 0);

   sv_vec_remove_swap(&vs, 0, &sv_gpa);
   sv_test_run(t, vs.size == 0);

   sv_vec_deinit(&vs, &sv_gpa);
}

static inline void sv_test_vector_search(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   int ok = 0;
   int xs[] = { 5, 8, 13, 8, 21 };
   sv_vec_push_many(&vs, xs, 5, &ok, &sv_gpa);

   int64_t idx = 0;
   sv_vec_index_of_auto(&vs, 5, &idx);
   sv_test_run(t, idx == 0);
   sv_vec_index_of_auto(&vs, 13, &idx);
   sv_test_run(t, idx == 2);
   sv_vec_index_of_auto(&vs, 8, &idx);
   sv_test_run(t, idx == 1);
   sv_vec_index_of_auto(&vs, 99, &idx);
   sv_test_run(t, idx == -1);

   sv_vec_index_of(&vs, 21, sv_test_cmp_int, &idx);
   sv_test_run(t, idx == 4);

   int found = 0;
   sv_vec_is_in(&vs, 8, sv_test_cmp_int, &found);
   sv_test_run(t, found == 1);
   sv_vec_is_in(&vs, 9, sv_test_cmp_int, &found);
   sv_test_run(t, found == 0);

   int64_t count = 0;
   sv_vec_count_auto(&vs, 8, &count);
   sv_test_run(t, count == 2);
   sv_vec_count(&vs, 99, sv_test_cmp_int, &count);
   sv_test_run(t, count == 0);

   sv_vec_deinit(&vs, &sv_gpa);

   sv_vec_t(int) empty = sv_vec_init(int, &sv_gpa);
   sv_vec_index_of_auto(&empty, 1, &idx);
   sv_test_run(t, idx == -1);
   sv_vec_is_in_auto(&empty, 1, &found);
   sv_test_run(t, found == 0);
   sv_vec_count_auto(&empty, 1, &count);
   sv_test_run(t, count == 0);
   sv_vec_deinit(&empty, &sv_gpa);
}

static inline void sv_test_vector_foreach(sv_testing_t* t)
{
   sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
   int ok = 0;
   for (int i = 1; i <= 5; i++)
      sv_vec_push(&vs, i, &ok, &sv_gpa);

   int64_t sum = 0;
   sv_vec_foreach(int, el, &vs) {
      sum += el;
   }
   sv_test_run(t, sum == 15);
   sv_vec_deinit(&vs, &sv_gpa);

   sv_vec_t(int) empty = sv_vec_init(int, &sv_gpa);
   int64_t visits = 0;
   sv_vec_foreach(int, el, &empty) {
      (void)el;
      visits++;
   }
   sv_test_run(t, visits == 0);
   sv_vec_deinit(&empty, &sv_gpa);
}

static inline void sv_test_vector(sv_testing_t* t)
{
   sv_test_vector_init(t);
   sv_test_vector_push(t);
   sv_test_vector_push_many(t);
   sv_test_vector_boundary(t);
   sv_test_vector_remove(t);
   sv_test_vector_empty_and_reuse(t);
   sv_test_vector_search(t);
   sv_test_vector_foreach(t);
}

#endif
