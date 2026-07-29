#include "parser.h"
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

static sexpr_t parse_expr(scanner_t* s, ctx_t* ctx, uint8_t min_prec);

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

static void token_text(token_t token, char* buf, size_t size, ctx_t* ctx)
{
    sv_str_t text = token_format(token, &ctx->alloc);
    if (text.size < 0) {
        snprintf(buf, size, "?");
        return;
    }
    if (text.size > (int64_t)size - 1)
        snprintf(buf, size, "%.*s...", (int)(size - 4), text.chars);
    else
        snprintf(buf, size, "%.*s", (int)text.size, text.chars);
    sv_str_deinit(&text, &ctx->alloc);
}

static token_t parser_error_at(ctx_t* ctx, parser_error_kind kind, int64_t line, const char* msg)
{
    ctx->err.error_code = (int)kind;
    ctx->err.msg = sv_str_copy(sv_str_init(msg), &ctx->alloc);
    return (token_t){ .kind = TOKEN_ERROR, .line = line };
}

static token_t oom_error(ctx_t* ctx, int64_t line)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Out of memory at line %" PRId64, line);
    return parser_error_at(ctx, PARSER_ERROR_OOM, line, msg);
}

static bool is_error_sexpr(sexpr_t e)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR;
}

static sexpr_t atom_sexpr(token_t t)
{
    return (sexpr_t){ .tag = S_ATOM, .atom = t };
}

static sexpr_t cons_sexpr(sv_vec_t(sexpr_t) list)
{
    return (sexpr_t){ .tag = S_CONS, .cons = list };
}

static bool push_sexpr(sv_vec_t(sexpr_t)* list, sexpr_t e, ctx_t* ctx)
{
    int success;
    sv_vec_push(list, e, &success, &ctx->alloc);
    return success != 0;
}

static sexpr_t free_list_error(sv_vec_t(sexpr_t)* list, ctx_t* ctx, sexpr_t err)
{
    sexpr_t cons = cons_sexpr(*list);
    sexpr_free(&cons, &ctx->alloc);
    return err;
}

static sexpr_t cons_of(ctx_t* ctx, sexpr_t* items, int64_t n, int64_t line)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    int success;
    sv_vec_push_many(&list, items, n, &success, &ctx->alloc);
    if (!success) {
        for (int64_t k = 0; k < n; k++)
            sexpr_free(&items[k], &ctx->alloc);
        sv_vec_deinit(&list, &ctx->alloc);
        return atom_sexpr(oom_error(ctx, line));
    }
    return cons_sexpr(list);
}

static sexpr_t unexpected_token_error(ctx_t* ctx, token_t token, const char* what)
{
    char got[64];
    token_text(token, got, sizeof(got), ctx);

    char msg[320];
    snprintf(msg, sizeof(msg), "%s '%s' at line %" PRId64, what, got, token.line);
    return atom_sexpr(parser_error_at(ctx, PARSER_ERROR_UNEXPECTED_TOKEN, token.line, msg));
}

static token_t parser_expect(scanner_t* s, ctx_t* ctx, token_pattern p)
{
    token_t token = scanner_next(s, ctx);
    if (token.kind == TOKEN_ERROR || token_is(token, p))
        return token;

    char got[64];
    char want[64];
    token_text(token, got, sizeof(got), ctx);
    token_text(pattern_token(p), want, sizeof(want), ctx);

    char msg[320];
    snprintf(msg, sizeof(msg), "Unexpected token '%s' at line %" PRId64 ", expected '%s'",
             got, token.line, want);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    return parser_error_at(ctx, kind, token.line, msg);
}

static token_t parser_expect_close(scanner_t* s, ctx_t* ctx, token_t open, token_pattern close)
{
    token_t token = scanner_next(s, ctx);
    if (token.kind == TOKEN_ERROR || token_is(token, close))
        return token;

    char open_text[64];
    char got[64];
    char want[64];
    token_text(open, open_text, sizeof(open_text), ctx);
    token_text(token, got, sizeof(got), ctx);
    token_text(pattern_token(close), want, sizeof(want), ctx);

    char msg[320];
    snprintf(msg, sizeof(msg),
             "Unclosed '%s' from line %" PRId64 ": expected '%s', got '%s' at line %" PRId64,
             open_text, open.line, want, got, token.line);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    return parser_error_at(ctx, kind, token.line, msg);
}

static token_t parser_expect_id(scanner_t* s, ctx_t* ctx)
{
    token_t token = scanner_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        return token;
    if (token.kind == TOKEN_LITERAL && token.literal.kind == LITERAL_IDENTIFIER)
        return token;

    char got[64];
    token_text(token, got, sizeof(got), ctx);

    char msg[320];
    snprintf(msg, sizeof(msg), "Expected identifier, got '%s' at line %" PRId64,
             got, token.line);
    parser_error_kind kind =
        token.kind == TOKEN_EOF ? PARSER_ERROR_EOF : PARSER_ERROR_UNEXPECTED_TOKEN;
    return parser_error_at(ctx, kind, token.line, msg);
}

static bool parser_check(scanner_t* s, ctx_t* ctx, token_pattern p, token_t* out)
{
    token_t token = scanner_peek(s, ctx);
    if (!token_is(token, p))
        return false;
    *out = scanner_next(s, ctx);
    return true;
}

static precedence infix_prec(operator_kind op)
{
    switch (op) {
        case OPERATOR_EQUAL:
            return (precedence){ .left = 2, .right = 1, .has_right = true };
        case OPERATOR_COMMA:
            return (precedence){ .left = 3, .right = 4, .has_right = true };
        case OPERATOR_PIPE_FORWARD:
            return (precedence){ .left = 6, .right = 5, .has_right = true };
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
        case OPERATOR_DOT:
            return (precedence){ .left = 18, .right = 17, .has_right = true };
        case OPERATOR_LEFT_PAREN:
        case OPERATOR_LEFT_BRACKET:
            return (precedence){ .left = 15, .right = 0, .has_right = false };
    }
    return (precedence){ .left = 0, .right = 0, .has_right = false };
}

static sexpr_t parse_container(sv_vec_t(sexpr_t)* list, scanner_t* s, ctx_t* ctx,
                               token_t open_token, token_pattern close)
{
    token_t closer;
    if (parser_check(s, ctx, close, &closer))
        return cons_sexpr(*list);

    for (;;) {
        sexpr_t e = parse_expr(s, ctx, 5);
        if (is_error_sexpr(e))
            return free_list_error(list, ctx, e);
        if (!push_sexpr(list, e, ctx)) {
            sexpr_free(&e, &ctx->alloc);
            return free_list_error(list, ctx, atom_sexpr(oom_error(ctx, open_token.line)));
        }

        token_t comma;
        if (!parser_check(s, ctx, op_pattern(OPERATOR_COMMA), &comma))
            break;
    }

    token_t closed = parser_expect_close(s, ctx, open_token, close);
    if (closed.kind == TOKEN_ERROR)
        return free_list_error(list, ctx, atom_sexpr(closed));

    return cons_sexpr(*list);
}

static sexpr_t parse_parens(scanner_t* s, ctx_t* ctx, token_t left_paren, sexpr_t* lhs)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    if (lhs != NULL && !push_sexpr(&list, *lhs, ctx)) {
        sexpr_free(lhs, &ctx->alloc);
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, left_paren.line)));
    }

    return parse_container(&list, s, ctx, left_paren, kind_pattern(TOKEN_RIGHT_PAREN));
}

static sexpr_t parse_list(scanner_t* s, ctx_t* ctx, token_t open, token_pattern close)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t list_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_LIST };
    if (!push_sexpr(&list, atom_sexpr(list_atom), ctx))
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, open.line)));

    return parse_container(&list, s, ctx, open, close);
}

static sexpr_t parse_map(scanner_t* s, ctx_t* ctx, token_t open)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t map_atom = { .kind = TOKEN_SP_FUNCTION, .line = open.line, .fn = FN_MAP };
    if (!push_sexpr(&list, atom_sexpr(map_atom), ctx))
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, open.line)));

    token_t closer;
    if (parser_check(s, ctx, kind_pattern(TOKEN_RIGHT_BRACE), &closer))
        return cons_sexpr(list);

    for (;;) {
        sexpr_t key = parse_expr(s, ctx, 5);
        if (is_error_sexpr(key))
            return free_list_error(&list, ctx, key);
        if (!push_sexpr(&list, key, ctx)) {
            sexpr_free(&key, &ctx->alloc);
            return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, open.line)));
        }

        token_t colon = parser_expect(s, ctx, kind_pattern(TOKEN_COLON));
        if (colon.kind == TOKEN_ERROR)
            return free_list_error(&list, ctx, atom_sexpr(colon));

        sexpr_t value = parse_expr(s, ctx, 5);
        if (is_error_sexpr(value))
            return free_list_error(&list, ctx, value);
        if (!push_sexpr(&list, value, ctx)) {
            sexpr_free(&value, &ctx->alloc);
            return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, open.line)));
        }

        token_t comma;
        if (!parser_check(s, ctx, op_pattern(OPERATOR_COMMA), &comma))
            break;
    }

    token_t closed = parser_expect_close(s, ctx, open, kind_pattern(TOKEN_RIGHT_BRACE));
    if (closed.kind == TOKEN_ERROR)
        return free_list_error(&list, ctx, atom_sexpr(closed));

    return cons_sexpr(list);
}

static sexpr_t parse_bracket(scanner_t* s, ctx_t* ctx, token_t left_bracket, sexpr_t lhs)
{
    sexpr_t rhs = parse_expr(s, ctx, 0);
    if (is_error_sexpr(rhs)) {
        sexpr_free(&lhs, &ctx->alloc);
        return rhs;
    }

    token_t closed = parser_expect_close(s, ctx, left_bracket, kind_pattern(TOKEN_RIGHT_BRACKET));
    if (closed.kind == TOKEN_ERROR) {
        sexpr_free(&lhs, &ctx->alloc);
        sexpr_free(&rhs, &ctx->alloc);
        return atom_sexpr(closed);
    }

    sexpr_t items[] = { atom_sexpr(left_bracket), lhs, rhs };
    return cons_of(ctx, items, 3, left_bracket.line);
}

static sexpr_t parse_block(scanner_t* s, ctx_t* ctx, const token_pattern* ends,
                           int64_t n_ends, int64_t line, token_t* term)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t do_atom = { .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_DO };
    if (!push_sexpr(&list, atom_sexpr(do_atom), ctx))
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, line)));

    for (;;) {
        sexpr_t e = parse_expr(s, ctx, 0);
        if (is_error_sexpr(e))
            return free_list_error(&list, ctx, e);
        if (!push_sexpr(&list, e, ctx)) {
            sexpr_free(&e, &ctx->alloc);
            return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, line)));
        }

        for (int64_t k = 0; k < n_ends; k++) {
            if (parser_check(s, ctx, ends[k], term))
                return cons_sexpr(list);
        }
    }
}

static sexpr_t parse_for(scanner_t* s, ctx_t* ctx, token_t for_token)
{
    token_t id = parser_expect_id(s, ctx);
    if (id.kind == TOKEN_ERROR)
        return atom_sexpr(id);

    token_t in_token = parser_expect(s, ctx, kw_pattern(KEYWORD_IN));
    if (in_token.kind == TOKEN_ERROR)
        return atom_sexpr(in_token);

    sexpr_t iter = parse_expr(s, ctx, 0);
    if (is_error_sexpr(iter))
        return iter;

    sexpr_t cond_items[] = { atom_sexpr(id), iter };
    sexpr_t loop_cond = cons_of(ctx, cond_items, 2, for_token.line);
    if (is_error_sexpr(loop_cond))
        return loop_cond;

    token_t do_token = parser_expect(s, ctx, kw_pattern(KEYWORD_DO));
    if (do_token.kind == TOKEN_ERROR) {
        sexpr_free(&loop_cond, &ctx->alloc);
        return atom_sexpr(do_token);
    }

    token_pattern ends[] = { kw_pattern(KEYWORD_END) };
    token_t term;
    sexpr_t body = parse_block(s, ctx, ends, 1, for_token.line, &term);
    if (is_error_sexpr(body)) {
        sexpr_free(&loop_cond, &ctx->alloc);
        return body;
    }

    sexpr_t items[] = { atom_sexpr(for_token), loop_cond, body };
    return cons_of(ctx, items, 3, for_token.line);
}

static sexpr_t parse_fun_body(scanner_t* s, ctx_t* ctx, sv_vec_t(sexpr_t)* list, token_t fun_token,
                              const token_pattern* ends, int64_t n_ends, token_t* term)
{
    token_t id = parser_expect_id(s, ctx);
    if (id.kind == TOKEN_ERROR)
        return free_list_error(list, ctx, atom_sexpr(id));
    if (!push_sexpr(list, atom_sexpr(id), ctx))
        return free_list_error(list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));

    token_t left_bracket;
    if (parser_check(s, ctx, op_pattern(OPERATOR_LEFT_BRACKET), &left_bracket)) {
        sv_vec_t(sexpr_t) captures = sv_vec_init(sexpr_t);
        sexpr_t closure_vals =
            parse_container(&captures, s, ctx, left_bracket, kind_pattern(TOKEN_RIGHT_BRACKET));
        if (is_error_sexpr(closure_vals))
            return free_list_error(list, ctx, closure_vals);
        if (!push_sexpr(list, closure_vals, ctx)) {
            sexpr_free(&closure_vals, &ctx->alloc);
            return free_list_error(list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));
        }
    }

    token_t left_paren = parser_expect(s, ctx, op_pattern(OPERATOR_LEFT_PAREN));
    if (left_paren.kind == TOKEN_ERROR)
        return free_list_error(list, ctx, atom_sexpr(left_paren));

    sexpr_t args = parse_parens(s, ctx, left_paren, NULL);
    if (is_error_sexpr(args))
        return free_list_error(list, ctx, args);
    if (!push_sexpr(list, args, ctx)) {
        sexpr_free(&args, &ctx->alloc);
        return free_list_error(list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));
    }

    token_t equal = parser_expect(s, ctx, op_pattern(OPERATOR_EQUAL));
    if (equal.kind == TOKEN_ERROR)
        return free_list_error(list, ctx, atom_sexpr(equal));

    sexpr_t body = parse_block(s, ctx, ends, n_ends, fun_token.line, term);
    if (is_error_sexpr(body))
        return free_list_error(list, ctx, body);
    if (!push_sexpr(list, body, ctx)) {
        sexpr_free(&body, &ctx->alloc);
        return free_list_error(list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));
    }

    return cons_sexpr(*list);
}

static sexpr_t parse_fun(scanner_t* s, ctx_t* ctx, token_t fun_token)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    if (!push_sexpr(&list, atom_sexpr(fun_token), ctx))
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));

    token_t pipe_token;
    if (!parser_check(s, ctx, kind_pattern(TOKEN_PIPE), &pipe_token)) {
        token_pattern ends[] = { kw_pattern(KEYWORD_END) };
        token_t term;
        return parse_fun_body(s, ctx, &list, fun_token, ends, 1, &term);
    }

    token_t end_token = pipe_token;
    while (end_token.kind == TOKEN_PIPE) {
        sv_vec_t(sexpr_t) body_list = sv_vec_init(sexpr_t);
        token_pattern ends[] = { kw_pattern(KEYWORD_END), kind_pattern(TOKEN_PIPE) };
        sexpr_t body = parse_fun_body(s, ctx, &body_list, end_token, ends, 2, &end_token);
        if (is_error_sexpr(body))
            return free_list_error(&list, ctx, body);
        if (!push_sexpr(&list, body, ctx)) {
            sexpr_free(&body, &ctx->alloc);
            return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, fun_token.line)));
        }
    }

    return cons_sexpr(list);
}

static sexpr_t parse_if(scanner_t* s, ctx_t* ctx, token_t if_token)
{
    sexpr_t cond = parse_expr(s, ctx, 0);
    if (is_error_sexpr(cond))
        return cond;

    token_t do_token = parser_expect(s, ctx, kw_pattern(KEYWORD_DO));
    if (do_token.kind == TOKEN_ERROR) {
        sexpr_free(&cond, &ctx->alloc);
        return atom_sexpr(do_token);
    }

    token_pattern ends[] = { kw_pattern(KEYWORD_ELSE), kw_pattern(KEYWORD_END) };
    token_t term;
    sexpr_t true_branch = parse_block(s, ctx, ends, 2, if_token.line, &term);
    if (is_error_sexpr(true_branch)) {
        sexpr_free(&cond, &ctx->alloc);
        return true_branch;
    }

    sexpr_t items[] = { atom_sexpr(if_token), cond, true_branch };
    sexpr_t out = cons_of(ctx, items, 3, if_token.line);
    if (is_error_sexpr(out))
        return out;

    if (token_is(term, kw_pattern(KEYWORD_ELSE))) {
        sexpr_t false_branch;
        token_t else_if;
        if (parser_check(s, ctx, fn_pattern(FN_IF), &else_if)) {
            false_branch = parse_if(s, ctx, else_if);
        } else {
            token_pattern end_only[] = { kw_pattern(KEYWORD_END) };
            token_t ignored;
            false_branch = parse_block(s, ctx, end_only, 1, term.line, &ignored);
        }

        if (is_error_sexpr(false_branch)) {
            sexpr_free(&out, &ctx->alloc);
            return false_branch;
        }
        if (!push_sexpr(&out.cons, false_branch, ctx)) {
            sexpr_free(&false_branch, &ctx->alloc);
            sexpr_free(&out, &ctx->alloc);
            return atom_sexpr(oom_error(ctx, if_token.line));
        }
    }

    return out;
}

static sexpr_t parse_operator(scanner_t* s, ctx_t* ctx, token_t start_token, uint8_t min_prec)
{
    sexpr_t lhs;
    if (start_token.kind == TOKEN_OPERATOR) {
        if (start_token.operator == OPERATOR_LEFT_PAREN) {
            lhs = parse_expr(s, ctx, 0);
            if (is_error_sexpr(lhs))
                return lhs;
            token_t closed = parser_expect_close(s, ctx, start_token, kind_pattern(TOKEN_RIGHT_PAREN));
            if (closed.kind == TOKEN_ERROR) {
                sexpr_free(&lhs, &ctx->alloc);
                return atom_sexpr(closed);
            }
        } else if (start_token.operator == OPERATOR_LEFT_BRACKET) {
            lhs = parse_list(s, ctx, start_token, kind_pattern(TOKEN_RIGHT_BRACKET));
            if (is_error_sexpr(lhs))
                return lhs;
        } else if (start_token.operator == OPERATOR_MINUS) {
            sexpr_t rhs = parse_expr(s, ctx, 13);
            if (is_error_sexpr(rhs))
                return rhs;

            sexpr_t items[] = { atom_sexpr(start_token), rhs };
            lhs = cons_of(ctx, items, 2, start_token.line);
            if (is_error_sexpr(lhs))
                return lhs;
        } else {
            return unexpected_token_error(ctx, start_token, "Not a prefix operator");
        }
    } else if (start_token.kind == TOKEN_LITERAL) {
        lhs = atom_sexpr(start_token);
    } else if (start_token.kind == TOKEN_PERCENT_BRACE) {
        lhs = parse_map(s, ctx, start_token);
        if (is_error_sexpr(lhs))
            return lhs;
    } else {
        return unexpected_token_error(ctx, start_token, "Unexpected token");
    }

    for (;;) {
        token_t token = scanner_peek(s, ctx);
        if (token.kind == TOKEN_ERROR) {
            sexpr_free(&lhs, &ctx->alloc);
            return atom_sexpr(token);
        }

        operator_kind op;
        if (token.kind == TOKEN_OPERATOR) {
            op = token.operator;
        } else if (token.kind == TOKEN_LITERAL || token.kind == TOKEN_SP_FUNCTION) {
            if (token.line != start_token.line)
                break;

            sexpr_free(&lhs, &ctx->alloc);
            return unexpected_token_error(ctx, token, "Unexpected token");
        } else {
            break;
        }

        precedence prec = infix_prec(op);
        if (prec.left < min_prec)
            break;

        scanner_next(s, ctx);

        if (prec.has_right) {
            sexpr_t rhs = parse_expr(s, ctx, prec.right);
            if (is_error_sexpr(rhs)) {
                sexpr_free(&lhs, &ctx->alloc);
                return rhs;
            }
            sexpr_t items[] = { atom_sexpr(token), lhs, rhs };
            lhs = cons_of(ctx, items, 3, token.line);
        } else if (op == OPERATOR_LEFT_PAREN) {
            sexpr_t callee = lhs;
            lhs = parse_parens(s, ctx, token, &callee);
        } else {
            lhs = parse_bracket(s, ctx, token, lhs);
        }

        if (is_error_sexpr(lhs))
            return lhs;
    }

    return lhs;
}

static sexpr_t parse_expr(scanner_t* s, ctx_t* ctx, uint8_t min_prec)
{
    token_t token = scanner_next(s, ctx);
    if (token.kind == TOKEN_ERROR)
        return atom_sexpr(token);

    if (token.kind == TOKEN_EOF) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected expression, got end of input at line %" PRId64,
                 token.line);
        return atom_sexpr(parser_error_at(ctx, PARSER_ERROR_EOF, token.line, msg));
    }

    if (token.kind == TOKEN_SP_FUNCTION) {
        switch (token.fn) {
            case FN_IF:
                return parse_if(s, ctx, token);
            case FN_FOR:
                return parse_for(s, ctx, token);
            case FN_FUN:
                return parse_fun(s, ctx, token);
            case FN_LIST: {
                token_t left_paren = parser_expect(s, ctx, op_pattern(OPERATOR_LEFT_PAREN));
                if (left_paren.kind == TOKEN_ERROR)
                    return atom_sexpr(left_paren);
                return parse_list(s, ctx, left_paren, kind_pattern(TOKEN_RIGHT_PAREN));
            }
            case FN_CLASS:
            case FN_MAP:
            case FN_MAPF:
            case FN_MATCH:
            case FN_REDUCE:
            case FN_WHILE:
            case FN_IMPORT: {
                char msg[128];
                snprintf(msg, sizeof(msg), "'%s' is not implemented yet at line %" PRId64,
                         special_fn_text(token.fn), token.line);
                return atom_sexpr(parser_error_at(ctx, PARSER_ERROR_NOT_IMPLEMENTED,
                                                   token.line, msg));
            }
        }
    }

    return parse_operator(s, ctx, token, min_prec);
}

sexpr_t parser_expr(scanner_t* s, ctx_t* ctx)
{
    return parse_expr(s, ctx, 0);
}

sexpr_t parser_program(scanner_t* s, ctx_t* ctx)
{
    sv_vec_t(sexpr_t) list = sv_vec_init(sexpr_t);
    token_t do_atom = { .kind = TOKEN_KEYWORD, .line = 1, .keyword = KEYWORD_DO };
    if (!push_sexpr(&list, atom_sexpr(do_atom), ctx))
        return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, 1)));

    for (;;) {
        token_t peeked = scanner_peek(s, ctx);
        if (peeked.kind == TOKEN_EOF)
            break;
        if (peeked.kind == TOKEN_ERROR)
            return free_list_error(&list, ctx, atom_sexpr(scanner_next(s, ctx)));

        sexpr_t e = parse_expr(s, ctx, 0);
        if (is_error_sexpr(e))
            return free_list_error(&list, ctx, e);
        if (!push_sexpr(&list, e, ctx)) {
            sexpr_free(&e, &ctx->alloc);
            return free_list_error(&list, ctx, atom_sexpr(oom_error(ctx, peeked.line)));
        }
    }

    return cons_sexpr(list);
}
