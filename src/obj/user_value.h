#ifndef LONG_USER_VALUE_H
#define LONG_USER_VALUE_H

#include "../common.h"
#include "../std/allocator.h"
#include "../std/string.h"

typedef struct {
    // true if two values are equal
    bool (*eql_fn)(void*, void*);
    // free the value
    void (*free_fn)(void*, const sv_allocator_t*);
    // get field, must return a borrowed value
    sv_opt_t(value_t) (*get_field_fn)(void*, uint32_t index, const vm_ctx_t*);
    // string representation for printing, heap allocated
    sv_str_t (*to_string_fn)(void*, const vm_ctx_t*);
    // hash function for hashmaps
    uint32_t (*hash_fn)(void*);
} user_value_vtable_t;

typedef struct user_value_t {
    void* value;
    uint32_t tag;
    const char* name;
    const user_value_vtable_t* vtable;
} user_value_t;

bool uv_eql(user_value_t, user_value_t);
sv_opt_t(value_t) uv_field(user_value_t, uint32_t index, const vm_ctx_t*);
void uv_free(user_value_t, const sv_allocator_t*);
sv_str_t uv_string(user_value_t, const vm_ctx_t*);
uint32_t uv_hash(user_value_t);

#endif
