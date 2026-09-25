#include "user_value.h"

#include "../value.h"
#include <string.h>

bool uv_eql(user_value_t v1, user_value_t v2)
{
    if (v1.tag != v2.tag || (strcmp(v1.name, v2.name) != 0))
        return false;

    return v1.vtable->eql_fn(v1.value, v2.value);
}

sv_opt_t(value_t) uv_field(user_value_t v, uint32_t index, const vm_ctx_t* ctx)
{ return v.vtable->get_field_fn(v.value, index, ctx); }

void uv_free(user_value_t v, const sv_allocator_t* a)
{ v.vtable->free_fn(v.value, a); }

sv_str_t uv_string(user_value_t v, const vm_ctx_t* ctx)
{ return v.vtable->to_string_fn(v.value, ctx); }

uint32_t uv_hash(user_value_t uv)
{ return uv.vtable->hash_fn(uv.value); }
