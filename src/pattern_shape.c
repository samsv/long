#include "pattern_shape.h"
#include "compiler.h"
#include "token.h"

#define WILDCARD_NAME "_"
typedef sv_vec_t(sexpr_t) cons_t;

static bool is_named(sexpr_t e, const char* name)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_LITERAL
        && e.atom.literal.kind == LITERAL_IDENTIFIER
        && sv_str_comp(e.atom.literal.literal, sv_str_init(name));
}

static bool is_wildcard(sexpr_t p)
{
    return is_named(p, WILDCARD_NAME);
}

bool list_has_tail(sexpr_t list)
{
    sexpr_t last = list.cons.arr[list.cons.size - 1];
    return last.tag == S_CONS && last.cons.arr[0].atom.kind == TOKEN_DOT_DOT;
}

int64_t list_n_fixed(sexpr_t list)
{
    return list.cons.size - 1 - (list_has_tail(list) ? 1 : 0);
}

static bool is_dot_dot(cons_t cons)
{
    sexpr_t last = cons.arr[cons.size - 1];
    return last.tag == S_ATOM && last.atom.kind == TOKEN_DOT_DOT;
}

bool record_is_open(sexpr_t rec)
{
    return is_dot_dot(rec.cons);
}

bool hashmap_is_open(sexpr_t map)
{
    return is_dot_dot(map.cons);
}

int64_t hashmap_n_keys(sexpr_t map)
{
    return (map.cons.size - 1 - (hashmap_is_open(map) ? 1 : 0)) / 2;
}

int64_t record_n_fields(sexpr_t rec)
{
    int64_t n = rec.cons.size - 1 - (record_is_open(rec) ? 1 : 0);
    return n / 2;
}

bool pattern_vars(sexpr_t p, sv_vec_t(sv_str_t)* out, ctx_t* ctx)
{
    if (p.tag == S_ATOM) {
        if (p.atom.kind != TOKEN_LITERAL
            || p.atom.literal.kind != LITERAL_IDENTIFIER
            || is_wildcard(p)
        )
            return true;

        /* A set: a repeated variable binds one name, so it must not be counted twice. */
        for (int64_t i = 0; i < out->size; i++)
            if (sv_str_comp(out->arr[i], p.atom.literal.literal))
                return true;

        int success = 0;
        sv_vec_push(out, p.atom.literal.literal, &success, &ctx->alloc);
        return success != 0 ?
            true
            : error_set_oom(&ctx->err, C_ERR_OOM, p.atom.line, &ctx->alloc);
    }

    token_t head = p.cons.arr[0].atom;
    if (head.kind != TOKEN_SP_FUNCTION)
        return true;

    if (head.fn == FN_RECORD || head.fn == FN_HASHMAP) {
        int64_t n = head.fn == FN_RECORD ? record_n_fields(p) : hashmap_n_keys(p);
        for (int64_t i = 0; i < n; i++)
            if (!pattern_vars(p.cons.arr[2 + 2 * i], out, ctx))
                return false;

        return true;
    }

    if (head.fn == FN_LIST) {
        for (int64_t i = 0; i < list_n_fixed(p); i++)
            if (!pattern_vars(p.cons.arr[1 + i], out, ctx))
                return false;

        if (list_has_tail(p))
            return pattern_vars(p.cons.arr[p.cons.size - 1].cons.arr[1], out, ctx);

        return true;
    }

    for (int64_t i = 1; i < p.cons.size; i++)
        if (!pattern_vars(p.cons.arr[i], out, ctx))
            return false;

    return true;
}
