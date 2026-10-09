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
 * Parses one expression into a tree allocated with the allocator.
 */
sexpr_t parser_expr(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a);

/**
 * Parses expressions until the end of the input into a `(do ...)` cons.
 */
sexpr_t parser_program(scanner_t* s, ctx_t* ctx, const sv_allocator_t* a);

/**
 * Skips statement separators: semicolons and newlines.
 */
void parser_skip_semicolons(scanner_t* s, ctx_t* ctx);

#endif
