#ifndef SV_ALLOCATOR_H
#define SV_ALLOCATOR_H

#include <stddef.h>

typedef void* (*sv_malloc_fn)(void*, size_t);
typedef void (*sv_free_fn)(void*, void*);
typedef void* (*sv_realloc_fn)(void*, void*, size_t);

typedef struct {
   sv_malloc_fn malloc;
   sv_free_fn free;
   sv_realloc_fn realloc;
   void* ctx;
} sv_allocator_t;

#define sv_malloc(allocator, size) (allocator)->malloc((allocator)->ctx, size)
#define sv_free(allocator, ptr) (allocator)->free((allocator)->ctx, ptr)
#define sv_realloc(allocator, ptr, size) (allocator)->realloc((allocator)->ctx, ptr, size)

#endif
