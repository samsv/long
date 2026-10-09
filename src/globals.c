#include "globals.h"
#include "obj.h"
#include <inttypes.h>

sv_opt_t(uint32_t) names_get(transient_hashmap_t names, sv_str_t id, const sv_allocator_t* a)
{
    if (names.set.store.cell == NULL)
        return sv_opt_none_t(uint32_t);

    value_t key = value_init_str(id, a);
    if (key.obj.cell == NULL)
        return sv_opt_none_t(uint32_t);

    sv_opt_t(value_t) v = thm_get(names, key);
    value_free(&key, a);
    if (!v.is_some)
        return sv_opt_none_t(uint32_t);
    return sv_opt_some_t(uint32_t, (uint32_t)v.value.number);
}

void check_limit(ctx_t* ctx, int64_t n, uint32_t max, const char* what, int64_t line)
{
    if (n <= (int64_t)max)
        return;
    char msg[128];
    snprintf(msg, sizeof(msg), "More than %" PRIu32 " %s at line %" PRId64, max, what, line);
    ctx_fail(ctx, (int)GLOBAL_ERROR_TOO_MANY, msg);
}

_Noreturn static void error_redefined(ctx_t* ctx, const char* what, sv_str_t id)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s '%.*s'", what, (int)id.size, id.chars);
    ctx_fail(ctx, (int)GLOBAL_ERROR_REDEFINED, msg);
}

int64_t names_count(transient_hashmap_t names)
{
    return names.set.store.cell != NULL ? thm_count(names) : 0;
}

uint32_t names_add(transient_hashmap_t* names, sv_str_t id, bool* existed, ctx_t* ctx, const sv_allocator_t* a)
{
    if (names->set.store.cell == NULL) {
        *names = thm_init(4, a);
        if (names->set.store.cell == NULL)
            longjmp(*ctx->on_error, 1);
    }

    sv_opt_t(uint32_t) found = names_get(*names, id, a);
    *existed = found.is_some;
    if (*existed)
        return found.value;

    value_t key = value_init_str(id, a);
    if (key.obj.cell == NULL)
        longjmp(*ctx->on_error, 1);

    uint32_t next = (uint32_t)thm_count(*names);
    kv_t kv = { .key = key, .value = { .kind = VALUE_NUMBER, .number = (double)next } };
    bool ok = thm_put(names, kv, a);
    value_free(&key, a);
    if (!ok)
        longjmp(*ctx->on_error, 1);
    return next;
}

const char** names_arr_init(transient_hashmap_t names, const sv_allocator_t* a)
{
    int64_t n = names_count(names);
    const char** arr = sv_malloc(a, sizeof(char*) * (size_t)n);
    if (arr == NULL)
        return NULL;
    for (int64_t i = 0; i < n; i++)
        arr[i] = NULL;

    map_iter_t it = thm_iter_init(names);
    for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
        char* copy = sv_str_to_c_str(AS_STR(kv.value.key), a);
        if (copy == NULL) {
            names_arr_deinit(arr, (uint32_t)n, a);
            return NULL;
        }
        arr[(int64_t)kv.value.value.number] = copy;
    }
    return arr;
}

void names_arr_deinit(const char** names, uint32_t n, const sv_allocator_t* a)
{
    if (names == NULL)
        return;
    for (uint32_t i = 0; i < n; i++)
        if (names[i] != NULL)
            sv_free(a, (void*)names[i]);
    sv_free(a, (void*)names);
}

sv_str_t append_prefix(sv_str_t id, sv_str_t prefix, const sv_allocator_t* a)
{
    if (prefix.size == 0)
        return sv_str_copy(id, a);

    sv_str_builder b = sv_strb_init();
    if (sv_strb_add(&b, prefix.chars, prefix.size, a) < 0
        || sv_strb_add_char(&b, '$', a) < 0
        || sv_strb_add(&b, id.chars, id.size, a) < 0
    ) {
        sv_strb_deinit(&b, a);
        return (sv_str_t){0};
    }
    return sv_strb_to_str(&b);
}

uint32_t globals_add(globals_t* g, sv_str_t id, sv_str_t prefix, int64_t line, ctx_t* ctx,
                     const sv_allocator_t* scratch)
{
    check_limit(ctx, names_count(g->name_indexes), UINT32_MAX, "globals", line);

    sv_str_t name = append_prefix(id, prefix, scratch);
    if (name.chars == NULL)
        longjmp(*ctx->on_error, 1);

    bool existed = false;
    uint32_t index = names_add(&g->name_indexes, name, &existed, ctx, &ctx->alloc);
    sv_str_deinit(&name, scratch);
    if (existed)
        error_redefined(ctx, "Global redefined", id);
    return index;
}

sv_opt_t(uint32_t) globals_get(const globals_t g, sv_str_t id, sv_str_t prefix, const sv_allocator_t* a)
{
    sv_str_t name = append_prefix(id, prefix, a);
    if (name.chars == NULL)
        return sv_opt_none_t(uint32_t);

    sv_opt_t(uint32_t) maybe = names_get(g.name_indexes, name, a);
    sv_str_deinit(&name, a);
    if (maybe.is_some)
        return maybe;

    return names_get(g.name_indexes, id, a);
}
