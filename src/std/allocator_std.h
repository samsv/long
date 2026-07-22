#ifndef SV_ALLOCATOR_STD_H
#define SV_ALLOCATOR_STD_H

#include "allocator.h"
#include <stddef.h>

extern sv_allocator_t sv_gpa;

#ifdef SV_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>
void* sv_malloc_gpa(void* ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

void sv_free_gpa(void* ctx, void* ptr)
{
    (void)ctx;
    free(ptr);
}

void* sv_realloc_gpa(void* ctx, void* ptr, size_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

sv_allocator_t sv_gpa = {
    .malloc = sv_malloc_gpa,
    .free = sv_free_gpa,
    .realloc = sv_realloc_gpa,
};

#endif
#endif
