#ifndef SV_TESTS_STRING_H
#define SV_TESTS_STRING_H

#include <string.h>
#include <stdlib.h>
#include "../src/std/test.h"
#include "../src/std/string.h"
#include "../src/std/allocator_std.h"

static void* sv_test_fail_malloc(void* self, size_t size)
{
   (void)self;
   (void)size;
   return NULL;
}

static void* sv_test_fail_realloc(void* self, void* ptr, size_t size)
{
   (void)self;
   (void)ptr;
   (void)size;
   return NULL;
}

static void sv_test_fail_free(void* self, void* ptr)
{
   (void)self;
   (void)ptr;
}

static const sv_allocator_vtable sv_test_fail_vtable = {
   .malloc = sv_test_fail_malloc,
   .free = sv_test_fail_free,
   .realloc = sv_test_fail_realloc,
};

static const sv_allocator_t sv_test_fail_alloc = {
   .vtable = &sv_test_fail_vtable,
   .self = NULL,
};

typedef struct {
   int64_t remaining;
} sv_test_countdown_t;

static void* sv_test_countdown_malloc(void* self, size_t size)
{
   sv_test_countdown_t* c = self;
   if (c->remaining <= 0)
      return NULL;
   c->remaining--;
   return malloc(size);
}

static void* sv_test_countdown_realloc(void* self, void* ptr, size_t size)
{
   sv_test_countdown_t* c = self;
   if (c->remaining <= 0)
      return NULL;
   c->remaining--;
   return realloc(ptr, size);
}

static void sv_test_countdown_free(void* self, void* ptr)
{
   (void)self;
   free(ptr);
}

static const sv_allocator_vtable sv_test_countdown_vtable = {
   .malloc = sv_test_countdown_malloc,
   .free = sv_test_countdown_free,
   .realloc = sv_test_countdown_realloc,
};

static inline void sv_test_string_views(sv_testing_t* t)
{
   sv_str_t s = sv_str_init("hello");
   sv_test_run(t, s.size == 5);
   sv_test_run(t, s.chars != NULL);

   sv_str_t empty = sv_str_init("");
   sv_test_run(t, empty.size == 0);

   sv_test_run(t, sv_str_at(s, 0) == 'h');
   sv_test_run(t, sv_str_at(s, 4) == 'o');
   sv_test_run(t, sv_str_at(s, -1) == 'o');
   sv_test_run(t, sv_str_at(s, -5) == 'h');
   sv_test_run(t, sv_str_at(s, 5) == '\0');
   sv_test_run(t, sv_str_at(s, -6) == '\0');
   sv_test_run(t, sv_str_at(empty, 0) == '\0');
   sv_test_run(t, sv_str_at(empty, -1) == '\0');

   sv_str_t mid = sv_str_slice(s, 1, 4);
   sv_test_run(t, mid.size == 3);
   sv_test_run(t, sv_str_comp(mid, sv_str_init("ell")));

   sv_test_run(t, sv_str_comp(sv_str_slice(s, 0, 5), s));
   sv_test_run(t, sv_str_comp(sv_str_slice(s, 1, 99), sv_str_init("ello")));
   sv_test_run(t, sv_str_slice(s, 3, 1).size == 0);
   sv_test_run(t, sv_str_slice(s, 2, 2).size == 0);
   sv_test_run(t, sv_str_slice(s, 99, 100).size == 0);
   sv_test_run(t, sv_str_slice(empty, 0, 1).size == 0);

   sv_test_run(t, sv_str_comp(sv_str_slice(s, 0, -1), sv_str_init("hell")));
   sv_test_run(t, sv_str_comp(sv_str_slice(s, -3, -1), sv_str_init("ll")));
}

static inline void sv_test_string_comp(sv_testing_t* t)
{
   sv_test_run(t, sv_str_comp(sv_str_init("abc"), sv_str_init("abc")));
   sv_test_run(t, !sv_str_comp(sv_str_init("abc"), sv_str_init("abd")));
   sv_test_run(t, !sv_str_comp(sv_str_init("abc"), sv_str_init("ab")));
   sv_test_run(t, sv_str_comp(sv_str_init(""), sv_str_init("")));
   sv_test_run(t, !sv_str_comp(sv_str_init(""), sv_str_init("a")));
}

static inline void sv_test_string_copy(sv_testing_t* t)
{
   sv_str_t src = sv_str_init("copy me");
   sv_str_t dup = sv_str_copy(src, &sv_gpa);
   sv_test_run(t, sv_str_comp(dup, src));
   sv_test_run(t, dup.chars != src.chars);
   sv_str_deinit(&dup, &sv_gpa);

   sv_str_t empty = sv_str_copy(sv_str_init(""), &sv_gpa);
   sv_test_run(t, empty.size == 0);
   sv_str_deinit(&empty, &sv_gpa);
}

static inline void sv_test_string_to_c_str(sv_testing_t* t)
{
   sv_str_t s = sv_str_init("hello");
   char* c = sv_str_to_c_str(sv_str_slice(s, 1, 4), &sv_gpa);
   sv_test_run(t, c != NULL);
   sv_test_run(t, strcmp(c, "ell") == 0);
   sv_free(&sv_gpa, c);

   char* e = sv_str_to_c_str(sv_str_init(""), &sv_gpa);
   sv_test_run(t, e != NULL);
   sv_test_run(t, strcmp(e, "") == 0);
   sv_free(&sv_gpa, e);
}

static inline void sv_test_string_search(sv_testing_t* t)
{
   sv_str_t s = sv_str_init("the cat sat on the mat");

   sv_test_run(t, sv_str_index_of(s, sv_str_init("the")) == 0);
   sv_test_run(t, sv_str_index_of(s, sv_str_init("sat")) == 8);
   sv_test_run(t, sv_str_index_of(s, sv_str_init("dog")) == -1);
   sv_test_run(t, sv_str_index_of(s, s) == 0);
   sv_test_run(t, sv_str_index_of(sv_str_init("ab"), sv_str_init("abc")) == -1);
   sv_test_run(t, sv_str_index_of(s, sv_str_init("")) == 0);
   sv_test_run(t, sv_str_index_of(sv_str_init(""), sv_str_init("a")) == -1);

   sv_test_run(t, sv_str_in(s, sv_str_init("cat")));
   sv_test_run(t, !sv_str_in(s, sv_str_init("bat")));
   sv_test_run(t, sv_str_cstr_in(s, "mat"));
   sv_test_run(t, !sv_str_cstr_in(s, "hat"));
}

static inline void sv_test_string_add(sv_testing_t* t)
{
   sv_str_t joined = sv_str_add(sv_str_init("foo"), sv_str_init("bar"), &sv_gpa);
   sv_test_run(t, joined.size == 6);
   sv_test_run(t, sv_str_comp(joined, sv_str_init("foobar")));
   sv_str_deinit(&joined, &sv_gpa);

   sv_str_t left = sv_str_add(sv_str_init(""), sv_str_init("x"), &sv_gpa);
   sv_test_run(t, sv_str_comp(left, sv_str_init("x")));
   sv_str_deinit(&left, &sv_gpa);
}

static inline void sv_test_string_split(sv_testing_t* t)
{
   sv_str_t s = sv_str_init("a,b,c");
   sv_vec_t(sv_str_t) v = sv_str_split(s, ",", &sv_gpa);
   sv_test_run(t, v.size == 3);
   sv_test_run(t, sv_str_comp(sv_vec_at(v, 0), sv_str_init("a")));
   sv_test_run(t, sv_str_comp(sv_vec_at(v, 1), sv_str_init("b")));
   sv_test_run(t, sv_str_comp(sv_vec_at(v, 2), sv_str_init("c")));
   sv_vec_deinit(&v, &sv_gpa);

   sv_vec_t(sv_str_t) lead = sv_str_split(sv_str_init(",a"), ",", &sv_gpa);
   sv_test_run(t, lead.size == 2);
   sv_test_run(t, sv_vec_at(lead, 0).size == 0);
   sv_test_run(t, sv_str_comp(sv_vec_at(lead, 1), sv_str_init("a")));
   sv_vec_deinit(&lead, &sv_gpa);

   sv_vec_t(sv_str_t) adj = sv_str_split(sv_str_init("a,,b"), ",", &sv_gpa);
   sv_test_run(t, adj.size == 3);
   sv_test_run(t, sv_vec_at(adj, 1).size == 0);
   sv_vec_deinit(&adj, &sv_gpa);

   sv_vec_t(sv_str_t) trail = sv_str_split(sv_str_init("a,"), ",", &sv_gpa);
   sv_test_run(t, trail.size == 1);
   sv_test_run(t, sv_str_comp(sv_vec_at(trail, 0), sv_str_init("a")));
   sv_vec_deinit(&trail, &sv_gpa);

   sv_vec_t(sv_str_t) none = sv_str_split(sv_str_init("abc"), ",", &sv_gpa);
   sv_test_run(t, none.size == 1);
   sv_test_run(t, sv_str_comp(sv_vec_at(none, 0), sv_str_init("abc")));
   sv_vec_deinit(&none, &sv_gpa);

   sv_vec_t(sv_str_t) multi = sv_str_split(sv_str_init("a--b--c"), "--", &sv_gpa);
   sv_test_run(t, multi.size == 3);
   sv_test_run(t, sv_str_comp(sv_vec_at(multi, 2), sv_str_init("c")));
   sv_vec_deinit(&multi, &sv_gpa);

   sv_vec_t(sv_str_t) only = sv_str_split(sv_str_init(","), ",", &sv_gpa);
   sv_test_run(t, only.size == 1);
   sv_test_run(t, sv_vec_at(only, 0).size == 0);
   sv_vec_deinit(&only, &sv_gpa);

   sv_vec_t(sv_str_t) esep = sv_str_split(sv_str_init("abc"), "", &sv_gpa);
   sv_test_run(t, esep.size == 0);
   sv_vec_deinit(&esep, &sv_gpa);

   sv_vec_t(sv_str_t) shorter = sv_str_split(sv_str_init("a"), "--", &sv_gpa);
   sv_test_run(t, shorter.size == 0);
   sv_vec_deinit(&shorter, &sv_gpa);
}

static inline void sv_test_string_builder(sv_testing_t* t)
{
   sv_str_builder b = sv_vec_init(char);
   sv_test_run(t, sv_strb_add_char(&b, 'h', &sv_gpa) == 1);
   sv_test_run(t, sv_strb_add(&b, "ello", 4, &sv_gpa) == 4);

   sv_str_t s = sv_strb_to_str(&b);
   sv_test_run(t, s.size == 5);
   sv_test_run(t, sv_str_comp(s, sv_str_init("hello")));
   sv_test_run(t, b.arr == NULL);
   sv_test_run(t, b.size == 0);
   sv_test_run(t, b.capacity == 0);

   sv_test_run(t, sv_strb_add(&b, "again", 5, &sv_gpa) == 5);
   sv_str_t s2 = sv_strb_to_str(&b);
   sv_test_run(t, sv_str_comp(s2, sv_str_init("again")));
   sv_test_run(t, !sv_str_comp(s, s2));

   sv_str_deinit(&s, &sv_gpa);
   sv_str_deinit(&s2, &sv_gpa);

   sv_str_builder e = sv_vec_init(char);
   sv_str_t es = sv_strb_to_str(&e);
   sv_test_run(t, es.size == 0);
   sv_vec_deinit(&e, &sv_gpa);
}

static inline void sv_test_string_errors(sv_testing_t* t)
{
   sv_str_t src = sv_str_init("payload");

   sv_str_t bad_copy = sv_str_copy(src, &sv_test_fail_alloc);
   sv_test_run(t, bad_copy.size == -1);
   sv_test_run(t, bad_copy.chars == NULL);
   sv_str_deinit(&bad_copy, &sv_test_fail_alloc);

   sv_str_t bad_add = sv_str_add(src, src, &sv_test_fail_alloc);
   sv_test_run(t, bad_add.size == -1);
   sv_test_run(t, bad_add.chars == NULL);

   sv_test_run(t, sv_str_to_c_str(src, &sv_test_fail_alloc) == NULL);

   sv_str_builder b = sv_vec_init(char);
   sv_test_run(t, sv_strb_add_char(&b, 'x', &sv_test_fail_alloc) == -1);
   sv_test_run(t, sv_strb_add(&b, "xy", 2, &sv_test_fail_alloc) == -1);
   sv_test_run(t, b.size == 0);

   sv_vec_t(sv_str_t) bad_split = sv_str_split(sv_str_init("a,b"), ",", &sv_test_fail_alloc);
   sv_test_run(t, bad_split.size == 0);
   sv_test_run(t, bad_split.arr == NULL);

   sv_test_countdown_t counter = { .remaining = 1 };
   sv_allocator_t countdown = {
      .vtable = &sv_test_countdown_vtable,
      .self = &counter,
   };
   sv_vec_t(sv_str_t) partial = sv_str_split(sv_str_init("a,b,c,d,e,f,g,h,i,j"), ",", &countdown);
   sv_test_run(t, partial.size == 0);
   sv_test_run(t, partial.arr == NULL);
}

static inline void sv_test_string(sv_testing_t* t)
{
   sv_test_string_views(t);
   sv_test_string_comp(t);
   sv_test_string_copy(t);
   sv_test_string_to_c_str(t);
   sv_test_string_search(t);
   sv_test_string_add(t);
   sv_test_string_split(t);
   sv_test_string_builder(t);
   sv_test_string_errors(t);
}

#endif
