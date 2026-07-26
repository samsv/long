#ifndef SV_TESTS_LIST_H
#define SV_TESTS_LIST_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/obj/list.h"
#include "string.h"

static int sv_test_list_obj_frees = 0;

static void sv_test_list_free_obj(obj_t* o, const sv_allocator_t* a)
{
   sv_str_deinit(&o->str, a);
   sv_test_list_obj_frees++;
}

static value_t sv_test_list_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static value_t sv_test_list_obj(const char* text, const sv_allocator_t* a)
{
   obj_t o = { .kind = OBJ_STR, .str = sv_str_copy(sv_str_init(text), a) };
   sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, sv_test_list_free_obj, a);
   return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}

static inline void sv_test_list_init_get(sv_testing_t* t)
{
   value_t vals[] = { sv_test_list_num(1), sv_test_list_num(2), sv_test_list_num(3) };
   list_t l = ll_init(vals, 3, &sv_gpa);
   sv_test_run(t, l.cell != NULL);
   sv_test_run(t, ll_count(l) == 3);
   sv_test_run(t, ll_get(l, 0)->number == 1);
   sv_test_run(t, ll_get(l, 1)->number == 2);
   sv_test_run(t, ll_get(l, 2)->number == 3);
   sv_test_run(t, ll_get(l, 99) == NULL);
   sv_test_run(t, ll_head(l)->number == 1);
   ll_deinit(&l, &sv_gpa);

   sv_vec_t(value_t) vec = sv_vec_init(value_t);
   int success;
   sv_vec_push(&vec, sv_test_list_num(7), &success, &sv_gpa);
   sv_vec_push(&vec, sv_test_list_num(8), &success, &sv_gpa);
   list_t from_vec = ll_init_from_vec(vec, &sv_gpa);
   sv_test_run(t, ll_count(from_vec) == 2);
   sv_test_run(t, ll_get(from_vec, 1)->number == 7);
   sv_test_run(t, ll_get(from_vec, 0)->number == 8);
   ll_deinit(&from_vec, &sv_gpa);

   list_t empty = ll_empty();
   sv_test_run(t, ll_count(empty) == 0);
   sv_test_run(t, ll_head(empty) == NULL);
   ll_deinit(&empty, &sv_gpa);
}

static inline void sv_test_list_persistence(sv_testing_t* t)
{
   value_t vals[] = { sv_test_list_num(2), sv_test_list_num(3) };
   list_t l1 = ll_init(vals, 2, &sv_gpa);

   list_t l2 = ll_prepend(l1, sv_test_list_num(1), &sv_gpa);
   sv_test_run(t, l2.cell != NULL);
   sv_test_run(t, ll_count(l2) == 3);
   sv_test_run(t, ll_get(l2, 0)->number == 1);
   sv_test_run(t, ll_get(l2, 1)->number == 2);
   sv_test_run(t, ll_count(l1) == 2);
   sv_test_run(t, ll_get(l1, 0)->number == 2);

   ll_deinit(&l1, &sv_gpa);
   sv_test_run(t, ll_get(l2, 2)->number == 3);

   list_t l3 = ll_prepend(l2, sv_test_list_num(0), &sv_gpa);
   sv_test_run(t, ll_count(l3) == 4);
   ll_deinit(&l2, &sv_gpa);
   sv_test_run(t, ll_get(l3, 0)->number == 0);
   sv_test_run(t, ll_get(l3, 3)->number == 3);
   ll_deinit(&l3, &sv_gpa);

   list_t base = ll_empty();
   list_t one = ll_prepend(base, sv_test_list_num(9), &sv_gpa);
   sv_test_run(t, ll_count(one) == 1);
   sv_test_run(t, ll_head(one)->number == 9);
   ll_deinit(&base, &sv_gpa);
   ll_deinit(&one, &sv_gpa);

   value_t arr[] = { sv_test_list_num(1), sv_test_list_num(2) };
   value_t tail_vals[] = { sv_test_list_num(3) };
   list_t rest = ll_init(tail_vals, 1, &sv_gpa);
   list_t joined = ll_prepend_arr(rest, arr, 2, &sv_gpa);
   sv_test_run(t, ll_count(joined) == 3);
   sv_test_run(t, ll_get(joined, 0)->number == 1);
   sv_test_run(t, ll_get(joined, 1)->number == 2);
   sv_test_run(t, ll_get(joined, 2)->number == 3);
   sv_test_run(t, ll_count(rest) == 1);
   ll_deinit(&rest, &sv_gpa);
   ll_deinit(&joined, &sv_gpa);
}

static inline void sv_test_list_add(sv_testing_t* t)
{
   value_t left_vals[] = { sv_test_list_num(1), sv_test_list_num(2) };
   value_t right_vals[] = { sv_test_list_num(3), sv_test_list_num(4) };
   list_t left = ll_init(left_vals, 2, &sv_gpa);
   list_t right = ll_init(right_vals, 2, &sv_gpa);

   list_t both = ll_add(left, right, &sv_gpa);
   sv_test_run(t, ll_count(both) == 4);
   sv_test_run(t, ll_get(both, 0)->number == 1);
   sv_test_run(t, ll_get(both, 3)->number == 4);
   sv_test_run(t, ll_count(left) == 2);
   sv_test_run(t, ll_count(right) == 2);

   list_t empty = ll_empty();
   list_t left_only = ll_add(left, empty, &sv_gpa);
   sv_test_run(t, ll_count(left_only) == 2);
   list_t right_only = ll_add(empty, right, &sv_gpa);
   sv_test_run(t, ll_count(right_only) == 2);
   sv_test_run(t, ll_get(right_only, 1)->number == 4);

   ll_deinit(&left, &sv_gpa);
   ll_deinit(&right, &sv_gpa);
   ll_deinit(&both, &sv_gpa);
   ll_deinit(&empty, &sv_gpa);
   ll_deinit(&left_only, &sv_gpa);
   ll_deinit(&right_only, &sv_gpa);
}

static inline void sv_test_list_modify(sv_testing_t* t)
{
   value_t vals[] = { sv_test_list_num(1), sv_test_list_num(2), sv_test_list_num(3) };
   list_t l = ll_init(vals, 3, &sv_gpa);

   list_t inserted = ll_insert(l, sv_test_list_num(9), 1, &sv_gpa);
   sv_test_run(t, ll_count(inserted) == 4);
   sv_test_run(t, ll_get(inserted, 0)->number == 1);
   sv_test_run(t, ll_get(inserted, 1)->number == 9);
   sv_test_run(t, ll_get(inserted, 2)->number == 2);
   sv_test_run(t, ll_count(l) == 3);
   sv_test_run(t, ll_get(l, 1)->number == 2);
   ll_deinit(&inserted, &sv_gpa);

   list_t updated = ll_update(l, sv_test_list_num(9), 1, &sv_gpa);
   sv_test_run(t, ll_count(updated) == 3);
   sv_test_run(t, ll_get(updated, 1)->number == 9);
   sv_test_run(t, ll_get(l, 1)->number == 2);
   ll_deinit(&updated, &sv_gpa);

   list_t deleted = ll_delete_at(l, 1, &sv_gpa);
   sv_test_run(t, ll_count(deleted) == 2);
   sv_test_run(t, ll_get(deleted, 0)->number == 1);
   sv_test_run(t, ll_get(deleted, 1)->number == 3);
   sv_test_run(t, ll_count(l) == 3);
   ll_deinit(&deleted, &sv_gpa);

   value_t single[] = { sv_test_list_num(5) };
   list_t one = ll_init(single, 1, &sv_gpa);
   list_t none = ll_delete_at(one, 0, &sv_gpa);
   sv_test_run(t, none.cell != NULL);
   sv_test_run(t, ll_count(none) == 0);
   sv_test_run(t, ll_count(one) == 1);
   ll_deinit(&one, &sv_gpa);
   ll_deinit(&none, &sv_gpa);

   ll_deinit(&l, &sv_gpa);
}

static inline void sv_test_list_head_tail(sv_testing_t* t)
{
   value_t vals[] = { sv_test_list_num(1), sv_test_list_num(2), sv_test_list_num(3) };
   list_t l = ll_init(vals, 3, &sv_gpa);

   list_t rest = ll_tail(l);
   sv_test_run(t, ll_count(rest) == 2);
   sv_test_run(t, ll_head(rest)->number == 2);
   sv_test_run(t, ll_count(l) == 3);

   list_t rest2 = ll_tail(rest);
   sv_test_run(t, ll_count(rest2) == 1);
   sv_test_run(t, ll_head(rest2)->number == 3);

   list_t none = ll_tail(rest2);
   sv_test_run(t, none.cell != NULL);
   sv_test_run(t, ll_count(none) == 0);
   sv_test_run(t, ll_head(none) == NULL);

   ll_deinit(&l, &sv_gpa);
   ll_deinit(&rest, &sv_gpa);
   ll_deinit(&rest2, &sv_gpa);
   ll_deinit(&none, &sv_gpa);
}

static inline void sv_test_list_values(sv_testing_t* t)
{
   sv_test_list_obj_frees = 0;

   value_t v = sv_test_list_obj("shared", &sv_gpa);
   sv_test_run(t, v.obj.cell->count == 1);

   list_t l = ll_init(&v, 1, &sv_gpa);
   sv_test_run(t, v.obj.cell->count == 2);
   sv_test_run(t, ll_get(l, 0)->obj.cell == v.obj.cell);
   sv_test_run(t, sv_str_comp(ll_get(l, 0)->obj.cell->value.str, sv_str_init("shared")));

   list_t l2 = ll_prepend(l, sv_test_list_num(1), &sv_gpa);
   sv_test_run(t, v.obj.cell->count >= 2);

   list_t replaced = ll_update(l, sv_test_list_num(0), 0, &sv_gpa);
   sv_test_run(t, ll_get(replaced, 0)->kind == VALUE_NUMBER);
   ll_deinit(&replaced, &sv_gpa);

   ll_deinit(&l2, &sv_gpa);
   ll_deinit(&l, &sv_gpa);
   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_list_obj_frees == 0);

   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_list_obj_frees == 1);
}

static inline void sv_test_list_oom(sv_testing_t* t)
{
   sv_test_list_obj_frees = 0;

   value_t nums[] = { sv_test_list_num(1), sv_test_list_num(2) };
   list_t failed = ll_init(nums, 2, &sv_test_fail_alloc);
   sv_test_run(t, failed.cell == NULL);
   ll_deinit(&failed, &sv_test_fail_alloc);

   value_t good_vals[] = { sv_test_list_num(1) };
   list_t good = ll_init(good_vals, 1, &sv_gpa);
   list_t bad_prepend = ll_prepend(good, sv_test_list_num(2), &sv_test_fail_alloc);
   sv_test_run(t, bad_prepend.cell == NULL);
   sv_test_run(t, ll_count(good) == 1);
   ll_deinit(&good, &sv_gpa);

   value_t v = sv_test_list_obj("unwound", &sv_gpa);
   sv_test_countdown_t counter = { .remaining = 1 };
   sv_allocator_t countdown = {
      .vtable = &sv_test_countdown_vtable,
      .self = &counter,
   };
   list_t partial = ll_init(&v, 1, &countdown);
   sv_test_run(t, partial.cell == NULL);
   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_list_obj_frees == 0);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_list_obj_frees == 1);
}

static inline void sv_test_list(sv_testing_t* t)
{
   sv_test_list_init_get(t);
   /**
   sv_test_list_persistence(t);
   sv_test_list_add(t);
   sv_test_list_modify(t);
   sv_test_list_head_tail(t);
   sv_test_list_values(t);
   sv_test_list_oom(t);
   */
}

#endif
