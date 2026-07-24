#ifndef SV_STRING_H
#define SV_STRING_H

#include "vector.h"
#include <string.h>
#include <stdbool.h>

typedef struct {
   int64_t size;
   const char* chars;
} sv_str_t;

sv_vec_def(sv_str_t);

sv_vec_def(char);

typedef sv_vec_t(char) sv_str_builder;


/**
 * Adds the char array to the builder. Returns the amount of chars written, -1 on error.
 */
int sv_strb_add(sv_str_builder*, const char*, int64_t n, const sv_allocator_t*);

/**
 * Adds one char to the builder. Returns 1 on success, -1 on error.
 */
int sv_strb_add_char(sv_str_builder*, char, const sv_allocator_t*);

/**
 * Creates a string from the string builder.
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
sv_str_t sv_str_slice(const sv_str_t s, int64_t start, int64_t end);

/**
 * Returns a NULL-terminated copy of `s` allocated with `a`. The caller owns
 * the result and frees it with the same allocator.
 */
char* sv_str_to_c_str(const sv_str_t s, const sv_allocator_t* a);

/**
 * Returns the char at position `i`. A negative `i` counts from the end
 * (sv_str_at(s, -1) is the last char); an out of range `i` returns '\0'.
 */
char sv_str_at(const sv_str_t s, int64_t i);

/**
 * Returns true if both strings have the same size and bytes.
 */
bool sv_str_comp(const sv_str_t s1, const sv_str_t s2);

/**
 * Returns a copy of `string` allocated with `a`. The caller owns the result
 * and frees it with the same allocator. On failure, the returned string will have length -1
 * and `str` field NULL.
 */
sv_str_t sv_str_copy(const sv_str_t string, const sv_allocator_t* a);

/**
 * Returns the index of the first occurrence of `s2` in `s1`, or -1 if `s2`
 * does not occur.
 */
int64_t sv_str_index_of(const sv_str_t s1, const sv_str_t s2);

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


static const sv_str_t sv_str_empty = {
   .chars = "",
   .size = 0,
};

static const sv_str_t sv_str_error = {
   .chars = NULL,
   .size = -1,
};

int sv_strb_add(sv_str_builder* b, const char* c, int64_t n, const sv_allocator_t* a)
{
   int success;
   sv_vec_push_many(b, c, n, &success, a);
   if (success)
      return n;
   else
      return -1;
}

int sv_strb_add_char(sv_str_builder* b, char c, const sv_allocator_t* a)
{
   int success;
   sv_vec_push(b, c, &success, a);
   if (success)
      return 1;
   else
      return -1;
}

sv_str_t sv_strb_to_str(sv_str_builder* b)
{
   if (b->size == 0)
      return sv_str_empty;

   sv_str_t str = {
      .chars = b->arr,
      .size = b->size,
   };

   b->arr = NULL;
   b->size = 0;
   b->capacity = 0;

   return str;
}

sv_str_t sv_str_init(const char* c)
{
   int64_t len = (int64_t)strlen(c);
   return (sv_str_t){
      .chars = c,
      .size = len,
   };
}

void sv_str_deinit(sv_str_t* s, const sv_allocator_t* a)
{
   if (s->size > 0)
      sv_free(a, (void*)s->chars);
}

static int64_t sv_wrap_i(const sv_str_t s, int64_t i)
{
   return i >= 0 ? i : s.size + i;
}

sv_str_t sv_str_slice(const sv_str_t s, int64_t start, int64_t end)
{
   start = sv_wrap_i(s, start);
   if (start > s.size || start < 0)
      return sv_str_empty;

   end = sv_wrap_i(s, end);
   if (end < 0 || end < start)
      return sv_str_empty;

   int64_t d = end - start;
   if (d <= 0)
      return sv_str_empty;

   int64_t size = s.size > end ? d : s.size - start;
   return (sv_str_t){
      .chars = &s.chars[start],
      .size = size,
   };
}

char* sv_str_to_c_str(const sv_str_t s, const sv_allocator_t* a)
{
   char* c = sv_malloc(a, s.size + 1);
   if (c == NULL)
      return c;

   memcpy(c, s.chars, s.size);
   c[s.size] = '\0';
   return c;
}

char sv_str_at(const sv_str_t s, int64_t i)
{
   i = sv_wrap_i(s, i);
   if (i < 0 || i >= s.size)
      return '\0';
   return s.chars[i];
}

bool sv_str_comp(const sv_str_t s1, const sv_str_t s2)
{
   return s1.size == s2.size ?
      memcmp(s1.chars, s2.chars, s1.size) == 0
      : false;
}

sv_str_t sv_str_copy(const sv_str_t string, const sv_allocator_t* a)
{
   if (string.size == 0)
      return sv_str_empty;

   char* chars = sv_malloc(a, string.size);
   if (chars == NULL)
      return sv_str_error;

   memcpy(chars, string.chars, string.size);
   return (sv_str_t){
      .chars = chars,
      .size = string.size,
   };
}

int64_t sv_str_index_of(const sv_str_t s1, const sv_str_t s2)
{
   if (s2.size <= 0)
      return 0;

   if (s2.size > s1.size)
      return -1;

   for (int i=0; i < s1.size - s2.size + 1; i++) {
      if (memcmp(&s1.chars[i], s2.chars, s2.size) == 0) {
         return i;
      }
   }
   return -1;

}

bool sv_str_in(const sv_str_t s1, const sv_str_t s2)
{
   return sv_str_index_of(s1, s2) >= 0;
}

bool sv_str_cstr_in(const sv_str_t s1, const char* c)
{
   sv_str_t s2 = sv_str_init(c);
   return sv_str_index_of(s1, s2) >= 0;
}

sv_str_t sv_str_add(const sv_str_t s1, const sv_str_t s2, const sv_allocator_t* a)
{
   char* chars = sv_malloc(a, s1.size + s2.size);
   if (chars == NULL)
      return sv_str_error;

   memcpy(chars, s1.chars, s1.size);
   memcpy(&chars[s1.size], s2.chars, s2.size);
   return (sv_str_t){
      .chars = chars,
      .size = s1.size + s2.size,
   };
}

sv_vec_t(sv_str_t) sv_str_split(const sv_str_t s, const char* c, const sv_allocator_t* a)
{
   sv_vec_t(sv_str_t) v = sv_vec_init(sv_str_t, a);
   int64_t c_length = (int64_t)strlen(c);
   if (s.size < c_length || c_length == 0) {
      return v;
   }

   int success = 0;
   int64_t begining = 0;
   for (int64_t i = 0; i < s.size - c_length + 1; i++) {
      if (memcmp(&s.chars[i], c, c_length) == 0) {
         sv_str_t string = sv_str_slice(s, begining, i);
         sv_vec_push(&v, string, &success, a);
         if (success == 0)
            goto error;

         i += c_length - 1;
         begining = i + 1;
      }
   }
   if (begining != s.size) {
      sv_str_t string = sv_str_slice(s, begining, s.size);
      sv_vec_push(&v, string, &success, a);
      if (success == 0)
         goto error;
   }
   return v;

error:
   sv_vec_deinit(&v, a);
   return (sv_vec_t(sv_str_t))sv_vec_init(sv_str_t, a);
}

#endif
