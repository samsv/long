#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/compiler.h"
#include "src/debug.h"
#include "src/std/allocator_std.h"

static inline value_t sv_test_map_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline kv_t sv_test_map_kv(double k, double v)
{
   return (kv_t){ .key = sv_test_map_num(k), .value = sv_test_map_num(v) };
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

int main(void)
{
    hashmap_t big = sv_test_map_big(40, &sv_gpa);
    hashmap_t chained = map_put(big, sv_test_map_kv(0, 999), &sv_gpa);
    hashmap_t del = map_delete(chained, sv_test_map_num(1), &sv_gpa);
    printf("%d\n", map_get(del, sv_test_map_num(1)).is_some);

    map_deinit(&big, &sv_gpa);
    map_deinit(&chained, &sv_gpa);
    map_deinit(&del, &sv_gpa);
}
