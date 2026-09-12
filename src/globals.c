#include "globals.h"
#include <inttypes.h>

sv_opt_t(uint32_t) names_get(transient_hashmap_t names, sv_str_t id, ctx_t* ctx)
{
    if (names.set.store.cell == NULL)
        return sv_opt_none_t(uint32_t);

    value_t key = value_init_str(id, &ctx->alloc);
    if (key.obj.cell == NULL)
        return sv_opt_none_t(uint32_t);

    sv_opt_t(value_t) v = thm_get(names, key);
    value_free(&key, &ctx->alloc);
    if (!v.is_some)
        return sv_opt_none_t(uint32_t);
    return sv_opt_some_t(uint32_t, (uint32_t)v.value.number);
}

bool check_limit(ctx_t* ctx, int64_t n, uint32_t max, const char* what, int64_t line)
{
    if (n <= (int64_t)max)
        return true;
    char msg[128];
    snprintf(msg, sizeof(msg), "More than %" PRIu32 " %s at line %" PRId64, max, what, line);
    return error_set(&ctx->err, (int)GLOBAL_ERROR_TOO_MANY, msg, &ctx->alloc);
}

static global_error_t error_redefined(ctx_t* ctx, const char* what, sv_str_t id)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s '%.*s'", what, (int)id.size, id.chars);
    error_set(&ctx->err, (int)GLOBAL_ERROR_REDEFINED, msg, &ctx->alloc);
    return GLOBAL_ERROR_REDEFINED;
}

int64_t names_count(transient_hashmap_t names)
{
    return names.set.store.cell != NULL ? thm_count(names) : 0;
}

bool names_add(transient_hashmap_t* names, sv_str_t id, ctx_t* ctx, bool* existed)
{
    if (names->set.store.cell == NULL) {
        *names = thm_init(4, &ctx->alloc);
        if (names->set.store.cell == NULL)
            return false;
    }

    *existed = names_get(*names, id, ctx).is_some;
    if (*existed)
        return true;

    value_t key = value_init_str(id, &ctx->alloc);
    if (key.obj.cell == NULL)
        return false;

    kv_t kv = { .key = key, .value = { .kind = VALUE_NUMBER, .number = (double)thm_count(*names) } };
    bool ok = thm_put(names, kv, &ctx->alloc);
    value_free(&key, &ctx->alloc);
    return ok;
}

sv_str_t append_prefix(sv_str_t id, sv_str_t prefix, ctx_t* ctx)
{
#define CHECK_OOM(cond) do { if (!(cond)) { \
    sv_strb_deinit(&b, &ctx->alloc); \
    return (sv_str_t){0}; \
} } while (0)

    if (prefix.size == 0)
        return sv_str_copy(id, &ctx->alloc);

    sv_str_builder b = sv_strb_init();
    CHECK_OOM(sv_strb_add(&b, prefix.chars, prefix.size, &ctx->alloc) > -1);
    CHECK_OOM(sv_strb_add_char(&b, '$', &ctx->alloc) > -1);
    CHECK_OOM(sv_strb_add(&b, id.chars, id.size, &ctx->alloc) > -1);
    sv_str_t name = sv_strb_to_str(&b);
    return name;
#undef CHECK_OOM
}

sv_opt_t(global_error_t) globals_add(globals_t* g, sv_str_t id, sv_str_t prefix, int64_t line, ctx_t* ctx)
{
    if (!check_limit(ctx, names_count(g->name_indexes), UINT32_MAX, "globals", line))
        return sv_opt_some_t(global_error_t, GLOBAL_ERROR_TOO_MANY);

    sv_str_t name = append_prefix(id, prefix, ctx);
    if (name.chars == NULL)
        return sv_opt_some_t(global_error_t, GLOBAL_ERROR_OOM);

    bool existed = false;
    if (!names_add(&g->name_indexes, name, ctx, &existed)) {
        sv_str_deinit(&name, &ctx->alloc);
        return sv_opt_some_t(global_error_t, GLOBAL_ERROR_OOM);
    }
    if (existed) {
        sv_str_deinit(&name, &ctx->alloc);
        return sv_opt_some_t(global_error_t, error_redefined(ctx, "Global redefined", id));
    }

    sv_str_deinit(&name, &ctx->alloc);
    return sv_opt_none_t(global_error_t);
}

sv_opt_t(uint32_t) globals_get(const globals_t g, sv_str_t id, sv_str_t prefix, ctx_t* ctx)
{
    sv_str_t name = append_prefix(id, prefix, ctx);
    if (name.chars == NULL)
        return sv_opt_none_t(uint32_t);

    sv_opt_t(uint32_t) maybe = names_get(g.name_indexes, name, ctx);
    sv_str_deinit(&name, &ctx->alloc);
    if (maybe.is_some)
        return maybe;

    return names_get(g.name_indexes, id, ctx);
}
