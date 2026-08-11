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

#define TRY(name, call) \
    sexpr_t name = (call); \
    if (name.tag == S_ATOM && name.atom.kind == TOKEN_ERROR) \
        return name;

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

#define WILDCARD_STR { .chars = "$_", .size = 2 }
static const sv_str_t wildcard_str = WILDCARD_STR;
static const sexpr_t wildcard = {
    .tag = S_ATOM,
    .atom = {
        .kind = TOKEN_LITERAL,
        .literal = { .kind = LITERAL_IDENTIFIER, .literal = WILDCARD_STR }
    }
};

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

    if (a.tag == S_ATOM && AS_LITERAL(a).kind == AS_LITERAL(b).kind) switch (AS_LITERAL(a).kind) {
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

    return false;
}
#undef AS_LITERAL
#undef AS_FN

static bool is_dot_dot(sexpr_t e)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_DOT_DOT;
}

static bool is_fail(sexpr_t e)
{
    return e.tag == S_ATOM
        && e.atom.kind == TOKEN_LITERAL
        && e.atom.literal.kind == LITERAL_IDENTIFIER
        && sv_str_comp(e.atom.literal.literal, sv_str_init(FAIL_NAME));
}

 /**
  * Closes a `(| tried otherwise)` built in place. A bar with a fail on either side cannot
  * change what the expression yields, so it is dropped: `a | FAIL = a` and `FAIL | b = b`.
  * `bar` must hold the pipe atom and both operands.
  */
 static sexpr_t close_pipe(cons_t bar)
 {
     if (is_fail(bar.arr[2]))
         return bar.arr[1];
     if (is_fail(bar.arr[1]))
         return bar.arr[2];

     return cons_sexpr(bar);
 }

/**
 * Reorders the rows to put patterns of the same constructor next to each other.
 */
static void group(cons_t* match_ptr)
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
}

/**
 * Merges the rows of `lower` that test the same head constructor into a single row.
 * Rows are in the form (tuple head (match us conds...)).
 * `lower` must already be sorted by constructor. Identifier heads are left alone.
 * Example
 * lower =
 * (match $1
        (tuple 1 (match $2 (tuple 2 (do 1))))
        (tuple 1 (match $2 (tuple 4 (do 3)))))
 * out =
 * (match $1
        (tuple 1 (match $2 (tuple 2 (do 1))
                           (tuple 4 (do 3)))))
 */
static void fuse_tuple_rows(cons_t* lower)
{
    for (int64_t i = lower->size - 1; i > CONDS_START; i--) {
        sexpr_t a = lower->arr[i].cons.arr[1];
        sexpr_t b = lower->arr[i - 1].cons.arr[1];

        if (!constructor_eql(a, b))
            continue;

        cons_t* dest = &lower->arr[i - 1].cons.arr[2].cons;
        cons_t src = lower->arr[i].cons.arr[2].cons;
        for (int64_t j = CONDS_START; j < src.size; j++)
            APPEND_CAP(dest, src.arr[j]);

        sv_vec_remove_linear(lower, i, NULL);
    }
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
    APPEND_CAP(&name, ATOM_TOKEN(TOKEN_SP_FUNCTION, .fn = FN_TUPLE))

static sexpr_t group_and_compile_pattern(cons_t* match, sexpr_t fail, ctx_t* ctx)
{
    group(match);
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
        sexpr_t head = match.arr[i].cons.arr[1].cons.arr[1];
        sexpr_t row_body = match.arr[i].cons.arr[2];

        // Make different named variable patterns work by binding the given name to the body
        if (pattern_class_of(head) == PAT_VAR) {
            INIT_CAPACITY(alias, 3);
            INIT_DO(do_block, 2);
            APPEND_CAP(&do_block, bind_var(&alias, head, lower_match.arr[1]));
            APPEND_CAP(&do_block, row_body);

            row_body = cons_sexpr(do_block);
            head = wildcard;
        }

        // match condition body, in the form (tuple cond (match ...))
        INIT_TUPLE(body, 2);
        // cond first item
        APPEND_CAP(&body, head);

        INIT_MATCH(body_match, match.size - 1);
        APPEND_CAP(&body_match, us_sexpr);

        INIT_TUPLE(cond_tuple, 2);

        INIT_TUPLE(cond_tuple_pats, TUPLE_SIZE(i) - 1);
        for (int64_t j = 2; j < TUPLE_SIZE(i) + 1; j++)
            APPEND_CAP(&cond_tuple_pats, match.arr[i].cons.arr[1].cons.arr[j]);
        sexpr_t pats_sexpr = cond_tuple_pats.size > 2 ? cons_sexpr(cond_tuple_pats) : cond_tuple_pats.arr[1];
        APPEND_CAP(&cond_tuple, pats_sexpr);
        APPEND_CAP(&cond_tuple, row_body);

        APPEND_CAP(&body_match, cons_sexpr(cond_tuple));

        APPEND_CAP(&body, cons_sexpr(body_match));
        APPEND_CAP(&lower_match, cons_sexpr(body));
    }

    group(&lower_match);
    fuse_tuple_rows(&lower_match);

    for (int64_t i = CONDS_START; i < lower_match.size; i++) {
        cons_t body_match = lower_match.arr[i].cons.arr[2].cons;
        TRY(cond, us.size > 2 ?
            compile_tuple(body_match, ctx)
            : group_and_compile_pattern(&body_match, fail, ctx));
        lower_match.arr[i].cons.arr[2] = cond;
    }

    int start_i = CONDS_START;
    return compile_pattern(lower_match, &start_i, lower_match.arr[1], fail, ctx);
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

static sexpr_t compile_empty_list(cons_t match, sexpr_t body, sexpr_t u, ctx_t* ctx)
{
    // if is-list?
    INIT_CAPACITY(pat_cond, 2);
    APPEND_CAP(&pat_cond, ATOM_TOKEN(TOKEN_LITERAL, .literal = LITERAL(class_predicate(PAT_LIST))));
    APPEND_CAP(&pat_cond, u);

    INIT_IF(if_list);
    APPEND_CAP(&if_list, cons_sexpr(pat_cond));

    // if is-cons?
    INIT_IF(if_cons);
    INIT_CAPACITY(if_cond, 2);
    APPEND_CAP(&if_cond, id_atom("is-cons?"));
    APPEND_CAP(&if_cond, u);

    APPEND_CAP(&if_cons, cons_sexpr(if_cond));
    APPEND_CAP(&if_cons, id_atom(FAIL_NAME));
    APPEND_CAP(&if_cons, body);

    APPEND_CAP(&if_list, cons_sexpr(if_cons));
    return cons_sexpr(if_list);
}

/**
 * Compiles a list pattern in the form
 * (match x
 *      (tuple (list x y (..xs)) body)
 *      ...
 * )
 * to
 * (if (is-list? x)
 *      (do
 *          (list-uncons $2 $3)
 *          (match
 *              (tuple $2 $3)
 *              (tuple (tuple x (list y (..xs))) body)
 * )))
 */
static sexpr_t compile_list(cons_t match, int* start_i, sexpr_t u, ctx_t* ctx)
{
    sexpr_t list_cond = match.arr[*start_i].cons.arr[1];
    sexpr_t body = match.arr[(*start_i)++].cons.arr[2];

    if (list_cond.cons.size == 1)
        return compile_empty_list(match, body, u, ctx);

    // (if (is-list? u))
    INIT_CAPACITY(pat_cond, 2);
    APPEND_CAP(&pat_cond, id_atom("is-cons?"));
    APPEND_CAP(&pat_cond, u);

    INIT_IF(if_block);
    APPEND_CAP(&if_block, cons_sexpr(pat_cond));

    // if body
    INIT_CAPACITY(list_uncons_expr, 4);
    // head
    sexpr_t u_2 = next_u();
    // tail
    sexpr_t u_3 = next_u();
    // (list-uncons u u2 u3)
    APPEND_CAP(&list_uncons_expr, id_atom("list-uncons"));
    // TODO make list-uncons be equal to (u2 = head u; u3 = tail u)
    APPEND_CAP(&list_uncons_expr, u);
    APPEND_CAP(&list_uncons_expr, u_2);
    APPEND_CAP(&list_uncons_expr, u_3);

    // (match
    //      (tuple $2 $3)
    //      (tuple (tuple x, (list ..)) body) )
    INIT_MATCH(lower_list_match, 2);

    // (tuple $2 $3)
    INIT_TUPLE(pat, 2);
    APPEND_CAP(&pat, u_2);
    APPEND_CAP(&pat, u_3);
    // holds the tuple
    APPEND_CAP(&lower_list_match, cons_sexpr(pat));

    // (tuple x, (list ..))
    INIT_TUPLE(new_pat, 2);
    APPEND_CAP(&new_pat, list_cond.cons.arr[1]);

    INIT_CAPACITY(tail, list_cond.cons.size - 1);
    APPEND_CAP(&tail, ATOM_TOKEN(TOKEN_SP_FUNCTION, .fn = FN_LIST));
    for (int64_t i = 2; i < list_cond.cons.size; i++)
        APPEND_CAP(&tail, list_cond.cons.arr[i]);
    if (tail.size == 2 && tail.arr[1].tag == S_CONS && is_dot_dot(tail.arr[1].cons.arr[0])) {
        // extract xs from (.. xs)
        APPEND_CAP(&new_pat, tail.arr[1].cons.arr[1]);
    } else {
        APPEND_CAP(&new_pat, cons_sexpr(tail));
    }

    INIT_TUPLE(row, 2);
    APPEND_CAP(&row, cons_sexpr(new_pat));
    APPEND_CAP(&row, body);

    APPEND_CAP(&lower_list_match, cons_sexpr(row));
    TRY(compiled_match, compile_tuple(lower_list_match, ctx));

    // (do (list-uncons ..) (match ..))
    INIT_DO(do_block, 3);
    APPEND_CAP(&do_block, cons_sexpr(list_uncons_expr));
    APPEND_CAP(&do_block, compiled_match);

    APPEND_CAP(&if_block, cons_sexpr(do_block));
    return cons_sexpr(if_block);
}

static sexpr_t compile_var(cons_t match, int* start_i, sexpr_t u, sexpr_t deflt_fail, ctx_t* ctx)
{
    sexpr_t var = match.arr[*start_i].cons.arr[1];
    sexpr_t body = match.arr[(*start_i)++].cons.arr[2];

    INIT_PIPE(bar_expr);
    if (sv_str_comp(var.atom.literal.literal, wildcard_str)) {
        APPEND_CAP(&bar_expr, body);
    } else {
        INIT_DO(do_expr, 2);
        INIT_CAPACITY(set_var_expr, 3);
        APPEND_CAP(&do_expr, bind_var(&set_var_expr, var, u));
        APPEND_CAP(&do_expr, body);

        APPEND_CAP(&bar_expr, cons_sexpr(do_expr));
    }

    TRY(deflt, compile_pattern(match, start_i, u, deflt_fail, ctx));
    APPEND_CAP(&bar_expr, deflt);

    return close_pipe(bar_expr);
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
    if (*start_i >= match.size)
        return deflt_fail;

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
            TRY(_, compile_tuple_init(match, &current_i, u, &end, ctx));
            continue;
        }
        else if (pat_type == PAT_VAR) {
            APPEND_CAP(&bar_expr, compile_var(match, &current_i, u, deflt_fail, ctx));
            APPEND_CAP(end, id_atom(FAIL_NAME));
            goto end;
        } else if (pat_type == PAT_LIST) {
            TRY(list, compile_list(match, &current_i, u, ctx));
            APPEND_CAP(end, list);
        }

        end = &end->arr[end->size - 1].cons;
    }

    APPEND_CAP(end, id_atom(FAIL_NAME));
    APPEND_CAP(&bar_expr, deflt_fail);

end:
    return close_pipe(bar_expr);
}

sexpr_t match_compile_2(sexpr_t s, ctx_t* ctx)
{
    if (s.cons.size <= CONDS_START)
        return discard(&s, ctx);

    group(&s.cons);

    cons_t match = s.cons;

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

    TRY(expr, compile_pattern(match, &start_i, u, match_fail(&match_fail_expr, u), ctx));
    APPEND_CAP(&do_expr, expr);

    return cons_sexpr(do_expr);
}
