#include "parser.h"
#include "pattern_shape.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct {
    uint8_t left;
    uint8_t right;
    bool has_right;
} precedence;

typedef struct {
    token_kind kind;
    union {
        operator_kind op;
        keyword_kind keyword;
        special_fn_kind fn;
    };
} token_pattern;

static sexpr_t parse_expr(scanner_t* s, ctx_t* ctx, uint8_t min_prec, const sv_allocator_t* a);
static sexpr_t parse_pattern(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a);
static sexpr_t parse_pattern_tail(scanner_t* s, ctx_t* ctx, sexpr_t lhs, const sv_allocator_t* a);

static token_pattern kind_pattern(token_kind kind)
{
    return (token_pattern){ .kind = kind };
}

static token_pattern op_pattern(operator_kind op)
{
    return (token_pattern){ .kind = TOKEN_OPERATOR, .op = op };
}

static token_pattern kw_pattern(keyword_kind keyword)
{
    return (token_pattern){ .kind = TOKEN_KEYWORD, .keyword = keyword };
}

static token_pattern fn_pattern(special_fn_kind fn)
{
    return (token_pattern){ .kind = TOKEN_SP_FUNCTION, .fn = fn };
}

static bool token_is(token_t t, token_pattern p)
{
    if (t.kind != p.kind)
        return false;
    if (p.kind == TOKEN_OPERATOR)
        return t.operator == p.op;
    if (p.kind == TOKEN_KEYWORD)
        return t.keyword == p.keyword;
    if (p.kind == TOKEN_SP_FUNCTION)
        return t.fn == p.fn;
    return true;
}

static token_t pattern_token(token_pattern p)
{
    token_t t = { .kind = p.kind, .line = 0 };
    if (p.kind == TOKEN_OPERATOR)
        t.operator = p.op;
    else if (p.kind == TOKEN_KEYWORD)
        t.keyword = p.keyword;
    else if (p.kind == TOKEN_SP_FUNCTION)
        t.fn = p.fn;
    return t;
}

static void token_text(token_t token, char* buf, size_t size, const sv_allocator_t* a)
{
    sv_str_t text = token_format(token, a);
    if (text.size < 0) {
        snprintf(buf, size, "?");
        return;
    }
    if (text.size > (int64_t)size - 1)
        snprintf(buf, size, "%.*s...", (int)(size - 4), text.chars);
    else
        snprintf(buf, size, "%.*s", (int)text.size, text.chars);
}

/* The scanner has stored its error already. */
_Noreturn static void scanner_failed(ctx_t* ctx)
{
    longjmp(*ctx->on_error, 2);
}

_Noreturn static void unexpected_token(ctx_t* ctx, token_t token, const char* what, const sv_allocator_t* a)
{
    char got[64];
    token_text(token, got, sizeof(got), a);

    char msg[320];
    snprintf(msg, sizeof(msg), "%s '%s' at line %" PRId64, what, got, token.line);
    ctx_fail(ctx, (int)PARSER_ERROR_UNEXPECTED_TOKEN, msg);
}

static void push_sexpr(sv_vec_t(sexpr_t)* list, sexpr_t e, const sv_allocator_t* a)
{
    int success;
    sv_vec_push(list, e, &success, a);
    (void)success;
}

static sexpr_t cons_of(sexpr_t* items, int64_t n, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    int success;
    sv_vec_push_many(&list, items, n, &success, a);
    (void)success;
    return cons_sexpr(list);
}

static token_t parser_peek(scanner_t* s, ctx_t* ctx)
{
    token_t token = scanner_peek(s, ctx);
    while (token.kind == TOKEN_NEWLINE) {
        scanner_next(s, ctx);
        token = scanner_peek(s, ctx);
    }
    return token;
}

static token_t parser_next(scanner_t* s, ctx_t* ctx)
{
    parser_peek(s, ctx);
    return scanner_next(s, ctx);
}

static token_t parser_expect(scanner_t* s, ctx_t* ctx, token_pattern p, const sv_allocator_t* a)
{
    token_t token = parser_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        scanner_failed(ctx);
    if (token_is(token, p))
        return token;

    char got[64];
    char want[64];
    token_text(token, got, sizeof(got), a);
    token_text(pattern_token(p), want, sizeof(want), a);

    char msg[320];
    snprintf(msg, sizeof(msg), "Unexpected token '%s' at line %" PRId64 ", expected '%s'",
             got, token.line, want);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    ctx_fail(ctx, (int)kind, msg);
}

static token_t parser_expect_close(scanner_t* s, ctx_t* ctx, token_t open, token_pattern close,
                                   const sv_allocator_t* a)
{
    token_t token = parser_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        scanner_failed(ctx);
    if (token_is(token, close))
        return token;

    char open_text[64];
    char got[64];
    char want[64];
    token_text(open, open_text, sizeof(open_text), a);
    token_text(token, got, sizeof(got), a);
    token_text(pattern_token(close), want, sizeof(want), a);

    char msg[320];
    snprintf(msg, sizeof(msg),
             "Unclosed '%s' from line %" PRId64 ": expected '%s', got '%s' at line %" PRId64,
             open_text, open.line, want, got, token.line);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    ctx_fail(ctx, (int)kind, msg);
}

static token_t parser_expect_literal(scanner_t* s, ctx_t* ctx, literal_kind literal_kind,
                                     const char* literal_name, const sv_allocator_t* a)
{
    token_t token = parser_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        scanner_failed(ctx);
    if (token.kind == TOKEN_LITERAL && token.literal.kind == literal_kind)
        return token;

    char got[64];
    token_text(token, got, sizeof(got), a);

    char msg[320];
    snprintf(msg, sizeof(msg), "Expected %s, got '%s' at line %" PRId64,
             literal_name, got, token.line);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    ctx_fail(ctx, (int)kind, msg);
}

static token_t parser_expect_id(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    return parser_expect_literal(s, ctx, LITERAL_IDENTIFIER, "literal", a);
}

static token_t parser_expect_str(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    return parser_expect_literal(s, ctx, LITERAL_STRING, "string", a);
}

static bool parser_check(scanner_t* s, ctx_t* ctx, token_pattern p, token_t* out)
{
    token_t token = parser_peek(s, ctx);
    if (!token_is(token, p))
        return false;
    *out = scanner_next(s, ctx);
    return true;
}

void parser_skip_semicolons(scanner_t* s, ctx_t* ctx)
{
    token_t semi;
    while (parser_check(s, ctx, kind_pattern(TOKEN_SEMICOLON), &semi))
        ;
}

static precedence infix_prec(operator_kind op)
{
    switch (op) {
        case OPERATOR_EQUAL:
            return (precedence){ .left = 2, .right = 1, .has_right = true };
        case OPERATOR_PIPE_FORWARD:
            return (precedence){ .left = 6, .right = 7, .has_right = true };
        case OPERATOR_GREATER:
        case OPERATOR_GREATER_EQUAL:
        case OPERATOR_LESS:
        case OPERATOR_LESS_EQUAL:
        case OPERATOR_BANG_EQUAL:
        case OPERATOR_EQUAL_EQUAL:
            return (precedence){ .left = 7, .right = 8, .has_right = true };
        case OPERATOR_PLUS:
        case OPERATOR_MINUS:
            return (precedence){ .left = 9, .right = 10, .has_right = true };
        case OPERATOR_STAR:
        case OPERATOR_SLASH:
            return (precedence){ .left = 11, .right = 12, .has_right = true };
        case OPERATOR_DOUBLE_COLON:
        case OPERATOR_DOT:
            return (precedence){ .left = 18, .right = 19, .has_right = true };
        case OPERATOR_LEFT_PAREN:
        case OPERATOR_LEFT_BRACKET:
            return (precedence){ .left = 15, .right = 0, .has_right = false };
    }
    return (precedence){ .left = 0, .right = 0, .has_right = false };
}

#define PREC_ELEMENT 5
#define PREC_PARAM 2

static void parse_list_tail(scanner_t* s, ctx_t* ctx, sv_vec_t(sexpr_t)* list, token_t dots,
                            bool as_pattern, const sv_allocator_t* a)
{
    if (list->size == 1)
        unexpected_token(ctx, dots, "List tail must follow an element", a);

    sexpr_t tail = as_pattern ? parse_pattern(s, ctx, a) : parse_expr(s, ctx, 5, a);
    if (as_pattern && !is_list_tail(tail))
        unexpected_token(ctx, dots, "List tail must be a variable or a list after", a);

    sexpr_t items[] = { atom_sexpr(dots), tail };
    push_sexpr(list, cons_of(items, 2, a), a);

    token_t comma;
    if (parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
        unexpected_token(ctx, comma, "List tail must be the last element", a);
}

static sexpr_t parse_container(sv_vec_t(sexpr_t)* list, scanner_t* s, ctx_t* ctx,
                               token_t open_token, token_pattern close, bool allow_tail,
                               uint8_t min_prec, const sv_allocator_t* a)
{
    token_t closer;
    if (parser_check(s, ctx, close, &closer))
        return cons_sexpr(*list);

    for (;;) {
        token_t dots;
        if (allow_tail && parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            parse_list_tail(s, ctx, list, dots, false, a);
            break;
        }

        push_sexpr(list, parse_expr(s, ctx, min_prec, a), a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open_token, close, a);
    return cons_sexpr(*list);
}

static sexpr_t parse_parens(scanner_t* s, ctx_t* ctx, token_t left_paren, sexpr_t* lhs,
                            uint8_t min_prec, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    if (lhs != NULL)
        push_sexpr(&list, *lhs, a);

    return parse_container(&list, s, ctx, left_paren, kind_pattern(TOKEN_RIGHT_PAREN), false,
                           min_prec, a);
}

static sexpr_t parse_list(scanner_t* s, ctx_t* ctx, token_t open, token_pattern close,
                          const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t list_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_LIST };
    push_sexpr(&list, atom_sexpr(list_atom), a);

    return parse_container(&list, s, ctx, open, close, true, PREC_ELEMENT, a);
}

static void parse_spread(scanner_t* s, ctx_t* ctx, sv_vec_t(sexpr_t)* list, token_t dots,
                         const sv_allocator_t* a)
{
    if (parser_peek(s, ctx).kind == TOKEN_RIGHT_BRACE) {
        push_sexpr(list, atom_sexpr(dots), a);
        return;
    }

    sexpr_t base = parse_expr(s, ctx, PREC_ELEMENT, a);
    sexpr_t items[] = { atom_sexpr(dots), base };
    push_sexpr(list, cons_of(items, 2, a), a);

    token_t comma;
    if (parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
        unexpected_token(ctx, comma, "Spread must be the last element, got", a);
}

static sexpr_t parse_hashmap(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t map_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_HASHMAP };
    push_sexpr(&list, atom_sexpr(map_atom), a);

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACE), &closer))
        return cons_sexpr(list);

    for (;;) {
        token_t dots;
        if (parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            parse_spread(s, ctx, &list, dots, a);
            break;
        }

        push_sexpr(&list, parse_expr(s, ctx, 5, a), a);
        parser_expect(s, ctx, kind_pattern(TOKEN_COLON), a);
        push_sexpr(&list, parse_expr(s, ctx, 5, a), a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACE), a);
    return cons_sexpr(list);
}

static sexpr_t parse_record(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t tuple_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_RECORD };
    push_sexpr(&list, atom_sexpr(tuple_atom), a);

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACE), &closer))
        return cons_sexpr(list);

    for (;;) {
        token_t dots;
        if (parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            parse_spread(s, ctx, &list, dots, a);
            break;
        }

        token_t field = parser_expect_id(s, ctx, a);
        push_sexpr(&list, atom_sexpr(field), a);

        token_t colon;
        sexpr_t value = atom_sexpr(field);
        if (parser_check(s, ctx, kind_pattern(TOKEN_COLON), &colon))
            value = parse_expr(s, ctx, 5, a);
        push_sexpr(&list, value, a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACE), a);
    return cons_sexpr(list);
}

static sexpr_t parse_bracket(scanner_t* s, ctx_t* ctx, token_t left_bracket, sexpr_t lhs,
                             const sv_allocator_t* a)
{
    sexpr_t rhs = parse_expr(s, ctx, 0, a);
    parser_expect_close(s, ctx, left_bracket, kind_pattern(TOKEN_RIGHT_BRACKET), a);

    sexpr_t items[] = { atom_sexpr(left_bracket), lhs, rhs };
    return cons_of(items, 3, a);
}

static sexpr_t parse_block(scanner_t* s, ctx_t* ctx, const token_pattern* ends,
                           int64_t n_ends, int64_t line, token_t* term, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t do_atom = { .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_DO };
    push_sexpr(&list, atom_sexpr(do_atom), a);

    for (;;) {
        push_sexpr(&list, parse_expr(s, ctx, 0, a), a);
        parser_skip_semicolons(s, ctx);

        for (int64_t k = 0; k < n_ends; k++) {
            if (parser_check(s, ctx, ends[k], term))
                return cons_sexpr(list);
        }
    }
}

static sexpr_t parse_loop(scanner_t* s, ctx_t* ctx, token_t loop_token, const sv_allocator_t* a)
{
    sexpr_t binding = parse_pattern(s, ctx, a);
    parser_expect(s, ctx, kw_pattern(KEYWORD_IN), a);
    sexpr_t iter = parse_expr(s, ctx, 0, a);

    sexpr_t cond_items[] = { binding, iter };
    sexpr_t loop_cond = cons_of(cond_items, 2, a);

    parser_expect(s, ctx, kw_pattern(KEYWORD_DO), a);

    token_pattern ends[] = { kw_pattern(KEYWORD_END) };
    token_t term;
    sexpr_t body = parse_block(s, ctx, ends, 1, loop_token.line, &term, a);

    sexpr_t items[] = { atom_sexpr(loop_token), loop_cond, body };
    return cons_of(items, 3, a);
}

static sexpr_t parse_clause_body(scanner_t* s, ctx_t* ctx, int64_t line, token_t* term,
                                 const sv_allocator_t* a)
{
    token_t when;
    sexpr_t guard = { 0 };
    bool guarded = parser_check(s, ctx, kw_pattern(KEYWORD_WHEN), &when);
    if (guarded)
        guard = parse_expr(s, ctx, 0, a);

    parser_expect(s, ctx, kw_pattern(KEYWORD_DO), a);

    token_pattern ends[] = { kind_pattern(TOKEN_PIPE), kw_pattern(KEYWORD_END) };
    sexpr_t body = parse_block(s, ctx, ends, 2, line, term, a);

    if (!guarded)
        return body;

    sexpr_t items[] = { atom_sexpr(when), guard, body };
    return cons_of(items, 3, a);
}

static void push_clause(sv_vec_t(sexpr_t)* list, sexpr_t pattern, sexpr_t body,
                        sexpr_t tuple_atom, const sv_allocator_t* a)
{
    sexpr_t items[] = { tuple_atom, pattern, body };
    push_sexpr(list, cons_of(items, 3, a), a);
}

static void reject_alternative(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    token_t pipe;
    if (parser_check(s, ctx, kind_pattern(TOKEN_PIPE), &pipe))
        unexpected_token(ctx, pipe, "Expected 'do' or 'when' after a clause pattern, got", a);
}

static token_t pattern_head(sexpr_t pattern)
{
    return pattern.tag == S_ATOM ? pattern.atom : pattern.cons.arr[0].atom;
}

static bool clause_matches_params(sexpr_t pattern, int64_t n_params)
{
    if (n_params < 2)
        return true;
    while (pattern_is_alias(pattern))
        pattern = alias_pattern(pattern);
    if (pattern.tag == S_ATOM)
        return pattern.atom.kind == TOKEN_LITERAL
            && pattern.atom.literal.kind == LITERAL_IDENTIFIER;

    token_t head = pattern_head(pattern);
    return head.kind == TOKEN_SP_FUNCTION && head.fn == FN_TUPLE
        && pattern.cons.size - 1 == n_params;
}

static sexpr_t params_scrutinee(ctx_t* ctx, sexpr_t args, int64_t line, const sv_allocator_t* a)
{
    for (int64_t i = 0; i < args.cons.size; i++)
        if (args.cons.arr[i].tag != S_ATOM)
            unexpected_token(ctx, pattern_head(args.cons.arr[i]), "Expected a parameter name", a);

    if (args.cons.size == 0)
        ctx_fail(ctx, (int)PARSER_ERROR_UNEXPECTED_TOKEN,
                 "A function with no parameters has nothing to match");
    if (args.cons.size == 1)
        return args.cons.arr[0];

    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t tuple = { .kind = TOKEN_SP_FUNCTION, .line = line, .fn = FN_TUPLE };
    push_sexpr(&list, atom_sexpr(tuple), a);
    for (int64_t i = 0; i < args.cons.size; i++)
        push_sexpr(&list, args.cons.arr[i], a);

    return cons_sexpr(list);
}

static sexpr_t parse_fun_clauses(scanner_t* s, ctx_t* ctx, sexpr_t args, token_t fun_token,
                                 token_t* term, token_t* pending, const sv_allocator_t* a)
{
    sexpr_t scrutinee = params_scrutinee(ctx, args, fun_token.line, a);

    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t match_atom = { .kind = TOKEN_SP_FUNCTION, .line = fun_token.line, .fn = FN_MATCH };
    push_sexpr(&list, atom_sexpr(match_atom), a);
    push_sexpr(&list, scrutinee, a);

    for (;;) {
        token_t id = parser_peek(s, ctx);
        if (id.kind == TOKEN_ERROR)
            scanner_failed(ctx);

        sexpr_t pattern;
        if (id.kind == TOKEN_LITERAL && id.literal.kind == LITERAL_IDENTIFIER) {
            scanner_next(s, ctx);
            token_t after = parser_peek(s, ctx);
            bool header = token_is(after, op_pattern(OPERATOR_LEFT_PAREN))
                || token_is(after, op_pattern(OPERATOR_LEFT_BRACKET));
            if (header && pending != NULL) {
                *pending = id;
                *term = (token_t){ .kind = TOKEN_PIPE, .line = id.line };
                break;
            }
            pattern = parse_pattern_tail(s, ctx, atom_sexpr(id), a);
        } else {
            pattern = parse_pattern(s, ctx, a);
        }

        if (!clause_matches_params(pattern, args.cons.size)) {
            int64_t line = pattern_head(pattern).line;
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Clause must be a %" PRId64 " element tuple or a variable at line %" PRId64,
                     args.cons.size, line);
            ctx_fail(ctx, (int)PARSER_ERROR_UNEXPECTED_TOKEN, msg);
        }

        reject_alternative(s, ctx, a);

        token_t inner = { .kind = TOKEN_EOF };
        sexpr_t body = parse_clause_body(s, ctx, fun_token.line, &inner, a);

        token_t tuple = { .kind = TOKEN_SP_FUNCTION, .line = fun_token.line, .fn = FN_TUPLE };
        push_clause(&list, pattern, body, atom_sexpr(tuple), a);

        if (inner.kind != TOKEN_PIPE) {
            *term = inner;
            break;
        }
    }

    return cons_sexpr(list);
}

static sexpr_t parse_fun_body(scanner_t* s, ctx_t* ctx, sv_vec_t(sexpr_t)* list, token_t fun_token,
                              token_t* term, token_t* pending, const sv_allocator_t* a)
{
    token_t id;
    if (pending != NULL && pending->kind == TOKEN_LITERAL) {
        id = *pending;
        *pending = (token_t){ .kind = TOKEN_EOF };
    } else {
        id = parser_expect_id(s, ctx, a);
    }
    push_sexpr(list, atom_sexpr(id), a);

    token_t left_bracket;
    if (parser_check(s, ctx, op_pattern(OPERATOR_LEFT_BRACKET), &left_bracket)) {
        sv_vec_t(sexpr_t) captures = sv_vec_init(sexpr_t);
        sexpr_t closure_vals = parse_container(&captures, s, ctx, left_bracket,
                                               kind_pattern(TOKEN_RIGHT_BRACKET), false,
                                               PREC_ELEMENT, a);
        push_sexpr(list, closure_vals, a);
    }

    token_t left_paren = parser_expect(s, ctx, op_pattern(OPERATOR_LEFT_PAREN), a);
    sexpr_t args = parse_parens(s, ctx, left_paren, NULL, PREC_PARAM, a);
    push_sexpr(list, args, a);

    sexpr_t body;
    token_t pipe;
    if (parser_check(s, ctx, kind_pattern(TOKEN_PIPE), &pipe)) {
        body = parse_fun_clauses(s, ctx, args, fun_token, term, pending, a);
    } else {
        body = parse_expr(s, ctx, 0, a);
        if (pending != NULL && !parser_check(s, ctx, kind_pattern(TOKEN_PIPE), term))
            *term = parser_expect_close(s, ctx, fun_token, kw_pattern(KEYWORD_END), a);
    }
    push_sexpr(list, body, a);

    return cons_sexpr(*list);
}

static sexpr_t parse_fun(scanner_t* s, ctx_t* ctx, token_t fun_token, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    push_sexpr(&list, atom_sexpr(fun_token), a);

    token_t pipe_token;
    if (!parser_check(s, ctx, kind_pattern(TOKEN_PIPE), &pipe_token)) {
        token_t term = { .kind = TOKEN_EOF };
        return parse_fun_body(s, ctx, &list, fun_token, &term, NULL, a);
    }

    token_t end_token = pipe_token;
    token_t pending = { .kind = TOKEN_EOF };
    while (end_token.kind == TOKEN_PIPE) {
        sv_vec_t(sexpr_t) body_list = sv_vec_init(sexpr_t);
        push_sexpr(&list, parse_fun_body(s, ctx, &body_list, end_token, &end_token, &pending, a), a);
    }

    return cons_sexpr(list);
}

static sexpr_t parse_import(scanner_t* s, ctx_t* ctx, token_t import_token, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init_capacity(sexpr_t, 3, a);
    list.arr[list.size++] = atom_sexpr(import_token);
    list.arr[list.size++] = atom_sexpr(parser_expect_id(s, ctx, a));
    parser_expect(s, ctx, op_pattern(OPERATOR_LEFT_PAREN), a);
    list.arr[list.size++] = atom_sexpr(parser_expect_str(s, ctx, a));
    parser_expect(s, ctx, kind_pattern(TOKEN_RIGHT_PAREN), a);
    return cons_sexpr(list);
}

static sexpr_t parse_if(scanner_t* s, ctx_t* ctx, token_t if_token, const sv_allocator_t* a)
{
    sexpr_t cond = parse_expr(s, ctx, 0, a);
    parser_expect(s, ctx, kw_pattern(KEYWORD_DO), a);

    token_pattern ends[] = { kw_pattern(KEYWORD_ELSE), kw_pattern(KEYWORD_END) };
    token_t term;
    sexpr_t true_branch = parse_block(s, ctx, ends, 2, if_token.line, &term, a);

    sexpr_t items[] = { atom_sexpr(if_token), cond, true_branch };
    sexpr_t out = cons_of(items, 3, a);

    if (token_is(term, kw_pattern(KEYWORD_ELSE))) {
        sexpr_t false_branch;
        token_t else_if;
        if (parser_check(s, ctx, fn_pattern(FN_IF), &else_if)) {
            false_branch = parse_if(s, ctx, else_if, a);
        } else {
            token_pattern end_only[] = { kw_pattern(KEYWORD_END) };
            token_t ignored;
            false_branch = parse_block(s, ctx, end_only, 1, term.line, &ignored, a);
        }
        push_sexpr(&out.cons, false_branch, a);
    }

    return out;
}

static sexpr_t parse_tuple(scanner_t* s, ctx_t* ctx, token_t open, sexpr_t first, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t tuple_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_TUPLE };
    push_sexpr(&list, atom_sexpr(tuple_atom), a);
    push_sexpr(&list, first, a);

    for (;;) {
        token_t closer;
        if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_PAREN), &closer))
            return cons_sexpr(list);

        push_sexpr(&list, parse_expr(s, ctx, 0, a), a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_PAREN), a);
    return cons_sexpr(list);
}

static sexpr_t parse_paren_pattern(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sexpr_t first = parse_pattern(s, ctx, a);

    token_t comma;
    if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma)) {
        parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_PAREN), a);
        return first;
    }

    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t tuple_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_TUPLE };
    push_sexpr(&list, atom_sexpr(tuple_atom), a);
    push_sexpr(&list, first, a);

    for (;;) {
        token_t closer;
        if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_PAREN), &closer))
            return cons_sexpr(list);

        push_sexpr(&list, parse_pattern(s, ctx, a), a);

        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_PAREN), a);
    return cons_sexpr(list);
}

static sexpr_t parse_list_pattern(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t list_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_LIST };
    push_sexpr(&list, atom_sexpr(list_atom), a);

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACKET), &closer))
        return cons_sexpr(list);

    for (;;) {
        token_t dots;
        if (parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            parse_list_tail(s, ctx, &list, dots, true, a);
            break;
        }

        push_sexpr(&list, parse_pattern(s, ctx, a), a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACKET), a);
    return cons_sexpr(list);
}

static sexpr_t parse_record_pattern(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t record_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_RECORD };
    push_sexpr(&list, atom_sexpr(record_atom), a);

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACE), &closer))
        return cons_sexpr(list);

    for (;;) {
        token_t dots;
        if (parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            push_sexpr(&list, atom_sexpr(dots), a);
            break;
        }

        token_t field = parser_expect_id(s, ctx, a);
        push_sexpr(&list, atom_sexpr(field), a);

        token_t colon;
        sexpr_t value = atom_sexpr(field);
        if (parser_check(s, ctx, kind_pattern(TOKEN_COLON), &colon))
            value = parse_pattern(s, ctx, a);
        push_sexpr(&list, value, a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACE), a);
    return cons_sexpr(list);
}

static sexpr_t parse_hashmap_pattern(scanner_t* s, ctx_t* ctx, token_t open, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t map_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_HASHMAP };
    push_sexpr(&list, atom_sexpr(map_atom), a);

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACE), &closer))
        return cons_sexpr(list);

    for (;;) {
        token_t dots;
        if (parser_check(s, ctx, kind_pattern(TOKEN_DOT_DOT), &dots)) {
            push_sexpr(&list, atom_sexpr(dots), a);
            break;
        }

        token_t key = parser_next(s, ctx);
        if (key.kind == TOKEN_ERROR)
            scanner_failed(ctx);
        if (key.kind != TOKEN_LITERAL || key.literal.kind == LITERAL_IDENTIFIER)
            unexpected_token(ctx, key, "Hashmap pattern keys must be literals", a);
        push_sexpr(&list, atom_sexpr(key), a);

        parser_expect(s, ctx, kind_pattern(TOKEN_COLON), a);
        push_sexpr(&list, parse_pattern(s, ctx, a), a);

        token_t comma;
        if (!parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
            break;
    }

    parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACE), a);
    return cons_sexpr(list);
}

static sexpr_t parse_pattern_primary(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    token_t token = parser_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        scanner_failed(ctx);

    if (token.kind == TOKEN_EOF) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected pattern, got end of input at line %" PRId64,
                 token.line);
        ctx_fail(ctx, (int)PARSER_ERROR_EOF, msg);
    }

    if (token.kind == TOKEN_LITERAL)
        return atom_sexpr(token);
    if (token_is(token, op_pattern(OPERATOR_LEFT_PAREN)))
        return parse_paren_pattern(s, ctx, token, a);
    if (token_is(token, op_pattern(OPERATOR_LEFT_BRACKET)))
        return parse_list_pattern(s, ctx, token, a);
    if (token.kind == TOKEN_LEFT_BRACE)
        return parse_record_pattern(s, ctx, token, a);
    if (token.kind == TOKEN_PERCENT_BRACE)
        return parse_hashmap_pattern(s, ctx, token, a);
    if (token_is(token, op_pattern(OPERATOR_MINUS))) {
        token_t num = parser_next(s, ctx);
        if (num.kind == TOKEN_ERROR)
            scanner_failed(ctx);
        if (num.kind != TOKEN_LITERAL || num.literal.kind != LITERAL_NUMBER)
            unexpected_token(ctx, num, "Expected a number after '-' in a pattern", a);

        num.literal.number = -num.literal.number;
        return atom_sexpr(num);
    }

    unexpected_token(ctx, token, "Expected a pattern", a);
}

static sexpr_t parse_pattern_tail(scanner_t* s, ctx_t* ctx, sexpr_t lhs, const sv_allocator_t* a)
{
    token_t eq;
    if (!parser_check(s, ctx, op_pattern(OPERATOR_EQUAL), &eq))
        return lhs;

    sexpr_t rhs = parse_pattern(s, ctx, a);
    if (!pattern_is_name(lhs) && !pattern_is_name(rhs))
        unexpected_token(ctx, eq, "One side of '=' in a pattern must be a name, got", a);

    sexpr_t items[] = { atom_sexpr(eq), lhs, rhs };
    return cons_of(items, 3, a);
}

static sexpr_t parse_pattern(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    return parse_pattern_tail(s, ctx, parse_pattern_primary(s, ctx, a), a);
}

static sexpr_t parse_match(scanner_t* s, ctx_t* ctx, token_t match_token, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    push_sexpr(&list, atom_sexpr(match_token), a);
    push_sexpr(&list, parse_expr(s, ctx, 0, a), a);

    sexpr_t tuple_atom = atom_sexpr((token_t){ .kind = TOKEN_SP_FUNCTION, .line = match_token.line, .fn = FN_TUPLE });
    token_t term;
    if (!parser_check(s, ctx, kind_pattern(TOKEN_PIPE), &term)) {
        parser_expect_close(s, ctx, match_token, kw_pattern(KEYWORD_END), a);
        return cons_sexpr(list);
    }

    while (term.kind == TOKEN_PIPE) {
        sexpr_t pattern = parse_pattern(s, ctx, a);
        reject_alternative(s, ctx, a);
        sexpr_t body = parse_clause_body(s, ctx, match_token.line, &term, a);
        push_clause(&list, pattern, body, tuple_atom, a);
    }

    return cons_sexpr(list);
}

static sexpr_t parse_operator(scanner_t* s, ctx_t* ctx, token_t start_token, uint8_t min_prec,
                              const sv_allocator_t* a)
{
    sexpr_t lhs;
    if (start_token.kind == TOKEN_OPERATOR) {
        if (start_token.operator == OPERATOR_LEFT_PAREN) {
            lhs = parse_expr(s, ctx, 0, a);

            token_t comma;
            if (parser_check(s, ctx, kind_pattern(TOKEN_COMMA), &comma))
                lhs = parse_tuple(s, ctx, start_token, lhs, a);
            else
                parser_expect_close(s, ctx, start_token, kind_pattern(TOKEN_RIGHT_PAREN), a);
        } else if (start_token.operator == OPERATOR_LEFT_BRACKET) {
            lhs = parse_list(s, ctx, start_token, kind_pattern(TOKEN_RIGHT_BRACKET), a);
        } else if (start_token.operator == OPERATOR_MINUS) {
            sexpr_t rhs = parse_expr(s, ctx, 13, a);
            sexpr_t items[] = { atom_sexpr(start_token), rhs };
            lhs = cons_of(items, 2, a);
        } else {
            unexpected_token(ctx, start_token, "Not a prefix operator", a);
        }
    } else if (start_token.kind == TOKEN_LITERAL) {
        lhs = atom_sexpr(start_token);
    } else if (start_token.kind == TOKEN_PERCENT_BRACE) {
        lhs = parse_hashmap(s, ctx, start_token, a);
    } else if (start_token.kind == TOKEN_LEFT_BRACE) {
        lhs = parse_record(s, ctx, start_token, a);
    } else if (start_token.kind == TOKEN_KEYWORD && start_token.keyword == KEYWORD_NOT) {
        sexpr_t rhs = parse_expr(s, ctx, 7, a);
        sexpr_t items[] = { atom_sexpr(start_token), rhs };
        lhs = cons_of(items, 2, a);
    } else {
        unexpected_token(ctx, start_token, "Unexpected token", a);
    }

    for (;;) {
        token_t token = scanner_peek(s, ctx);
        if (token.kind == TOKEN_ERROR)
            scanner_failed(ctx);

        operator_kind op;
        if (token.kind == TOKEN_OPERATOR) {
            op = token.operator;
        } else if (token.kind == TOKEN_KEYWORD
                   && (token.keyword == KEYWORD_AND || token.keyword == KEYWORD_OR)) {
            precedence kprec = token.keyword == KEYWORD_OR
                ? (precedence){ .left = 5, .right = 6, .has_right = true }
                : (precedence){ .left = 6, .right = 7, .has_right = true };
            if (kprec.left < min_prec)
                break;

            scanner_next(s, ctx);
            sexpr_t rhs = parse_expr(s, ctx, kprec.right, a);
            sexpr_t items[] = { atom_sexpr(token), lhs, rhs };
            lhs = cons_of(items, 3, a);
            continue;
        } else if (token.kind == TOKEN_LITERAL || token.kind == TOKEN_SP_FUNCTION) {
            unexpected_token(ctx, token, "Unexpected token", a);
        } else {
            break;
        }

        precedence prec = infix_prec(op);
        if (prec.left < min_prec)
            break;

        scanner_next(s, ctx);

        if (prec.has_right) {
            sexpr_t rhs = parse_expr(s, ctx, prec.right, a);
            sexpr_t items[] = { atom_sexpr(token), lhs, rhs };
            lhs = cons_of(items, 3, a);
        } else if (op == OPERATOR_LEFT_PAREN) {
            sexpr_t callee = lhs;
            lhs = parse_parens(s, ctx, token, &callee, PREC_ELEMENT, a);
        } else {
            lhs = parse_bracket(s, ctx, token, lhs, a);
        }
    }

    return lhs;
}

static sexpr_t parse_expr(scanner_t* s, ctx_t* ctx, uint8_t min_prec, const sv_allocator_t* a)
{
    token_t token = parser_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        scanner_failed(ctx);

    if (token.kind == TOKEN_EOF) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected expression, got end of input at line %" PRId64,
                 token.line);
        ctx_fail(ctx, (int)PARSER_ERROR_EOF, msg);
    }

    if (token.kind == TOKEN_KEYWORD && token.keyword == KEYWORD_DO) {
        token_pattern ends[] = { kw_pattern(KEYWORD_END) };
        token_t term;
        return parse_block(s, ctx, ends, 1, token.line, &term, a);
    }

    if (token.kind == TOKEN_SP_FUNCTION) {
        switch (token.fn) {
            case FN_IF:
                return parse_if(s, ctx, token, a);
            case FN_FOR:
                return parse_loop(s, ctx, token, a);
            case FN_MAP:
                return parse_loop(s, ctx, token, a);
            case FN_MAPF:
                return parse_loop(s, ctx, token, a);
            case FN_FUN:
                return parse_fun(s, ctx, token, a);
            case FN_LIST: {
                token_t left_paren = parser_expect(s, ctx, op_pattern(OPERATOR_LEFT_PAREN), a);
                return parse_list(s, ctx, left_paren, kind_pattern(TOKEN_RIGHT_PAREN), a);
            }
            case FN_MATCH:
                return parse_match(s, ctx, token, a);
            case FN_IMPORT:
                return parse_import(s, ctx, token, a);
            case FN_LENGTH:
            case FN_RECORD_GET_OR_NIL:
            case FN_HASHMAP_GET_OR_NIL:
            case FN_HASHMAP:
            case FN_RECORD:
            case FN_TUPLE:
            case FN_REDUCE:
            case FN_WHILE: {
                char msg[128];
                snprintf(msg, sizeof(msg), "Parser '%s' is not implemented yet at line %" PRId64,
                         special_fn_text(token.fn), token.line);
                ctx_fail(ctx, (int)PARSER_ERROR_NOT_IMPLEMENTED, msg);
            }
        }
    }

    return parse_operator(s, ctx, token, min_prec, a);
}

sexpr_t parser_expr(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    return parse_expr(s, ctx, 0, a);
}

sexpr_t parser_program(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t do_atom = { .kind = TOKEN_KEYWORD, .line = 1, .keyword = KEYWORD_DO };
    push_sexpr(&list, atom_sexpr(do_atom), a);

    for (;;) {
        parser_skip_semicolons(s, ctx);
        token_t peeked = parser_peek(s, ctx);
        if (peeked.kind == TOKEN_EOF)
            break;
        if (peeked.kind == TOKEN_ERROR)
            scanner_failed(ctx);

        push_sexpr(&list, parse_expr(s, ctx, 0, a), a);
    }

    return cons_sexpr(list);
}
