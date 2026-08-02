#include "scanner.h"
#include "unicode_alphabetic_table.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODEPOINT_EOF UINT32_MAX
#define CODEPOINT_INVALID (UINT32_MAX - 1)

static uint32_t next_codepoint(scanner_t* s)
{
    if (s->i >= s->source.size)
        return CODEPOINT_EOF;

    const uint8_t* bytes = (const uint8_t*)s->source.chars;
    uint8_t b0 = bytes[s->i];
    if (b0 < 0x80) {
        s->i++;
        return b0;
    }

    int64_t len;
    uint32_t c;
    uint32_t min;
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        c = b0 & 0x1F;
        min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        c = b0 & 0x0F;
        min = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        c = b0 & 0x07;
        min = 0x10000;
    } else {
        return CODEPOINT_INVALID;
    }

    if (s->i + len > s->source.size)
        return CODEPOINT_INVALID;

    for (int64_t k = 1; k < len; k++) {
        uint8_t b = bytes[s->i + k];
        if ((b & 0xC0) != 0x80)
            return CODEPOINT_INVALID;
        c = (c << 6) | (b & 0x3F);
    }

    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return CODEPOINT_INVALID;

    s->i += len;
    return c;
}

static bool next_char_if_eq(scanner_t* s, char c)
{
    if (s->i >= s->source.size || s->source.chars[s->i] != c)
        return false;
    s->i++;
    return true;
}

static bool is_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

/**
 * The Unicode White_Space property (PropList.txt). This set is small and
 * effectively frozen.
 */
static bool is_whitespace(uint32_t c)
{
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case 0x000B:
        case 0x000C:
        case 0x0085:
        case 0x00A0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

static token_t token_of(const scanner_t* s, token_kind kind)
{
    return (token_t){ .kind = kind, .line = s->line };
}

static token_t operator_of(const scanner_t* s, operator_kind op)
{
    return (token_t){ .kind = TOKEN_OPERATOR, .line = s->line, .operator = op };
}

static token_t error_token(scanner_t* s, ctx_t* ctx, scanner_error_kind kind, const char* msg)
{
    ctx->err.error_code = (int)kind;
    ctx->err.msg = sv_str_copy(sv_str_init(msg), &ctx->alloc);
    return token_of(s, TOKEN_ERROR);
}

static token_t slice_error_token(scanner_t* s, ctx_t* ctx, scanner_error_kind kind,
                                 const char* what, int64_t start)
{
    int64_t len = s->i - start;
    const char* ellipsis = "";
    if (len > 180) {
        len = 180;
        ellipsis = "...";
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "%s '%.*s%s' at line %" PRId64,
             what, (int)len, &s->source.chars[start], ellipsis, s->line);
    return error_token(s, ctx, kind, msg);
}

static token_t utf8_error_token(scanner_t* s, ctx_t* ctx)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Invalid UTF-8 at line %" PRId64, s->line);
    return error_token(s, ctx, SCANNER_ERROR_INVALID_UTF8, msg);
}

static token_t scan_string(scanner_t* s, ctx_t* ctx)
{
    int64_t initial_i = s->i;
    for (;;) {
        uint32_t c = next_codepoint(s);
        if (c == CODEPOINT_INVALID)
            break;

        if (c == CODEPOINT_EOF) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Unclosed string at line %" PRId64, s->line);
            return error_token(s, ctx, SCANNER_ERROR_UNCLOSED_STRING, msg);
        }

        if (c == '"')
            return (token_t){
                .kind = TOKEN_LITERAL,
                .line = s->line,
                .literal = {
                    .kind = LITERAL_STRING,
                    .str = sv_str_slice(s->source, initial_i, s->i - 1),
                },
            };

        if (c == '\n')
            s->line++;
    }

    return utf8_error_token(s, ctx);
}

static token_t scan_number(scanner_t* s, ctx_t* ctx, int64_t initial_i)
{
    bool has_dot = false;
    int64_t last_i = initial_i + 1;
    uint32_t last = 0;
    for (;;) {
        uint32_t c = next_codepoint(s);
        if (c == CODEPOINT_EOF || c == CODEPOINT_INVALID)
            break;

        if (is_digit(c)) {
            last_i = s->i;
            last = c;
            continue;
        }

        if (c != '.') {
            if (last == '.')
                return slice_error_token(s, ctx, SCANNER_ERROR_INVALID_NUMBER,
                                         "Invalid number literal", initial_i);
            break;
        }

        if (has_dot)
            return slice_error_token(s, ctx, SCANNER_ERROR_INVALID_NUMBER,
                                     "Invalid number literal", initial_i);
        has_dot = true;
        last = '.';
    }

    s->i = last_i;

    char buf[184];
    int64_t len = last_i - initial_i;
    if (len >= (int64_t)sizeof(buf))
        return slice_error_token(s, ctx, SCANNER_ERROR_INVALID_NUMBER,
                                 "Number literal too long", initial_i);

    memcpy(buf, &s->source.chars[initial_i], len);
    buf[len] = '\0';

    return (token_t){
        .kind = TOKEN_LITERAL,
        .line = s->line,
        .literal = {
            .kind = LITERAL_NUMBER,
            .number = strtod(buf, NULL),
        },
    };
}

static token_t scan_literal(scanner_t* s, int64_t initial_i)
{
    int64_t last_i = s->i;
    for (;;) {
        uint32_t c = next_codepoint(s);
        if (c == CODEPOINT_EOF || c == CODEPOINT_INVALID)
            break;
        if (!unicode_alphabetic(c) && !is_digit(c) && c != '_')
            break;
        last_i = s->i;
    }

    s->i = last_i;
    sv_str_t str = sv_str_slice(s->source, initial_i, last_i);

    for (int k = KEYWORD_AND; k <= KEYWORD_SELF; k++) {
        if (sv_str_comp(str, sv_str_init(keyword_text((keyword_kind)k))))
            return (token_t){ .kind = TOKEN_KEYWORD, .line = s->line, .keyword = (keyword_kind)k };
    }

    for (int k = FN_CLASS; k <= FN_IMPORT; k++) {
        if (sv_str_comp(str, sv_str_init(special_fn_text((special_fn_kind)k))))
            return (token_t){ .kind = TOKEN_SP_FUNCTION, .line = s->line, .fn = (special_fn_kind)k };
    }

    literal_t lit;
    if (sv_str_comp(str, sv_str_init("nil")))
        lit = (literal_t){ .kind = LITERAL_NIL };
    else if (sv_str_comp(str, sv_str_init("true")))
        lit = (literal_t){ .kind = LITERAL_TRUE };
    else if (sv_str_comp(str, sv_str_init("false")))
        lit = (literal_t){ .kind = LITERAL_FALSE };
    else
        lit = (literal_t){ .kind = LITERAL_IDENTIFIER, .literal = str };

    return (token_t){ .kind = TOKEN_LITERAL, .line = s->line, .literal = lit };
}

scanner_t scanner_init(sv_str_t source)
{
    return (scanner_t){
        .source = source,
        .i = 0,
        .line = 1,
        .next_token = { .kind = TOKEN_EOF, .line = 1 },
        .has_next_token = false,
    };
}

token_t scanner_next(scanner_t* s, ctx_t* ctx)
{
    if (s->has_next_token) {
        s->has_next_token = false;
        return s->next_token;
    }

    int64_t initial_i = s->i;
    uint32_t c = next_codepoint(s);

    while (c != CODEPOINT_EOF && c != CODEPOINT_INVALID && is_whitespace(c)) {
        if (c == '\n')
            s->line++;
        initial_i = s->i;
        c = next_codepoint(s);
    }

    if (c == CODEPOINT_EOF)
        return token_of(s, TOKEN_EOF);

    if (c == CODEPOINT_INVALID)
        return utf8_error_token(s, ctx);

    switch (c) {
        case '(': return operator_of(s, OPERATOR_LEFT_PAREN);
        case ')': return token_of(s, TOKEN_RIGHT_PAREN);
        case '[': return operator_of(s, OPERATOR_LEFT_BRACKET);
        case ']': return token_of(s, TOKEN_RIGHT_BRACKET);
        case '{': return token_of(s, TOKEN_LEFT_BRACE);
        case '}': return token_of(s, TOKEN_RIGHT_BRACE);
        case '+': return operator_of(s, OPERATOR_PLUS);
        case '-': return operator_of(s, OPERATOR_MINUS);
        case '*': return operator_of(s, OPERATOR_STAR);
        case '/': return operator_of(s, OPERATOR_SLASH);
        case '#': return token_of(s, TOKEN_HASH);
        case '.': return operator_of(s, OPERATOR_DOT);
        case ',': return token_of(s, TOKEN_COMMA);
        case ';': return token_of(s, TOKEN_SEMICOLON);
        case ':': return token_of(s, TOKEN_COLON);
        case '%':
            if (next_char_if_eq(s, '{'))
                return token_of(s, TOKEN_PERCENT_BRACE);
            next_codepoint(s);
            return slice_error_token(s, ctx, SCANNER_ERROR_UNKNOWN_TOKEN,
                                     "Unknown token", initial_i);
        case '|':
            if (next_char_if_eq(s, '>'))
                return operator_of(s, OPERATOR_PIPE_FORWARD);
            return token_of(s, TOKEN_PIPE);
        case '!':
            if (next_char_if_eq(s, '='))
                return operator_of(s, OPERATOR_BANG_EQUAL);
            next_codepoint(s);
            return slice_error_token(s, ctx, SCANNER_ERROR_UNKNOWN_TOKEN,
                                     "Unknown token", initial_i);
        case '>':
            if (next_char_if_eq(s, '='))
                return operator_of(s, OPERATOR_GREATER_EQUAL);
            return operator_of(s, OPERATOR_GREATER);
        case '<':
            if (next_char_if_eq(s, '='))
                return operator_of(s, OPERATOR_LESS_EQUAL);
            return operator_of(s, OPERATOR_LESS);
        case '=':
            if (next_char_if_eq(s, '='))
                return operator_of(s, OPERATOR_EQUAL_EQUAL);
            return operator_of(s, OPERATOR_EQUAL);
        case '"':
            return scan_string(s, ctx);
        default:
            if (is_digit(c))
                return scan_number(s, ctx, initial_i);
            if (unicode_alphabetic(c) || c == '_')
                return scan_literal(s, initial_i);
            return slice_error_token(s, ctx, SCANNER_ERROR_UNKNOWN_TOKEN,
                                     "Unknown token", initial_i);
    }
}

token_t scanner_peek(scanner_t* s, ctx_t* ctx)
{
    if (!s->has_next_token) {
        s->next_token = scanner_next(s, ctx);
        s->has_next_token = true;
    }

    return s->next_token;
}
