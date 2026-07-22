#ifndef SV_STRING_H
#define SV_STRING_H

#include "vector.h"
#include <stdbool.h>

typedef struct {
   const char* str;
   size_t size;
} sv_str_t;

sv_vec_def(sv_str_t);

sv_vec_def(char);

typedef sv_vec_t(char) sv_str_builder;


/**
 * Adds the char array to the builder. Returns the amount of chars written, -1 on error.
 */
int sv_strb_add(sv_str_builder*, const char*, const sv_allocator_t*);

/**
 * Adds one char to the builder. Returns 1 on success, -1 on error.
 */
int sv_strb_add_char(sv_str_builder*, char, const sv_allocator_t*);

/**
 * Creates a string from the string builder and invalidates it.
 */
sv_str_t sv_strb_to_str(sv_str_builder*);

/**
 * Creates a string view over the NULL-terminated array `c`.
 */
sv_str_t sv_str_init(const char* c);

/**
 * Frees the string character array. Must only be used when the string owns the char array.
 */
void sv_str_deinit(sv_str_t* s, const sv_allocator_t* a);

/**
 * Returns the slice of `s` between `start` and `end`. If `end` is smaller
 * than `start` an empty string is returned. The slice is a view into `s`
 * and is only valid while `s`'s storage lives.
 */
sv_str_t sv_str_slice(const sv_str_t s, size_t start, size_t end);

/**
 * Returns a NULL-terminated copy of `s` allocated with `a`. The caller owns
 * the result and frees it with the same allocator.
 */
char* sv_str_to_c_str(const sv_str_t s, const sv_allocator_t* a);

/**
 * Returns the char at position `i`. A negative `i` counts from the end
 * (sv_str_at(s, -1) is the last char); an out of range `i` returns '\0'.
 */
char sv_str_at(const sv_str_t s, int i);

/**
 * Returns true if both strings have the same size and bytes.
 */
bool sv_str_comp(const sv_str_t s1, const sv_str_t s2);

/**
 * Returns a copy of `string` allocated with `a`. The caller owns the result
 * and frees it with the same allocator.
 */
sv_str_t sv_str_copy(const sv_str_t string, const sv_allocator_t* a);

/**
 * Returns the index of the first occurrence of `s2` in `s1`, or -1 if `s2`
 * does not occur.
 */
int sv_str_index_of(const sv_str_t s1, const sv_str_t s2);

/**
 * Returns true if `s2` occurs in `s1`.
 */
bool sv_str_in(const sv_str_t s1, const sv_str_t s2);

/**
 * Returns true if the NULL-terminated array `c` occurs in `s1`.
 */
bool sv_str_cstr_in(const sv_str_t s1, const char* c);

/**
 * Returns the concatenation of `s1` and `s2`, allocated with `a`. The caller
 * owns the result and frees it with the same allocator.
 */
sv_str_t sv_str_add(const sv_str_t s1, const sv_str_t s2, const sv_allocator_t* a);

/**
 * Returns a vector of the substrings of `s` delimited by the separator `c`.
 * The substrings are views into `s`.
 */
sv_vec_t(sv_str_t) sv_str_split(const sv_str_t s, const char* c, const sv_allocator_t* a);

#endif
