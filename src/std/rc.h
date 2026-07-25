#ifndef SV_RC_H
#define SV_RC_H

#include "allocator.h"
#include <stdint.h>

#define sv_rc_tt(type) rc_ ## type ##_t
#define sv_rc_t(type) sv_rc_tt(type)

#define sv_rc_cell_tt(type) cell_ ## type
#define sv_rc_cell_t(type) sv_rc_cell_tt(type)

#define sv_rc_def(type) typedef struct {                                                                      \
   type value;                                                                                                \
   int64_t count;                                                                                             \
   void(*free_fn)(type*, const sv_allocator_t*);                                                              \
} sv_rc_cell_t(type);                                                                                         \
                                                                                                              \
typedef struct {                                                                                              \
   sv_rc_cell_t(type)* cell;                                                                                  \
} sv_rc_t(type)

#define rc_init(type, v, free_func, a) {                                                                      \
   .cell = rc_alloc_cell(                                                                                     \
      &(sv_rc_cell_t(type)){.value = v, .count = 1, .free_fn = free_func},                                    \
      sizeof(sv_rc_cell_t(type)),                                                                             \
      (a)                                                                                                     \
   )                                                                                                          \
}

#define rc_borrow(rc) ((rc).cell != NULL ? (rc).cell->count++ : 0, (rc))
#define rc_get(rc) ((rc).cell != NULL ? &(rc).cell->value : NULL)

#define rc_deinit(rc, a) do {                                                                                 \
   if ((rc)->cell == NULL) break;                                                                             \
   (rc)->cell->count--;                                                                                       \
   if ((rc)->cell->count == 0) {                                                                              \
      (rc)->cell->free_fn(&(rc)->cell->value, (a));                                                           \
      sv_free((a), (rc)->cell);                                                                               \
   }                                                                                                          \
   (rc)->cell = NULL;                                                                                         \
} while (0)

void* rc_alloc_cell(void* src, size_t size, const sv_allocator_t* a);

#ifdef SV_IMPLEMENTATION
#include <string.h>

void* rc_alloc_cell(void* src, size_t size, const sv_allocator_t* a)
{
   void* dst = sv_malloc(a, size);
   if (dst == NULL)
      return NULL;

   return memcpy(dst, src, size);
}

#endif
#endif
