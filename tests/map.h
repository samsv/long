#ifndef SV_TESTS_MAP_H
#define SV_TESTS_MAP_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/obj/map.h"
#include "../src/obj.h"
#include "string.h"

static int sv_test_map_obj_frees = 0;

static inline void sv_test_map_free_obj(obj_t* o, const sv_allocator_t* a)
{
   sv_str_deinit(&o->str, a);
   sv_test_map_obj_frees++;
}

static inline value_t sv_test_map_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline value_t sv_test_map_obj(const char* text, const sv_allocator_t* a)
{
   obj_t o = { .kind = OBJ_STR, .str = sv_str_copy(sv_str_init(text), a) };
   sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, sv_test_map_free_obj, a);
   return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}

static inline kv_t sv_test_map_kv(double k, double v)
{
   return (kv_t){ .key = sv_test_map_num(k), .value = sv_test_map_num(v) };
}

static inline double sv_test_map_get_num(hashmap_t m, double k)
{
   sv_opt_t(value_t) r = map_get(m, sv_test_map_num(k));
   return r.is_some ? r.value.number : -1;
}

static inline hashmap_t sv_test_map_big(int64_t n, const sv_allocator_t* a)
{
   hashmap_t m = map_init(NULL, 0, a);
   for (int64_t i = 0; i < n; i++) {
      hashmap_t next = map_put(m, sv_test_map_kv((double)i, (double)i), a);
      map_deinit(&m, a);
      m = next;
   }
   return m;
}

static inline void sv_test_map_init_get(sv_testing_t* t)
{
   value_t kvs[] = {
      sv_test_map_num(1), sv_test_map_num(10),
      sv_test_map_num(2), sv_test_map_num(20),
      sv_test_map_num(3), sv_test_map_num(30),
   };
   hashmap_t m = map_init(kvs, 6, &sv_gpa);
   sv_test_run(t, m.cell != NULL);
   sv_test_run(t, map_count(m) == 3);
   sv_test_run(t, sv_test_map_get_num(m, 1) == 10);
   sv_test_run(t, sv_test_map_get_num(m, 2) == 20);
   sv_test_run(t, sv_test_map_get_num(m, 3) == 30);
   sv_test_run(t, !map_get(m, sv_test_map_num(99)).is_some);
   map_deinit(&m, &sv_gpa);

   value_t edge_kvs[] = {
      sv_test_map_num(-7), sv_test_map_num(70),
      sv_test_map_num(2.5), sv_test_map_num(25),
      sv_test_map_num(0), sv_test_map_num(1),
   };
   hashmap_t edges = map_init(edge_kvs, 6, &sv_gpa);
   sv_test_run(t, sv_test_map_get_num(edges, -7) == 70);
   sv_test_run(t, sv_test_map_get_num(edges, 2.5) == 25);
   sv_test_run(t, sv_test_map_get_num(edges, -0.0) == 1);
   map_deinit(&edges, &sv_gpa);

   hashmap_t empty = map_init(NULL, 0, &sv_gpa);
   sv_test_run(t, empty.cell != NULL);
   sv_test_run(t, map_count(empty) == 0);
   sv_test_run(t, !map_get(empty, sv_test_map_num(1)).is_some);
   map_deinit(&empty, &sv_gpa);
}

static inline void sv_test_map_put(sv_testing_t* t)
{
   hashmap_t m0 = map_init(NULL, 0, &sv_gpa);
   hashmap_t m1 = map_put(m0, sv_test_map_kv(1, 10), &sv_gpa);
   sv_test_run(t, m1.cell != NULL);
   sv_test_run(t, !map_get(m0, sv_test_map_num(1)).is_some);
   sv_test_run(t, sv_test_map_get_num(m1, 1) == 10);
   sv_test_run(t, map_count(m1) == 1);

   hashmap_t m2 = map_put(m1, sv_test_map_kv(1, 99), &sv_gpa);
   sv_test_run(t, sv_test_map_get_num(m1, 1) == 10);
   sv_test_run(t, sv_test_map_get_num(m2, 1) == 99);
   sv_test_run(t, map_count(m2) == 1);

   hashmap_t big = sv_rc_borrow(m2);
   int ok = 1;
   for (int64_t i = 0; i < 40; i++) {
      hashmap_t next = map_put(big, sv_test_map_kv((double)(i + 100), (double)i), &sv_gpa);
      ok = ok && next.cell != NULL;
      map_deinit(&big, &sv_gpa);
      big = next;
   }
   sv_test_run(t, ok);
   ok = 1;
   for (int64_t i = 0; i < 40; i++)
      ok = ok && sv_test_map_get_num(big, (double)(i + 100)) == (double)i;
   sv_test_run(t, ok);
   sv_test_run(t, map_count(big) == 41);
   sv_test_run(t, sv_test_map_get_num(big, 1) == 99);
   sv_test_run(t, sv_test_map_get_num(m1, 1) == 10);

   map_deinit(&m0, &sv_gpa);
   map_deinit(&m1, &sv_gpa);
   map_deinit(&m2, &sv_gpa);
   map_deinit(&big, &sv_gpa);
}

static inline void sv_test_map_fork(sv_testing_t* t)
{
   value_t base[] = {
      sv_test_map_num(1), sv_test_map_num(1),
      sv_test_map_num(2), sv_test_map_num(2),
   };
   hashmap_t parent = map_init(base, 4, &sv_gpa);

   hashmap_t a = map_put(parent, sv_test_map_kv(10, 100), &sv_gpa);
   hashmap_t b = map_put(parent, sv_test_map_kv(20, 200), &sv_gpa);
   sv_test_run(t, sv_test_map_get_num(a, 10) == 100);
   sv_test_run(t, sv_test_map_get_num(a, 1) == 1);
   sv_test_run(t, !map_get(a, sv_test_map_num(20)).is_some);
   sv_test_run(t, sv_test_map_get_num(b, 20) == 200);
   sv_test_run(t, sv_test_map_get_num(b, 2) == 2);
   sv_test_run(t, !map_get(b, sv_test_map_num(10)).is_some);
   sv_test_run(t, map_count(parent) == 2);

   hashmap_t grown = map_put(parent, sv_test_map_kv(3, 3), &sv_gpa);
   hashmap_t forked = map_put(parent, sv_test_map_kv(1, 30), &sv_gpa);
   sv_test_run(t, !map_get(forked, sv_test_map_num(3)).is_some);
   sv_test_run(t, sv_test_map_get_num(forked, 1) == 30);
   sv_test_run(t, map_count(forked) == 2);
   sv_test_run(t, sv_test_map_get_num(grown, 3) == 3);
   sv_test_run(t, sv_test_map_get_num(parent, 1) == 1);

   map_deinit(&parent, &sv_gpa);
   map_deinit(&a, &sv_gpa);
   map_deinit(&b, &sv_gpa);
   map_deinit(&grown, &sv_gpa);
   map_deinit(&forked, &sv_gpa);
}

static inline void sv_test_map_fork_update(sv_testing_t* t)
{
   value_t base[] = { sv_test_map_num(1), sv_test_map_num(10) };
   hashmap_t m0 = map_init(base, 2, &sv_gpa);
   hashmap_t m1 = map_put(m0, sv_test_map_kv(1, 11), &sv_gpa);
   hashmap_t m2 = map_put(m1, sv_test_map_kv(2, 20), &sv_gpa);
   hashmap_t m3 = map_put(m0, sv_test_map_kv(3, 30), &sv_gpa);
   hashmap_t m4 = map_put(m3, sv_test_map_kv(2, 99), &sv_gpa);

   sv_test_run(t, map_count(m2) == 2);
   sv_test_run(t, sv_test_map_get_num(m2, 1) == 11);
   sv_test_run(t, sv_test_map_get_num(m2, 2) == 20);
   sv_test_run(t, !map_get(m2, sv_test_map_num(3)).is_some);

   sv_test_run(t, map_count(m4) == 3);
   sv_test_run(t, sv_test_map_get_num(m4, 1) == 10);
   sv_test_run(t, sv_test_map_get_num(m4, 2) == 99);
   sv_test_run(t, sv_test_map_get_num(m4, 3) == 30);

   sv_test_run(t, map_count(m0) == 1 && sv_test_map_get_num(m0, 1) == 10);
   sv_test_run(t, map_count(m1) == 1 && sv_test_map_get_num(m1, 1) == 11);
   sv_test_run(t, map_count(m3) == 2 && !map_get(m3, sv_test_map_num(2)).is_some);

   hashmap_t maps[] = { m2, m4 };
   for (int i = 0; i < 2; i++) {
      map_iter_t it = map_iter_init_no_borrow(maps[i]);
      for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it))
         sv_test_run(t, sv_test_map_get_num(maps[i], kv.value.key.number) == kv.value.value.number);
   }

   map_deinit(&m0, &sv_gpa);
   map_deinit(&m1, &sv_gpa);
   map_deinit(&m2, &sv_gpa);
   map_deinit(&m3, &sv_gpa);
   map_deinit(&m4, &sv_gpa);
}

static inline void sv_test_map_delete(sv_testing_t* t)
{
   value_t kvs[] = {
      sv_test_map_num(1), sv_test_map_num(1),
      sv_test_map_num(2), sv_test_map_num(2),
      sv_test_map_num(3), sv_test_map_num(3),
   };
   hashmap_t m = map_init(kvs, 6, &sv_gpa);

   hashmap_t d = map_delete(m, sv_test_map_num(2), &sv_gpa);
   sv_test_run(t, d.cell != NULL);
   sv_test_run(t, !map_get(d, sv_test_map_num(2)).is_some);
   sv_test_run(t, sv_test_map_get_num(d, 1) == 1);
   sv_test_run(t, sv_test_map_get_num(d, 3) == 3);
   sv_test_run(t, map_count(d) == 2);
   sv_test_run(t, sv_test_map_get_num(m, 2) == 2);

   hashmap_t miss = map_delete(m, sv_test_map_num(99), &sv_gpa);
   sv_test_run(t, miss.cell != NULL);
   sv_test_run(t, map_count(miss) == 3);

   map_deinit(&m, &sv_gpa);
   map_deinit(&d, &sv_gpa);
   map_deinit(&miss, &sv_gpa);

   hashmap_t big = sv_test_map_big(40, &sv_gpa);
   hashmap_t chained = map_put(big, sv_test_map_kv(0, 999), &sv_gpa);
   hashmap_t del = map_delete(chained, sv_test_map_num(1), &sv_gpa);
   sv_test_run(t, !map_get(del, sv_test_map_num(1)).is_some);
   sv_test_run(t, sv_test_map_get_num(del, 2) == 2);
   sv_test_run(t, sv_test_map_get_num(del, 0) == 999);
   sv_test_run(t, map_count(big) == 40);
   sv_test_run(t, map_count(chained) == 40);
   sv_test_run(t, map_count(del) == 39);

   map_deinit(&big, &sv_gpa);
   map_deinit(&chained, &sv_gpa);
   map_deinit(&del, &sv_gpa);
}

static inline void sv_test_map_count_flat(sv_testing_t* t)
{
   hashmap_t m0 = map_init(NULL, 0, &sv_gpa);
   hashmap_t m1 = map_put(m0, sv_test_map_kv(1, 10), &sv_gpa);
   hashmap_t m2 = map_put(m1, sv_test_map_kv(2, 20), &sv_gpa);
   hashmap_t m3 = map_put(m2, sv_test_map_kv(1, 99), &sv_gpa);
   hashmap_t m4 = map_delete(m3, sv_test_map_num(1), &sv_gpa);
   hashmap_t m5 = map_delete(m4, sv_test_map_num(12345), &sv_gpa);

   sv_test_run(t, map_count(m0) == 0);
   sv_test_run(t, map_count(m1) == 1);
   sv_test_run(t, map_count(m2) == 2);
   sv_test_run(t, map_count(m3) == 2);
   sv_test_run(t, map_count(m4) == 1);
   sv_test_run(t, map_count(m5) == 1);

   map_deinit(&m0, &sv_gpa);
   map_deinit(&m1, &sv_gpa);
   map_deinit(&m2, &sv_gpa);
   map_deinit(&m3, &sv_gpa);
   map_deinit(&m4, &sv_gpa);
   map_deinit(&m5, &sv_gpa);
}

static inline void sv_test_map_flatten(sv_testing_t* t)
{
   hashmap_t m = sv_test_map_big(40, &sv_gpa);
   sv_test_run(t, m.cell->value.depth == 0);

   const int8_t expected[] = { 1, 2, 3, 4, 5, 0, 1, 2, 3 };
   int ok = 1;
   for (int64_t i = 0; i < 9; i++) {
      hashmap_t next = map_put(m, sv_test_map_kv(0, (double)(1000 + i)), &sv_gpa);
      map_deinit(&m, &sv_gpa);
      m = next;
      ok = ok && m.cell->value.depth == expected[i];
      ok = ok && sv_test_map_get_num(m, 0) == (double)(1000 + i);
      ok = ok && sv_test_map_get_num(m, 39) == 39;
   }
   sv_test_run(t, ok);
   map_deinit(&m, &sv_gpa);
}

static inline void sv_test_map_iter(sv_testing_t* t)
{
   value_t kvs[] = {
      sv_test_map_num(1), sv_test_map_num(10),
      sv_test_map_num(2), sv_test_map_num(20),
   };
   hashmap_t m = map_init(kvs, 4, &sv_gpa);
   map_iter_t it = map_iter_init(m);
   map_deinit(&m, &sv_gpa);

   int64_t seen = 0;
   double key_sum = 0;
   double val_sum = 0;
   for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
      key_sum += kv.value.key.number;
      val_sum += kv.value.value.number;
      seen++;
   }
   map_iter_deinit(&it, &sv_gpa);
   sv_test_run(t, seen == 2);
   sv_test_run(t, key_sum == 3);
   sv_test_run(t, val_sum == 30);

   hashmap_t big = sv_test_map_big(40, &sv_gpa);
   hashmap_t layered = map_put(big, sv_test_map_kv(5, 200), &sv_gpa);
   hashmap_t pruned = map_delete(layered, sv_test_map_num(7), &sv_gpa);

   map_iter_t it2 = map_iter_init(pruned);
   seen = 0;
   int found_overwritten = 0;
   int found_deleted = 0;
   for (sv_opt_t(kv_t) kv = map_iter_next(&it2); kv.is_some; kv = map_iter_next(&it2)) {
      if (kv.value.key.number == 5) found_overwritten = kv.value.value.number == 200;
      if (kv.value.key.number == 7) found_deleted = 1;
      seen++;
   }
   map_iter_deinit(&it2, &sv_gpa);
   sv_test_run(t, seen == 39);
   sv_test_run(t, found_overwritten);
   sv_test_run(t, !found_deleted);

   map_deinit(&big, &sv_gpa);
   map_deinit(&layered, &sv_gpa);
   map_deinit(&pruned, &sv_gpa);
}

static inline void sv_test_map_values(sv_testing_t* t)
{
   sv_test_map_obj_frees = 0;

   value_t k = sv_test_map_obj("key", &sv_gpa);
   value_t v = sv_test_map_obj("shared", &sv_gpa);
   sv_test_run(t, v.obj.cell->count == 1);

   value_t kv[] = { k, v };
   hashmap_t m = map_init(kv, 2, &sv_gpa);
   sv_test_run(t, k.obj.cell->count == 2);
   sv_test_run(t, v.obj.cell->count == 2);

   sv_opt_t(value_t) got = map_get(m, k);
   sv_test_run(t, got.is_some && got.value.obj.cell == v.obj.cell);
   sv_test_run(t, v.obj.cell->count == 2);

   hashmap_t m2 = map_put(m, (kv_t){ .key = k, .value = sv_test_map_num(0) }, &sv_gpa);
   sv_test_run(t, map_get(m2, k).value.kind == VALUE_NUMBER);
   sv_test_run(t, map_get(m, k).value.kind == VALUE_OBJ);

   map_deinit(&m2, &sv_gpa);
   map_deinit(&m, &sv_gpa);
   sv_test_run(t, k.obj.cell->count == 1);
   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_map_obj_frees == 0);
   sv_rc_deinit(&k.obj, &sv_gpa);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_map_obj_frees == 2);
}

static inline void sv_test_map_oom(sv_testing_t* t)
{
   sv_test_map_obj_frees = 0;

   value_t kvs[] = {
      sv_test_map_num(1), sv_test_map_num(1),
      sv_test_map_num(2), sv_test_map_num(2),
   };
   hashmap_t failed = map_init(kvs, 4, &sv_test_fail_alloc);
   sv_test_run(t, failed.cell == NULL);

   hashmap_t good = map_init(kvs, 4, &sv_gpa);
   hashmap_t bad_put = map_put(good, sv_test_map_kv(3, 3), &sv_test_fail_alloc);
   sv_test_run(t, bad_put.cell == NULL);
   sv_test_run(t, map_count(good) == 2);
   hashmap_t bad_del = map_delete(good, sv_test_map_num(1), &sv_test_fail_alloc);
   sv_test_run(t, bad_del.cell == NULL);
   sv_test_run(t, map_count(good) == 2);
   map_deinit(&good, &sv_gpa);

   value_t v = sv_test_map_obj("unwound", &sv_gpa);
   for (int64_t remaining = 1; remaining < 8; remaining++) {
      sv_test_countdown_t counter = { .remaining = remaining };
      sv_allocator_t countdown = {
         .vtable = &sv_test_countdown_vtable,
         .self = &counter,
      };
      value_t kv[] = { sv_test_map_num(1), v };
      hashmap_t partial = map_init(kv, 2, &countdown);
      if (partial.cell != NULL)
         map_deinit(&partial, &countdown);
   }

   value_t upd_kvs[] = { sv_test_map_num(1), v, sv_test_map_num(2), sv_test_map_num(2) };
   hashmap_t upd = map_init(upd_kvs, 4, &sv_gpa);
   for (int64_t remaining = 1; remaining < 6; remaining++) {
      sv_test_countdown_t counter = { .remaining = remaining };
      sv_allocator_t countdown = {
         .vtable = &sv_test_countdown_vtable,
         .self = &counter,
      };
      hashmap_t updated = map_put(upd, sv_test_map_kv(2, 5), &countdown);
      if (updated.cell != NULL)
         map_deinit(&updated, &countdown);
   }
   map_deinit(&upd, &sv_gpa);
   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_map_obj_frees == 0);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_map_obj_frees == 1);
}

static inline void sv_test_map_transient(sv_testing_t* t)
{
   transient_hashmap_t tm = thm_init(4, &sv_gpa);
   sv_test_run(t, tm.set.store.cell != NULL);

   sv_test_map_obj_frees = 0;
   for (double k = 1; k <= 3; k++) {
      char name[4] = { 'k', 'e', (char)('0' + (int)k), '\0' };
      value_t key = sv_test_map_obj(name, &sv_gpa);
      kv_t kv = { .key = key, .value = sv_test_map_num(k * 10) };
      sv_test_run(t, thm_put(&tm, kv, &sv_gpa));
      value_free(&key, &sv_gpa);
   }
   sv_test_run(t, thm_count(tm) == 3);

   value_t k2 = sv_test_map_obj("ke2", &sv_gpa);
   sv_opt_t(value_t) got = thm_get(tm, k2);
   sv_test_run(t, got.is_some && got.value.number == 20);

   kv_t upd = { .key = k2, .value = sv_test_map_num(99) };
   sv_test_run(t, thm_put(&tm, upd, &sv_gpa));
   got = thm_get(tm, k2);
   sv_test_run(t, got.is_some && got.value.number == 99);
   sv_test_run(t, thm_count(tm) == 3);

   sv_test_run(t, thm_delete(&tm, k2, &sv_gpa));
   sv_test_run(t, !thm_get(tm, k2).is_some);
   sv_test_run(t, thm_count(tm) == 2);
   sv_test_run(t, !thm_delete(&tm, sv_test_map_num(404), &sv_gpa));

   kv_t re = { .key = k2, .value = sv_test_map_num(7) };
   sv_test_run(t, thm_put(&tm, re, &sv_gpa));
   got = thm_get(tm, k2);
   sv_test_run(t, got.is_some && got.value.number == 7);
   sv_test_run(t, thm_count(tm) == 3);
   value_free(&k2, &sv_gpa);

   for (double k = 0; k < 100; k++)
      sv_test_run(t, thm_put(&tm, sv_test_map_kv(1000 + k, k), &sv_gpa));
   sv_test_run(t, thm_count(tm) == 103);
   for (double k = 0; k < 100; k++) {
      sv_opt_t(value_t) v = thm_get(tm, sv_test_map_num(1000 + k));
      sv_test_run(t, v.is_some && v.value.number == k);
   }

   hashmap_t persisted = transient_to_map(&tm, &sv_gpa);
   sv_test_run(t, persisted.cell != NULL);
   sv_test_run(t, tm.set.store.cell == NULL);
   sv_test_run(t, map_count(persisted) == 103);
   sv_test_run(t, sv_test_map_get_num(persisted, 1042) == 42);

   transient_hashmap_t back = map_to_transient(&persisted, &sv_gpa);
   sv_test_run(t, back.set.store.cell != NULL);
   sv_test_run(t, persisted.cell == NULL);
   sv_test_run(t, thm_put(&back, sv_test_map_kv(5000, 1), &sv_gpa));
   sv_test_run(t, thm_count(back) == 104);

   hashmap_t shared = transient_to_map(&back, &sv_gpa);
   hashmap_t borrow = sv_rc_borrow(shared);
   transient_hashmap_t denied = map_to_transient(&shared, &sv_gpa);
   sv_test_run(t, denied.set.store.cell == NULL);
   sv_test_run(t, shared.cell != NULL);
   sv_test_run(t, map_count(shared) == 104);
   map_deinit(&borrow, &sv_gpa);
   map_deinit(&shared, &sv_gpa);

   value_t base[] = {
      sv_test_map_num(1), sv_test_map_num(1),
      sv_test_map_num(2), sv_test_map_num(2),
   };
   hashmap_t v0 = map_init(base, 4, &sv_gpa);
   hashmap_t v1 = map_put(v0, sv_test_map_kv(3, 3), &sv_gpa);
   transient_hashmap_t flat = map_to_transient(&v1, &sv_gpa);
   sv_test_run(t, flat.set.store.cell != NULL);
   sv_test_run(t, thm_count(flat) == 3);
   sv_test_run(t, thm_get(flat, sv_test_map_num(3)).is_some);
   sv_test_run(t, map_count(v0) == 2);
   sv_test_run(t, !map_get(v0, sv_test_map_num(3)).is_some);
   thm_deinit(&flat, &sv_gpa);
   map_deinit(&v0, &sv_gpa);
}

static inline void sv_test_map(sv_testing_t* t)
{
   sv_test_map_init_get(t);
   sv_test_map_put(t);
   sv_test_map_fork(t);
   sv_test_map_fork_update(t);
   sv_test_map_delete(t);
   sv_test_map_count_flat(t);
   sv_test_map_flatten(t);
   sv_test_map_iter(t);
   sv_test_map_values(t);
   sv_test_map_oom(t);
   sv_test_map_transient(t);
}

#endif
