#ifndef SV_ALLOCATOR_STD_H
#define SV_ALLOCATOR_STD_H

#include "allocator.h"
#include <stddef.h>

extern const sv_allocator_t sv_gpa;

#ifdef SV_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>
void* sv_malloc_gpa(void* self, size_t size)
{
    (void)self;
    return malloc(size);
}

void sv_free_gpa(void* self, void* ptr)
{
    (void)self;
    free(ptr);
}

void* sv_realloc_gpa(void* self, void* ptr, size_t size)
{
    (void)self;
    return realloc(ptr, size);
}

static const sv_allocator_vtable sv_gpa_vtable = {
    .malloc = sv_malloc_gpa,
    .free = sv_free_gpa,
    .realloc = sv_realloc_gpa,
};

const sv_allocator_t sv_gpa = {
    .vtable = &sv_gpa_vtable,
    .self = NULL,
};

#endif
#endif
