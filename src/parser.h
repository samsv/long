#ifndef LONG_PARSER_H
#define LONG_PARSER_H

#include "ctx.h"
#include "scanner.h"
#include "sexpr.h"

typedef enum {
    PARSER_ERROR_UNEXPECTED_TOKEN = 1,
    PARSER_ERROR_EOF,
    PARSER_ERROR_NOT_IMPLEMENTED,
    PARSER_ERROR_OOM,
} parser_error_kind;

/**
 * Parses one expression. The returned tree is allocated with `ctx->a` and
 * freed with sexpr_free; atoms are views into the scanner's source. On error
 * returns an S_ATOM holding a TOKEN_ERROR token and sets `ctx->err`.
 */
sexpr_t parser_expr(scanner_t* s, ctx_t* ctx);

/**
 * Parses expressions until the end of the input into a `(do ...)` cons.
 * Errors behave like parser_expr.
 */
sexpr_t parser_program(scanner_t* s, ctx_t* ctx);

#endif
