#ifndef SV_VECTOR_H
#define SV_VECTOR_H

/**
 * A type safe vector implementation for C.
 *
 * Example usage:
 ```
    #define SV_IMPLEMENTATION // define vector and allocator implementations.
    #include "vector.h"
    #include "allocator_std.h"

    // define the vector type before using it.
    sv_vec_def(int64_t);

    int main(void) {
        /// append ///
        sv_vec_t(int64_t) vs = sv_vec_init(int64_t, &sv_gpa);

        for (int64_t i = 0; i < 10; i++) {
            int success; // pass NULL if you do not wish to check for the error code.
            sv_vec_push(&vs, i, &success, &sv_gpa);
            if (!success) {
                printf("Could not insert data int64_to vector\n");
                exit(1);
            }
        }

        int64_t xs[] = {1, 2, 3, 4, 16, 15, 1021, 415};
        sv_vec_push_many(&v, xs, 8, NULL, &sv_gpa);

        /// loops ///
        // normal loop
        for (int64_t i = 0; i < vs.size; i++) {
            printf("%d\n", vs.arr[i]);
        }
        // foreach
        sv_vec_foreach(int64_t, el, &vs) {
            printf("%d\n", el);
        }

        /// free vector ///
        sv_vec_deinit(&vs, &sv_gpa);
    }
 ```
 */

#include <stddef.h>
#include <stdint.h>
#include "allocator.h"

#ifndef VEC_DEFAULT_CAP
#define VEC_DEFAULT_CAP 8
#endif

#define sv_vec_tt(type) type ## _vec
#define sv_vec_t(type) sv_vec_tt(type)

/**
 * Defines a new vector of type `type`. Must be called only once before using
 * the vector.
 */
#define sv_vec_def(type) typedef struct {                                                                     \
    int64_t size;                                                                                             \
    int64_t capacity;                                                                                         \
    type* arr;                                                                                                \
    size_t element_size;                                                                                      \
} sv_vec_t(type)

/**
 * Creates a new vector of the given type with default capacity.
 * The default capacity can be changed by setting `VEC_DEFAULT_CAP`
 * before importing `vector.h`.
 */
#define sv_vec_init(type) {                                                                                   \
    .arr = NULL,                                                                                              \
    .size = 0,                                                                                                \
    .capacity = 0,                                                                                            \
    .element_size = sizeof(type),                                                                             \
}

/**
 * Creates a new vector of the given type and capacity.
 * arr == NULL when allocation fails for capacity > 0.
 */
#define sv_vec_init_capacity(type, mcapacity, allocator) {                                                    \
    .arr = sv_malloc(allocator, sizeof(type) * mcapacity),                                                    \
    .size = 0,                                                                                                \
    .capacity = mcapacity,                                                                                    \
    .element_size = sizeof(type),                                                                             \
}

/**
 * Returns the element at index i.
 */
#define sv_vec_at(vec, i) (vec).arr[i]
/**
 * Sets the element at index i.
 */
#define sv_vec_set_at(vec, data, i) (vec)->arr[i] = data

/**
 * Inserts the given data at then end of the vector.
 */
#define sv_vec_push(vec, data, success, allocator) do {                                                       \
    int* vec_var_line(_vec_success) = (success);                                                              \
    char* res = sv_vec_allocate_one(                                                                          \
        (void*)&(vec)->arr, &(vec)->size, &(vec)->capacity,                                                   \
        (vec)->element_size, allocator);                                                                      \
    if (res == NULL) { if (vec_var_line(_vec_success) )*vec_var_line(_vec_success) = 0; break; }              \
    (vec)->arr[(res - (char*)(vec)->arr) / (vec)->element_size] = data;                                       \
    if (vec_var_line(_vec_success)) *vec_var_line(_vec_success) = 1;                                          \
} while (0)

/**
 * Inserts the given data array at then end of the vector.
 */
#define sv_vec_push_many(vec, values, amount, success, allocator) do {                                        \
    int* vec_var_line(_vec_success) = (success);                                                              \
    char* loc = sv_vec_allocate_many(                                                                         \
        (void*)&(vec)->arr, &(vec)->size, &(vec)->capacity,                                                   \
        (vec)->element_size, amount, allocator);                                                              \
    if (loc == NULL) { if (vec_var_line(_vec_success) )*vec_var_line(_vec_success) = 0; break; }              \
    int64_t idx = (loc - (char*)(vec)->arr) / (vec)->element_size;                                            \
    for (int64_t i = idx; i < (vec)->size; i++) (vec)->arr[i] = values[i - idx];                              \
    if (vec_var_line(_vec_success)) *vec_var_line(_vec_success) = 1;                                          \
} while (0)

/**
 * Removes the given element by replacing it with the last element of the array.
 * This has a constant O(1) time.
 */
#define sv_vec_remove_swap(vec, i, allocator) do {                                                            \
    (vec)->arr[i] = (vec)->arr[(vec)->size - 1];                                                              \
    (vec)->size -= 1;                                                                                         \
    if ((vec)->size <= (vec)->capacity / 2 && (vec)->size) sv_vec_realloc(                                    \
        (void*)&(vec)->arr, &(vec)->capacity,                                                                 \
        (vec)->capacity / 2, (vec)->element_size, allocator);                                                 \
} while (0)

/**
 * Removes the given element by shifting left all array members.
 * This has a O(N) time.
 */
#define sv_vec_remove_linear(vec, i, allocator) do {                                                          \
    for (int64_t idx = i; idx < (vec)->size - 1; idx++) (vec)->arr[idx] = (vec)->arr[idx + 1];                \
    (vec)->size -= 1;                                                                                         \
    if ((vec)->size <= (vec)->capacity / 2 && (vec)->size) sv_vec_realloc(                                    \
        (void*)&(vec)->arr, &(vec)->capacity,                                                                 \
        (vec)->capacity / 2, (vec)->element_size, allocator);                                                 \
} while (0)

/**
 * Checks if the item is in the given vector using the given compare function.
 */
#define sv_vec_is_in(vec, item, cmp_fn, result) do {                                                          \
    (*(result)) = 0;                                                                                          \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( cmp_fn((vec)->arr[vec_var_line(_i)], item) ) { (*(result)) = 1; break; };                       \
    }                                                                                                         \
} while (0)

/**
 * Checks if the item is in the given vector using the equality operator.
 */
#define sv_vec_is_in_auto(vec, item, result) do {                                                             \
    (*(result)) = 0;                                                                                          \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( (vec)->arr[vec_var_line(_i)] == item ) { (*(result)) = 1; break; };                             \
    }                                                                                                         \
} while (0)

/**
 * Checks if the item is in the given vector using the given compare function.
 */
#define sv_vec_index_of(vec, item, cmp_fn, result) do {                                                       \
    (*(result)) = -1;                                                                                         \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( cmp_fn((vec)->arr[vec_var_line(_i)], item) ) { (*(result)) = vec_var_line(_i); break; };        \
    }                                                                                                         \
} while (0)

/**
 * Checks if the item is in the given vector using the equality operator.
 */
#define sv_vec_index_of_auto(vec, item, result) do {                                                          \
    (*(result)) = -1;                                                                                         \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( (vec)->arr[vec_var_line(_i)] == item ) { (*(result)) = vec_var_line(_i); break; };              \
    }                                                                                                         \
} while (0)

/**
 * Counts how many times the item is in the given vector using the given compare function.
 */
#define sv_vec_count(vec, item, cmp_fn, count) do {                                                           \
    (*(count)) = 0;                                                                                           \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( cmp_fn((vec)->arr[vec_var_line(_i)], item) ) (*(count))++;                                      \
    }                                                                                                         \
} while (0)

/**
 * Counts how many times the item is in the given vector using the equality operator.
 */
#define sv_vec_count_auto(vec, item, count) do {                                                              \
    (*(count)) = 0;                                                                                           \
    for (int64_t vec_var_line(_i) = 0; vec_var_line(_i) < (vec)->size; vec_var_line(_i)++) {                  \
         if ( (vec)->arr[vec_var_line(_i)] == item ) (*(count))++;                                            \
    }                                                                                                         \
} while (0)

#define sv_vec_foreach(itemtype, item, vec)                                                                   \
    for (itemtype* vec_var_line(_p) = (vec)->arr, item;                                                       \
          (vec_var_line(_p) < &((vec)->arr[(vec)->size])) && (item = *vec_var_line(_p), 1) ;                  \
          vec_var_line(_p)++)

#define sv_vec_deinit(vec, allocator) sv_free(allocator, (vec)->arr)

#define vec_concat_macro_(a, b) a ## b
#define vec_concat_macro(a, b) vec_concat_macro_(a, b)
#define vec_var_line(name) vec_concat_macro(name, __LINE__)

void* sv_vec_realloc(
    void** arr,
    int64_t* capacity,
    const int64_t new_capacity,
    const int64_t element_size,
    const sv_allocator_t* a
);

/**
 * Returns the location of the array where an element can be set.
 * May reallocate the array if needed.
 */
void* sv_vec_allocate_one(
    void** arr,
    int64_t* size,
    int64_t* capacity,
    const int64_t element_size,
    const sv_allocator_t* a
);

/**
 * Returns the location of the array where `n` elements can be set.
 * May reallocate the array if needed.
 */
void* sv_vec_allocate_many(
    void** arr,
    int64_t* size,
    int64_t* capacity,
    const int64_t element_size,
    int64_t n,
    const sv_allocator_t* a
);

#ifdef SV_IMPLEMENTATION
/**
 * Reallocates the given array to the given size.
 */

#define GROW_CAPACITY(capacity) ((capacity) == 0 ? VEC_DEFAULT_CAP : (capacity) * 2)

void* sv_vec_realloc(
    void** arr,
    int64_t* capacity,
    const int64_t new_capacity,
    const int64_t element_size,
    const sv_allocator_t* a
) {
    void* new_arr = sv_realloc(a, *arr, element_size * new_capacity);
    if (new_arr == NULL) {
        return NULL;
    }
    *arr = new_arr;
    *capacity = new_capacity;
    return *arr;
}

/**
 * Returns the location of the array where an element can be set.
 * May reallocate the array if needed.
 */
void* sv_vec_allocate_one(
    void** arr,
    int64_t* size,
    int64_t* capacity,
    const int64_t element_size,
    const sv_allocator_t* a
) {
    if (*size + 1 >= *capacity) {
        void* new_arr = sv_vec_realloc(arr, capacity, GROW_CAPACITY(*capacity), element_size, a);
        if (new_arr == NULL) {
            return NULL;
        }
    }

    void* loc = (char*)(*arr) + (*size * element_size);
    *size += 1;
    return loc;
}

/**
 * Returns the location of the array where `n` elements can be set.
 * May reallocate the array if needed.
 */
void* sv_vec_allocate_many(
    void** arr,
    int64_t* size,
    int64_t* capacity,
    const int64_t element_size,
    int64_t n,
    const sv_allocator_t* a
) {
    if (n < 0)
        return NULL;

    while (*size + n >= *capacity) {
        void* new_arr = sv_vec_realloc(arr, capacity, GROW_CAPACITY(*capacity), element_size, a);
        if (new_arr == NULL) {
            return NULL;
        }
    }

    void* loc = (char*)(*arr) + (*size * element_size);
    *size += n;
    return loc;
}

#undef GROW_CAPACITY

#endif
#endif
