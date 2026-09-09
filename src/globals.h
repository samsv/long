#ifndef LONG_GLOBALS_H
#define LONG_GLOBALS_H

#include "ctx.h"
#include "obj/map.h"
#include "std/option.h"

typedef struct {
    transient_hashmap_t name_indexes;
} globals_t;

typedef enum {
    GLOBAL_ERROR_OOM,
    GLOBAL_ERROR_UNDEFINED,
    GLOBAL_ERROR_REDEFINED,
    GLOBAL_ERROR_TOO_MANY,
} global_error_t;

sv_opt_def(global_error_t);

sv_opt_t(global_error_t) globals_add(globals_t* g, sv_str_t id, sv_str_t prefix, int64_t line, ctx_t* ctx);
sv_opt_t(uint32_t) globals_get(const globals_t g, sv_str_t id, sv_str_t prefix, ctx_t* ctx);

sv_opt_t(uint32_t) names_get(transient_hashmap_t names, sv_str_t id, ctx_t* ctx);
bool check_limit(ctx_t* ctx, int64_t n, uint32_t max, const char* what, int64_t line);
sv_str_t append_prefix(sv_str_t id, sv_str_t prefix, ctx_t* ctx);
int64_t names_count(transient_hashmap_t names);
bool names_add(transient_hashmap_t* names, sv_str_t id, ctx_t* ctx, bool* existed);

#endif
