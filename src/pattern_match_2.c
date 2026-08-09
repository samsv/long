#include "pattern_match_2.h"
#include "stable_sort.h"
#include "compiler.h"
#include <stdio.h>
#include <string.h>

typedef sv_vec_t(sexpr_t) cons_t;

#define CONDS_START 2
#define TEMP_DIGITS 12
#define TEMP_SIZE (TEMP_DIGITS + 2)
#define TEMPS_MAX 1024
#define FAIL_NAME "$fail"

static char temp_names[TEMPS_MAX][TEMP_SIZE];
static int temps_used;

#define APPEND_CAP(vec, val) (vec)->arr[(vec)->size++] = (val)
#define LITERAL(name) { .kind = LITERAL_IDENTIFIER, .literal = sv_str_init((name)) }
#define NUMBER(value) { .kind = LITERAL_NUMBER, .number = (value) }

#define ATOM_TOKEN(k, union_case) (sexpr_t){ .tag = S_ATOM, .atom = { .kind = k, union_case } }
#define ATOM_TOKEN_NO_CASE(k) (sexpr_t){ .tag = S_ATOM, .atom = { .kind = k } }

#define INIT_CAPACITY(name, cap) \
    cons_t name = sv_vec_init_capacity(sexpr_t, (cap), &ctx->alloc); \
    if (name.arr == NULL) return error_oom(match.arr[0].atom, ctx);

#define PUSH(vec, val) do { \
    sv_vec_push((vec), (val), &success, &ctx->alloc); \
    if (!success) return atom_sexpr((token_t){ .kind = TOKEN_ERROR }); \
} while(0)

#define INIT_DO(name, size) \
    INIT_CAPACITY(name, (size) + 1); \
    APPEND_CAP(&name, ATOM_TOKEN(TOKEN_KEYWORD, .keyword = KEYWORD_DO))

#define INIT_IF(name) \
    INIT_CAPACITY(name, 4); \
    APPEND_CAP(&name, ATOM_TOKEN(TOKEN_SP_FUNCTION, .fn = FN_IF));

#define INIT_MATCH(name, size) \
    INIT_CAPACITY(name, (size) + 1); \
    APPEND_CAP(&name, match_atom(match.arr[0].atom.line));

#define INIT_PIPE(name) \
    INIT_CAPACITY(name, 3); \
    APPEND_CAP(&name, ATOM_TOKEN_NO_CASE(TOKEN_PIPE))

static sexpr_t id_atom(const char* name)
{
    return ATOM_TOKEN(TOKEN_LITERAL, .literal = LITERAL(name));
}

static sexpr_t match_atom(int64_t line)
{
    sexpr_t match_token = id_atom("match");
    match_token.atom.line = line;
    return match_token;
}

static sexpr_t empty_atom = { .tag = S_ATOM, .atom = { .kind = TOKEN_EOF } };

static sexpr_t error_oom(token_t t, ctx_t* c)
{
    error_set_oom(&c->err, C_ERR_OOM, t.line, &c->alloc);
    return ATOM_TOKEN_NO_CASE(TOKEN_ERROR);
}

typedef enum {
    PAT_UNKNOWN,
    // Literals
    PAT_STR,
    PAT_NUMBER,
    PAT_NIL,
    PAT_BOOL,

    // Collections
    PAT_LIST,
    PAT_HASHMAP,
    PAT_RECORD,
    PAT_TUPLE,
    PAT_VAR,
} pattern_class;

static pattern_class pattern_class_of(sexpr_t pattern)
{
    if (pattern.tag == S_CONS) {
        token_t head = pattern.cons.arr[0].atom;
        if (head.kind != TOKEN_SP_FUNCTION)
            return PAT_VAR;

        special_fn_kind fn = head.fn;
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
        case PAT_VAR:
        case PAT_UNKNOWN: return "";
    }
    return "";
}



#define AS_LITERAL(v) (v).atom.literal
#define AS_FN(v) (v).cons.arr[0].atom.fn
static int group_cmp(const void* a, const void* b) {
    sexpr_t sa = ((const sexpr_t*)a)->cons.arr[1];
    sexpr_t sb = ((const sexpr_t*)b)->cons.arr[1];

#define INT_VAL(v) (v).tag == S_ATOM ? (v).atom.literal.kind : LITERAL_FALSE + 1 + (v).cons.arr[0].atom.fn
    int a_val = INT_VAL(sa);
    int b_val = INT_VAL(sb);
#undef INT_VAL

    if (a_val != b_val)
        return a_val - b_val;

    if (sa.tag == S_ATOM) switch (AS_LITERAL(sa).kind) {
        case LITERAL_FALSE:
        case LITERAL_TRUE:
        case LITERAL_NIL:
            return 0;
        case LITERAL_NUMBER:
            return AS_LITERAL(sa).number - AS_LITERAL(sb).number;
        case LITERAL_STRING: {
            sv_str_t str_a = AS_LITERAL(sa).str;
            sv_str_t str_b = AS_LITERAL(sb).str;
            if (str_a.size != str_b.size)
                return str_a.size - str_b.size;
            return memcmp(str_a.chars, str_b.chars, str_a.size);
        }
        case LITERAL_IDENTIFIER:
            return 0;
    }

    return sa.cons.size - sb.cons.size;
}

/**
 * Verifies if two sexpr constructors are equal. Some equality examples
 * Equal literals;
 * Lists with more than one element (both are in the form CONS x xs)
 * Tuples with the same arity
 */
static bool constructor_eql(sexpr_t a, sexpr_t b)
{
    if (a.tag != b.tag)
        return false;

    if (a.tag == S_ATOM) switch (AS_LITERAL(a).kind) {
        case LITERAL_FALSE:
        case LITERAL_TRUE:
        case LITERAL_NIL:
            return true;
        case LITERAL_NUMBER:
            return AS_LITERAL(a).number == AS_LITERAL(b).number;
        case LITERAL_STRING:
            return sv_str_comp(AS_LITERAL(a).str, AS_LITERAL(b).str);
        case LITERAL_IDENTIFIER:
            return true;
    }

    /**
    if (AS_FN(a) == FN_TUPLE) {
        return a.cons.size == b.cons.size;
    } else if (AS_FN(a) == FN_LIST) {
        if (a.cons.size == b.cons.size)
            return true;
        return a.cons.size > 1 && b.cons.size > 1;
    }
    */

    return false;
}
#undef AS_LITERAL
#undef AS_FN

/**
 * Reorders the pattern to group patterns of the same type together.
 */
static sexpr_t group(cons_t* match_ptr, ctx_t* ctx)
{
    int start = 2;
    int current = start;
    for(; current < match_ptr->size; current++) {
        sexpr_t p = match_ptr->arr[current].cons.arr[1];
        if (p.tag == S_ATOM && p.atom.literal.kind == LITERAL_IDENTIFIER) {
            stable_sort(&match_ptr->arr[start], current - start, sizeof(sexpr_t), group_cmp);
            start = current + 1;
        }
    }

    if (start != current)
        stable_sort(&match_ptr->arr[start], current - start, sizeof(sexpr_t), group_cmp);

    cons_t match = *match_ptr;
    for (int64_t i = match_ptr->size - 1; i > 2; i--) {
        sexpr_t a = match_ptr->arr[i].cons.arr[1];
        sexpr_t b = match_ptr->arr[i - 1].cons.arr[1];

        if (!constructor_eql(a, b))
            continue;

        INIT_PIPE(bar_expr);
        APPEND_CAP(&bar_expr, match_ptr->arr[i - 1].cons.arr[2]);
        APPEND_CAP(&bar_expr, match_ptr->arr[i].cons.arr[2]);
        match_ptr->arr[i - 1].cons.arr[2] = cons_sexpr(bar_expr);
        sv_vec_remove_linear(match_ptr, i, NULL);
    }

    return empty_atom;
}

static int tuple_cmp(const void* a, const void* b)
{
    cons_t sa = ((const sexpr_t*)a)->cons.arr[1].cons;
    cons_t sb = ((const sexpr_t*)b)->cons.arr[1].cons;

    return sa.size - sb.size;
}

static int group_tuple(cons_t match, int start)
{
    int current = start;
    for(; current < match.size; current++) {
        sexpr_t p = match.arr[current].cons.arr[1];
        if (p.tag != S_CONS || p.cons.arr[0].atom.fn != FN_TUPLE)
            break;
    }

    stable_sort(&match.arr[start], current - start, sizeof(sexpr_t), tuple_cmp);
    return current;
}

/**
 * Discards an sexpr as an invalid match
 */
static sexpr_t discard(sexpr_t* s, ctx_t* ctx)
{
    int64_t line = s->tag == S_CONS ? s->cons.arr[0].atom.line : s->atom.line;
    sexpr_free(s, &ctx->alloc);
    return atom_sexpr((token_t){ .kind = TOKEN_ERROR, .line = line });
}

static sexpr_t sexpr_literal(const char* name)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = 0,
        .literal = { .kind = LITERAL_IDENTIFIER, .literal = sv_str_init(name) }
    });
}

static sexpr_t bind_var(cons_t* cons, sexpr_t lhs, sexpr_t rhs)
{
    token_t eql_token = { .kind = TOKEN_OPERATOR, .line = 0, .operator = OPERATOR_EQUAL };
    APPEND_CAP(cons, atom_sexpr(eql_token));
    APPEND_CAP(cons, lhs);
    APPEND_CAP(cons, rhs);

    return cons_sexpr(*cons);
}

/**
 * Creates a new `u` var from the book `the implementation of functional programming languages`.
 */
static sexpr_t next_u(void)
{
    char* buf = temp_names[temps_used];
    //snprintf(buf, TEMP_SIZE, "$%0*d", TEMP_DIGITS, ++temps_used);
    snprintf(buf, TEMP_SIZE, "$%d", ++temps_used);
    return sexpr_literal(buf);
}

static sexpr_t match_fail(cons_t* cons, sexpr_t u)
{
    APPEND_CAP(cons, id_atom("match-fail"));
    APPEND_CAP(cons, u);
    return cons_sexpr(*cons);
}

static sexpr_t compile_pattern(cons_t match, int* start_i, sexpr_t u,  sexpr_t deflt_fail, ctx_t* ctx);

#define TUPLE_SIZE(i) match.arr[(i)].cons.arr[1].cons.size - 1
#define INIT_TUPLE(name, size) \
    INIT_CAPACITY(name, (size) + 1); \
    APPEND_CAP(&name, id_atom("tuple"))

static sexpr_t group_and_compile_pattern(cons_t* match, sexpr_t fail, ctx_t* ctx)
{
    group(match, ctx);
    int start_i = 2;
    return compile_pattern(*match, &start_i, match->arr[1], fail, ctx);
}

/**
 * Compile a tuple pattern in the form
 * (
 *      match
 *          (a, b, ...)
 *          (tuple (tuple x, y, ...) body)
 *          ...
 * )
 */
static sexpr_t compile_tuple(cons_t match, ctx_t* ctx)
{
    // initialize (match ...)
    INIT_MATCH(lower_match, match.size);
    // u_1
    APPEND_CAP(&lower_match, match.arr[1].cons.arr[1]);

    // (tuple u_2 u_3 ...)
    INIT_TUPLE(us, match.arr[1].cons.size - 2);
    for (int64_t i = 2; i < match.arr[1].cons.size; i++)
        APPEND_CAP(&us, match.arr[1].cons.arr[i]);
    sexpr_t us_sexpr = us.size > 2 ? cons_sexpr(us) : us.arr[1];

    sexpr_t fail = id_atom(FAIL_NAME);
    // match conditions
    for (int64_t i = 2; i < match.size; i++) {
        // match condition body, in the form (tuple cond (match ...))
        INIT_TUPLE(body, 2);
        // cond first item
        APPEND_CAP(&body, match.arr[i].cons.arr[1].cons.arr[1]);

        INIT_MATCH(body_match, 2);
        APPEND_CAP(&body_match, us_sexpr);

        INIT_TUPLE(cond_tuple, 2);

        INIT_TUPLE(cond_tuple_pats, TUPLE_SIZE(i) - 1);
        for (int64_t j = 2; j < TUPLE_SIZE(i) + 1; j++)
            APPEND_CAP(&cond_tuple_pats, match.arr[i].cons.arr[1].cons.arr[j]);
        sexpr_t pats_sexpr = cond_tuple_pats.size > 2 ? cons_sexpr(cond_tuple_pats) : cond_tuple_pats.arr[1];
        APPEND_CAP(&cond_tuple, pats_sexpr);
        APPEND_CAP(&cond_tuple, match.arr[i].cons.arr[2]);

        APPEND_CAP(&body_match, cons_sexpr(cond_tuple));

        sexpr_t body_sexpr = us.size > 2 ?
            compile_tuple(body_match, ctx) : group_and_compile_pattern(&body_match, fail, ctx);

        APPEND_CAP(&body, body_sexpr);
        APPEND_CAP(&lower_match, cons_sexpr(body));
    }

    return group_and_compile_pattern(&lower_match, fail, ctx);
}

static sexpr_t compile_tuple_init(cons_t match, int* start_i, sexpr_t u, cons_t** end, ctx_t* ctx)
{
    int last_tuple_i = group_tuple(match, *start_i);

    while(*start_i < last_tuple_i) {
        // compile each pattern is_tuple case
        int64_t size = TUPLE_SIZE(*start_i);

        // (is-tuple? u size)
        INIT_CAPACITY(pat_cond, 3);
        APPEND_CAP(&pat_cond, ATOM_TOKEN(TOKEN_LITERAL, .literal = LITERAL(class_predicate(PAT_TUPLE))));
        APPEND_CAP(&pat_cond, u);
        sexpr_t size_atom = ATOM_TOKEN(TOKEN_LITERAL, .literal = NUMBER(size));
        APPEND_CAP(&pat_cond, size_atom);

        // (if (is-tuple? ...))
        INIT_IF(if_block);
        APPEND_CAP(&if_block, cons_sexpr(pat_cond));

        // get the range of patterns which have the same tuple size
        int64_t end_i = *start_i;
        while (end_i < last_tuple_i && TUPLE_SIZE(end_i) == size)
            end_i++;

        // if true body
        INIT_DO(do_expr, size + 1);
        // (do
        //      (= u_1 (nth u 0))
        //      (= u_2 (nth u 1))
        // ...)
        INIT_TUPLE(us, size);
        for (int64_t i = 0; i < size; i++) {
            sexpr_t u_i = next_u();
            APPEND_CAP(&us, u_i);

            INIT_CAPACITY(tuple_get, 3);
            APPEND_CAP(&tuple_get, ATOM_TOKEN(TOKEN_OPERATOR, .operator = OPERATOR_LEFT_BRACKET));
            APPEND_CAP(&tuple_get, u);
            APPEND_CAP(&tuple_get, ATOM_TOKEN(TOKEN_LITERAL, .literal = NUMBER(i)));

            INIT_CAPACITY(u_assign, 3);
            sexpr_t assign_expr = bind_var(&u_assign, u_i, cons_sexpr(tuple_get));
            APPEND_CAP(&do_expr, assign_expr);
        }

        // build (match (tuple u_1 ... u_n))
        INIT_MATCH(tuple_match, 1 + end_i - *start_i);
        APPEND_CAP(&tuple_match, cons_sexpr(us));
        for (int64_t i = *start_i; i < end_i; i++)
            APPEND_CAP(&tuple_match, match.arr[i]);

        APPEND_CAP(&do_expr, compile_tuple(tuple_match, ctx));
        APPEND_CAP(&if_block, cons_sexpr(do_expr));

        APPEND_CAP(*end, cons_sexpr(if_block));
        *end = &(*end)->arr[(*end)->size - 1].cons;

        *start_i = end_i;
    }

    return cons_sexpr(**end);
}
#undef TUPLE_SIZE

static sexpr_t compile_var(cons_t match, int* start_i, sexpr_t u, sexpr_t deflt_fail, ctx_t* ctx)
{
    INIT_PIPE(bar_expr);
    INIT_CAPACITY(set_var_expr, 3);

    INIT_DO(do_expr, 2);
    APPEND_CAP(&do_expr, bind_var(&set_var_expr, match.arr[*start_i].cons.arr[1], u));
    APPEND_CAP(&do_expr, match.arr[(*start_i)++].cons.arr[2]);

    APPEND_CAP(&bar_expr, cons_sexpr(do_expr));
    sexpr_t deflt = compile_pattern(match, start_i, u, deflt_fail, ctx);
    if (deflt.tag == S_ATOM && deflt.atom.kind == TOKEN_ERROR)
        return deflt;
    APPEND_CAP(&bar_expr, deflt);

    return cons_sexpr(bar_expr);
}

static sexpr_t compile_literals(cons_t match, int* start_i, sexpr_t u, pattern_class pat_type, ctx_t* ctx)
{
    INIT_CAPACITY(pat_cond, 2);
    APPEND_CAP(&pat_cond, ATOM_TOKEN(TOKEN_LITERAL, .literal = LITERAL(class_predicate(pat_type))));
    APPEND_CAP(&pat_cond, u);

    INIT_IF(if_block);
    APPEND_CAP(&if_block, cons_sexpr(pat_cond));

    cons_t* end = &if_block;
    for (int i = *start_i; i < match.size && pat_type == pattern_class_of(match.arr[i].cons.arr[1]); *start_i = ++i) {
        INIT_IF(if_body_block);

        INIT_CAPACITY(if_cond, 3);
        APPEND_CAP(&if_cond, ATOM_TOKEN(TOKEN_OPERATOR, .operator = OPERATOR_EQUAL_EQUAL));
        APPEND_CAP(&if_cond, u);
        APPEND_CAP(&if_cond, match.arr[i].cons.arr[1]);

        APPEND_CAP(&if_body_block, cons_sexpr(if_cond));
        APPEND_CAP(&if_body_block, match.arr[i].cons.arr[2]);

        APPEND_CAP(end, cons_sexpr(if_body_block));
        end = &end->arr[end->size - 1].cons;
    }

    APPEND_CAP(end, id_atom(FAIL_NAME));
    return cons_sexpr(if_block);
}

static sexpr_t compile_pattern(cons_t match, int* start_i, sexpr_t u, sexpr_t deflt_fail, ctx_t* ctx)
{
    // initialize bar
    INIT_PIPE(bar_expr);

    int current_i = *start_i;
    cons_t* end = &bar_expr;
    // start matching the conditions
    while (current_i < match.size) {
        // TODO: Check the pattern type, if it's a variable (x, y, etc), constant (1, true, nil, "str", etc) or a
        // more complex pattern (e.g. [x, ..xs], {x, y}, etc)
        pattern_class pat_type = pattern_class_of(match.arr[current_i].cons.arr[1]);
        // compile literal patterns
        if (pat_type >= PAT_STR && pat_type <= PAT_BOOL)
            APPEND_CAP(end, compile_literals(match, &current_i, u, pat_type, ctx));
        else if (pat_type == PAT_TUPLE) {
            sexpr_t ret = compile_tuple_init(match, &current_i, u, &end, ctx);
            if (ret.tag == S_ATOM && ret.atom.kind == TOKEN_ERROR)
                return ret;
            continue;
        }
        else if (pat_type == PAT_VAR) {
            APPEND_CAP(&bar_expr, compile_var(match, &current_i, u, deflt_fail, ctx));
            APPEND_CAP(end, id_atom(FAIL_NAME));
            goto end;
        }

        end = &end->arr[end->size - 1].cons;
    }

    APPEND_CAP(end, id_atom(FAIL_NAME));
    APPEND_CAP(&bar_expr, deflt_fail);

end:
    return cons_sexpr(bar_expr);
}

sexpr_t match_compile_2(sexpr_t s, ctx_t* ctx)
{
    if (s.cons.size <= CONDS_START)
        return discard(&s, ctx);

    sexpr_t e = group(&s.cons, ctx);

    cons_t match = s.cons;
    sv_str_t str = sexpr_format(e, &ctx->alloc);
    printf("\n\n %.*s \n\n", (int)str.size, str.chars);

    int start_i = CONDS_START;
    INIT_CAPACITY(match_fail_expr, 2);

    sexpr_t subject = match.arr[1];
    if (subject.tag == S_ATOM &&
        subject.atom.kind == TOKEN_LITERAL &&
        subject.atom.literal.kind == LITERAL_IDENTIFIER
    ) {
        return compile_pattern(match, &start_i, subject, match_fail(&match_fail_expr, subject), ctx);
    }

    sexpr_t u = next_u();

    // Initialize (do (= u_i x) (| ...))
    INIT_DO(do_expr, 2);

    INIT_CAPACITY(eql_expr, 3);
    APPEND_CAP(&do_expr, bind_var(&eql_expr, u, match.arr[1]));
    APPEND_CAP(&do_expr, compile_pattern(match, &start_i, u, match_fail(&match_fail_expr, u), ctx));

    return cons_sexpr(do_expr);
}
