#include "pattern_match.h"
#include "stable_sort.h"
#include "compiler.h"

#include <stdbool.h>

#define SCRUTINEE_NAME "$1"
#define FAIL_NAME "fail"
#define CLAUSES_START 2

/**
static int get_vars_size(cons_t match)
{
    sexpr_t vars = match.arr[1];
    return vars.tag == S_CONS ? vars.cons.size - 1 : 1;
}
*/

static int group_cmp(const void* a, const void* b) {
    sexpr_t sa = ((const sexpr_t*)a)->cons.arr[1];
    sexpr_t sb = ((const sexpr_t*)b)->cons.arr[1];

#define INT_VAL(v) (v).tag == S_ATOM ? (v).atom.literal.kind : LITERAL_FALSE + 1 + (v).cons.arr[0].atom.fn
    int a_val = INT_VAL(sa);
    int b_val = INT_VAL(sb);
    return a_val - b_val;
#undef INT_VAL
}

/**
 * Reorders the pattern to group patterns of the same type together.
 */
static void group(cons_t match)
{
    int start = 2;
    int current = start;
    for(; current < match.size; current++) {
        sexpr_t p = match.arr[current].cons.arr[1];
        if (p.tag == S_ATOM && p.atom.literal.kind == LITERAL_IDENTIFIER) {
            stable_sort(&match.arr[start], current - start, sizeof(sexpr_t), group_cmp);
            start = current + 1;
        }
    }

    if (start != current)
        stable_sort(&match.arr[start], current - start, sizeof(sexpr_t), group_cmp);
}

typedef enum {
    PAT_STR,
    PAT_NUMBER,
    PAT_NIL,
    PAT_BOOL,
    PAT_LIST,
    PAT_HASHMAP,
    PAT_RECORD,
    PAT_TUPLE,
    PAT_VAR,
} pattern_class;

static pattern_class pattern_class_of(sexpr_t pattern)
{
    if (pattern.tag == S_CONS) {
        special_fn_kind fn = pattern.cons.arr[0].atom.fn;
        if (fn == FN_LIST)
            return PAT_LIST;
        if (fn == FN_HASHMAP)
            return PAT_HASHMAP;
        if (fn == FN_RECORD)
            return PAT_RECORD;
        return PAT_TUPLE;
    }

    switch (pattern.atom.literal.kind) {
        case LITERAL_STRING: return PAT_STR;
        case LITERAL_NUMBER: return PAT_NUMBER;
        case LITERAL_NIL: return PAT_NIL;
        case LITERAL_TRUE:
        case LITERAL_FALSE: return PAT_BOOL;
        case LITERAL_IDENTIFIER: return PAT_VAR;
    }
    return PAT_VAR;
}

static const char* class_predicate(pattern_class class)
{
    switch (class) {
        case PAT_STR: return "is-str?";
        case PAT_NUMBER: return "is-number?";
        case PAT_NIL: return "is-nil?";
        case PAT_BOOL: return "is-bool?";
        case PAT_LIST: return "is-list?";
        case PAT_HASHMAP: return "is-hashmap?";
        case PAT_RECORD: return "is-record?";
        case PAT_TUPLE: return "is-tuple?";
        case PAT_VAR: return "";
    }
    return "";
}

static sexpr_t id_atom(const char* name, int64_t line)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = line,
        .literal = { .kind = LITERAL_IDENTIFIER, .literal = sv_str_init(name) },
    });
}

static sexpr_t nil_atom(int64_t line)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = line,
        .literal = { .kind = LITERAL_NIL },
    });
}

static sexpr_t op_atom(operator_kind op, int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_OPERATOR, .line = line, .operator = op });
}

static sexpr_t fn_atom(special_fn_kind fn, int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_SP_FUNCTION, .line = line, .fn = fn });
}

static sexpr_t do_atom(int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_DO });
}

static bool cons_build(sexpr_t* out, const sexpr_t* items, int64_t n, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init_capacity(sexpr_t, n, a);
    if (list.arr == NULL)
        return false;

    for (int64_t i = 0; i < n; i++)
        list.arr[list.size++] = items[i];

    *out = cons_sexpr(list);
    return true;
}

static bool match_oom(ctx_t* ctx, int64_t line)
{
    return error_set_oom(&ctx->err, (int)C_ERR_OOM, line, &ctx->alloc);
}

static sexpr_t discard(sexpr_t* s, ctx_t* ctx)
{
    int64_t line = s->tag == S_CONS ? s->cons.arr[0].atom.line : s->atom.line;
    sexpr_free(s, &ctx->alloc);
    return atom_sexpr((token_t){ .kind = TOKEN_ERROR, .line = line });
}

/**
 * Builds `(do (= name value) body)`, taking ownership of value and body.
 */
static bool lower_bind(
    sexpr_t* out,
    sexpr_t name,
    sexpr_t value,
    sexpr_t body,
    int64_t line,
    ctx_t* ctx
) {
    sexpr_t bind = { 0 };

    sexpr_t bind_items[] = { op_atom(OPERATOR_EQUAL, line), name, value };
    if (!cons_build(&bind, bind_items, 3, &ctx->alloc))
        goto error;
    value = (sexpr_t){ 0 };

    sexpr_t do_items[] = { do_atom(line), bind, body };
    if (!cons_build(out, do_items, 3, &ctx->alloc))
        goto error;

    return true;

error:
    sexpr_free(&bind, &ctx->alloc);
    sexpr_free(&value, &ctx->alloc);
    sexpr_free(&body, &ctx->alloc);
    return match_oom(ctx, line);
}

/**
 * Builds the equality chain for a run of clauses sharing a pattern class, i.e.
 * `(if (== $1 p1) e1 (if (== $1 p2) e2 fail))`.
 */
static bool lower_comparisons(sexpr_t* out, cons_t match, int64_t from, int64_t to, ctx_t* ctx)
{
    int64_t line = match.arr[0].atom.line;
    sexpr_t alt = id_atom(FAIL_NAME, line);
    sexpr_t test = { 0 };

    for (int64_t i = to - 1; i >= from; i--) {
        sexpr_t* slots = match.arr[i].cons.arr;

        sexpr_t test_items[] = {
            op_atom(OPERATOR_EQUAL_EQUAL, line),
            id_atom(SCRUTINEE_NAME, line),
            slots[1],
        };
        if (!cons_build(&test, test_items, 3, &ctx->alloc))
            goto error;
        slots[1] = nil_atom(line);

        sexpr_t if_items[] = { fn_atom(FN_IF, line), test, slots[2], alt };
        sexpr_t next;
        if (!cons_build(&next, if_items, 4, &ctx->alloc))
            goto error;
        slots[2] = nil_atom(line);

        test = (sexpr_t){ 0 };
        alt = next;
    }

    *out = alt;
    return true;

error:
    sexpr_free(&test, &ctx->alloc);
    sexpr_free(&alt, &ctx->alloc);
    return match_oom(ctx, line);
}

/**
 * Builds the type test chain over every class run, innermost first, so the last
 * run's else arm is the match default.
 */
static bool lower_dispatch(sexpr_t* out, cons_t match, int64_t end, sexpr_t def, ctx_t* ctx)
{
    int64_t line = match.arr[0].atom.line;
    sexpr_t chain = def;
    sexpr_t group_chain = { 0 };
    sexpr_t test = { 0 };

    int64_t to = end;
    while (to > CLAUSES_START) {
        pattern_class class = pattern_class_of(match.arr[to - 1].cons.arr[1]);
        int64_t from = to - 1;
        while (from > CLAUSES_START
               && pattern_class_of(match.arr[from - 1].cons.arr[1]) == class)
            from--;

        if (!lower_comparisons(&group_chain, match, from, to, ctx))
            goto cleanup;

        sexpr_t test_items[] = {
            id_atom(class_predicate(class), line),
            id_atom(SCRUTINEE_NAME, line),
        };
        if (!cons_build(&test, test_items, 2, &ctx->alloc))
            goto error;

        sexpr_t if_items[] = { fn_atom(FN_IF, line), test, group_chain, chain };
        sexpr_t next;
        if (!cons_build(&next, if_items, 4, &ctx->alloc))
            goto error;

        test = (sexpr_t){ 0 };
        group_chain = (sexpr_t){ 0 };
        chain = next;
        to = from;
    }

    *out = chain;
    return true;

error:
    match_oom(ctx, line);
cleanup:
    sexpr_free(&test, &ctx->alloc);
    sexpr_free(&group_chain, &ctx->alloc);
    sexpr_free(&chain, &ctx->alloc);
    return false;
}

/**
 * Takes the first wildcard clause's body as the match default and reports where
 * the reachable clauses end. Without a wildcard the default is nil.
 */
static bool lower_default(sexpr_t* out, int64_t* end, cons_t match, ctx_t* ctx)
{
    int64_t line = match.arr[0].atom.line;

    for (int64_t i = CLAUSES_START; i < match.size; i++) {
        sexpr_t pattern = match.arr[i].cons.arr[1];
        if (pattern_class_of(pattern) != PAT_VAR)
            continue;

        *end = i;
        sexpr_t* slots = match.arr[i].cons.arr;
        sexpr_t body = slots[2];
        slots[2] = nil_atom(line);

        if (sv_str_comp(pattern.atom.literal.literal, sv_str_init("_"))) {
            *out = body;
            return true;
        }

        return lower_bind(out, pattern, id_atom(SCRUTINEE_NAME, line), body, line, ctx);
    }

    *end = match.size;
    *out = nil_atom(line);
    return true;
}

sexpr_t match_compile(sexpr_t s, ctx_t* ctx)
{
    cons_t match = s.cons;
    int64_t line = match.arr[0].atom.line;

    group(match);

    int64_t end;
    sexpr_t def;
    if (!lower_default(&def, &end, match, ctx))
        return discard(&s, ctx);

    sexpr_t chain;
    if (!lower_dispatch(&chain, match, end, def, ctx))
        return discard(&s, ctx);

    sexpr_t scrutinee = match.arr[1];
    match.arr[1] = nil_atom(line);

    sexpr_t out;
    if (!lower_bind(&out, id_atom(SCRUTINEE_NAME, line), scrutinee, chain, line, ctx))
        return discard(&s, ctx);

    sexpr_free(&s, &ctx->alloc);
    return out;
}
