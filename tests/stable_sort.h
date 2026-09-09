#ifndef SV_TESTS_STABLE_SORT_H
#define SV_TESTS_STABLE_SORT_H

#include "../src/std/test.h"
#include "../src/stable_sort.h"

typedef struct {
   int key;
   int seq;
} sv_test_sort_pair_t;

static inline int sv_test_sort_int_cmp(const void* a, const void* b)
{
   const int* x = a;
   const int* y = b;
   return (*x > *y) - (*x < *y);
}

static inline int sv_test_sort_pair_cmp(const void* a, const void* b)
{
   const sv_test_sort_pair_t* x = a;
   const sv_test_sort_pair_t* y = b;
   return (x->key > y->key) - (x->key < y->key);
}

static inline void sv_test_stable_sort(sv_testing_t* t)
{
   int small[] = { 9, 7, 5, 3, 1, 8, 6, 4, 2, 0 };
   stable_sort(small, 10, sizeof(int), sv_test_sort_int_cmp);
   bool small_ok = true;
   for (int i = 0; i < 10; i++)
      small_ok = small_ok && small[i] == i;
   sv_test_run(t, small_ok);

   stable_sort(small, 0, sizeof(int), sv_test_sort_int_cmp);
   stable_sort(small, 1, sizeof(int), sv_test_sort_int_cmp);
   sv_test_run(t, small[0] == 0);

   int big[100];
   for (int i = 0; i < 100; i++)
      big[i] = (i * 37 + 11) % 100;
   stable_sort(big, 100, sizeof(int), sv_test_sort_int_cmp);
   bool big_ok = true;
   for (int i = 0; i < 100; i++)
      big_ok = big_ok && big[i] == i;
   sv_test_run(t, big_ok);

   int boundary[17];
   for (int i = 0; i < 17; i++)
      boundary[i] = 16 - i;
   stable_sort(boundary, 17, sizeof(int), sv_test_sort_int_cmp);
   bool boundary_ok = true;
   for (int i = 0; i < 17; i++)
      boundary_ok = boundary_ok && boundary[i] == i;
   sv_test_run(t, boundary_ok);

   sv_test_sort_pair_t pairs[40];
   for (int i = 0; i < 40; i++)
      pairs[i] = (sv_test_sort_pair_t){ .key = (i * 7 + 3) % 5, .seq = i };
   stable_sort(pairs, 40, sizeof(sv_test_sort_pair_t), sv_test_sort_pair_cmp);
   bool stable = true;
   for (int i = 1; i < 40; i++) {
      stable = stable && pairs[i - 1].key <= pairs[i].key;
      if (pairs[i - 1].key == pairs[i].key)
         stable = stable && pairs[i - 1].seq < pairs[i].seq;
   }
   sv_test_run(t, stable);
}

#endif
