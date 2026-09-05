#ifndef LONG_SCANNER_H
#define LONG_SCANNER_H

#include "ctx.h"
#include "token.h"

typedef enum {
    SCANNER_ERROR_UNCLOSED_STRING = 1,
    SCANNER_ERROR_INVALID_NUMBER,
    SCANNER_ERROR_UNKNOWN_TOKEN,
    SCANNER_ERROR_INVALID_UTF8,
} scanner_error_kind;

typedef struct {
    sv_str_t source;
    int64_t i;
    int64_t line;
    token_t next_token;
    bool has_next_token;
} scanner_t;

/**
 * Creates a scanner over `source`. The scanner is a view: it does not own
 * `source` and allocates nothing. The source is validated as UTF-8 lazily,
 * while scanning. `#` starts a comment that runs to the end of the line.
 */
scanner_t scanner_init(sv_str_t source);

/**
 * Returns the next token, consuming it. At the end of the input a TOKEN_EOF
 * token is returned. On error a TOKEN_ERROR token is returned and the error
 * information is placed in `ctx->err`: `error_code` is a scanner_error_kind
 * and `msg` is allocated with `ctx->a` and owned by the caller.
 */
token_t scanner_next(scanner_t* s, ctx_t* ctx);

/**
 * Returns the next token without consuming it. EOF and errors behave like
 * scanner_next.
 */
token_t scanner_peek(scanner_t* s, ctx_t* ctx);

#endif
