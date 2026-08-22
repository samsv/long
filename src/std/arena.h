#ifndef ARENA_H
#define ARENA_H

#include "allocator.h"
#include <stddef.h>

typedef struct sv_arena_t sv_arena_t;
/**
 * Bump allocator over a chain of blocks.
 */
struct sv_arena_t {
    char* data;
    size_t capacity;
    size_t count;

    sv_arena_t* next;
};

/**
 * Initializes the arena. Returns a zeroed arena on failure.
 */
sv_arena_t sv_arena_init(size_t capacity);

/**
 * Frees every block and zeroes the head.
 */
void sv_arena_deinit(sv_arena_t*);

/**
 * Returns storage for `amount` bytes, aligned for any type. A request larger
 * than the arena `capacity` allocates its own individual block. Returns NULL on failure.
 */
void* sv_arena_malloc(sv_arena_t*, const size_t amount);

/**
 * As sv_arena_malloc, zeroed.
 */
void* sv_arena_calloc(sv_arena_t*, const size_t size);

/**
 * Wraps the arena in an allocator, borrowing it.
 */
sv_allocator_t sv_arena_allocator_init(sv_arena_t*);

/**
 * Frees the arena.
 */
void sv_arena_allocator_deinit(sv_allocator_t*);

/**
 * Prints each block's capacity, fill and successor. Debugging aid.
 */
void arena_print(sv_arena_t* a);

#ifdef SV_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ALIGN_SIZE _Alignof(max_align_t)

/**
 * Rounds `x` up to a multiple of `align`, which must be a power of two.
 */
static size_t align_forward(size_t x, const size_t align) {
    return (x + align - 1) & ~(align - 1);
}

void arena_print(sv_arena_t* a) {
    sv_arena_t* current = a;
    while (current) {
        printf(
            "capacity: %lu, count: %lu, next_ptr: %p\n",
            current->capacity,
            current->count,
            (void*)current->next
        );
        current = current->next;
    }
}

void sv_arena_deinit(sv_arena_t* a)
{
    sv_arena_t* current = a->next;
    sv_arena_t* next = NULL;

    while (current != NULL) {
        next = current->next;
        free(current->data);
        free(current);
        current = next;
    }

    free(a->data);
    a->next = NULL;
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

sv_arena_t sv_arena_init(size_t capacity)
{
    char* data = malloc(capacity);
    if (data == NULL) {
        return (sv_arena_t){0};
    }

    sv_arena_t a = (sv_arena_t) {
        .data = data,
        .capacity = capacity,
        .count = 0,
        .next = NULL,
    };

    return a;
}

static void* sv_arena_malloc_aligned(sv_arena_t* a, const size_t size) {
    size_t default_capacity = a->capacity;
    while (a->capacity - a->count < size) {
        if (a->next) {
            a = a->next;
            continue;
        }

        sv_arena_t* next_ptr = malloc(sizeof(sv_arena_t));
        if (next_ptr == NULL)
            return NULL;
        *next_ptr = sv_arena_init(default_capacity > size ? default_capacity : size);
        if (next_ptr->data == NULL) {
            free(next_ptr);
            return NULL;
        }

        a->next = next_ptr;
        a = next_ptr;
    }

    size_t start = a->count;
    a->count += size;
    return &a->data[start];
}

void* sv_arena_malloc(sv_arena_t* a, const size_t amount)
{
    size_t size = align_forward(amount, ALIGN_SIZE);
    return sv_arena_malloc_aligned(a, size);
}

void* sv_arena_allocator_malloc(void* self, const size_t amount)
{
    return sv_arena_malloc((sv_arena_t*)self, amount);
}

void* sv_arena_calloc(sv_arena_t* a, const size_t size)
{
    size_t aligned_size = align_forward(size, ALIGN_SIZE);
    void* ptr = sv_arena_malloc_aligned(a, aligned_size);
    if (ptr == NULL) {
        return ptr;
    }

    memset(ptr, 0, aligned_size);
    return ptr;
}

void sv_arena_allocator_free(void* self, void* ptr)
{
    (void)self;
    (void)ptr;
}

void* sv_arena_allocator_realloc(void* self, void* ptr, size_t size)
{
    if (ptr == NULL)
        return sv_arena_malloc((sv_arena_t*)self, size);

    sv_arena_t* a = self;
    void* new_ptr = sv_arena_malloc(a, size);
    if (new_ptr == NULL) {
        return new_ptr;
    }

    // search for pointer and check how much we can copy to the new pointer
    sv_arena_t* current = a;
    while(current != NULL) {
        if (
            (uintptr_t)current->data <= (uintptr_t)ptr
            && (uintptr_t)ptr < (uintptr_t)(current->data + current->capacity)
        ) {
            size_t offset = (uintptr_t)ptr - (uintptr_t)current->data;
            size_t diff = current->capacity - offset;
            size_t copy_size = diff > size ? size : diff;
            memmove(new_ptr, ptr, copy_size);
            return new_ptr;
        }
        current = current->next;
    }

    return NULL;
}


static const sv_allocator_vtable sv_arena_vtable = {
    .malloc = sv_arena_allocator_malloc,
    .free = sv_arena_allocator_free,
    .realloc = sv_arena_allocator_realloc,
};

sv_allocator_t sv_arena_allocator_init(sv_arena_t* a)
{
    return (sv_allocator_t){
        .vtable = &sv_arena_vtable,
        .self = (void*)a,
    };
}

void sv_arena_allocator_deinit(sv_allocator_t* a)
{
    sv_arena_t* arena = a->self;
    sv_arena_deinit(arena);
}
#endif
#endif

