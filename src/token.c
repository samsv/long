#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool add_text(sv_str_builder* b, const char* text, const sv_allocator_t* a)
{
    return sv_strb_add(b, text, (int64_t)strlen(text), a) != -1;
}

static const char* operator_text(operator_kind op)
{
    switch (op) {
        case OPERATOR_DOT: return ".";
        case OPERATOR_MINUS: return "-";
        case OPERATOR_PLUS: return "+";
        case OPERATOR_PIPE_FORWARD: return "|>";
        case OPERATOR_STAR: return "*";
        case OPERATOR_SLASH: return "/";
        case OPERATOR_LEFT_PAREN: return "(";
        case OPERATOR_LEFT_BRACKET: return "[";
        case OPERATOR_BANG_EQUAL: return "!=";
        case OPERATOR_EQUAL: return "=";
        case OPERATOR_EQUAL_EQUAL: return "==";
        case OPERATOR_GREATER: return ">";
        case OPERATOR_GREATER_EQUAL: return ">=";
        case OPERATOR_LESS: return "<";
        case OPERATOR_LESS_EQUAL: return "<=";
    }
    return "";
}

const char* special_fn_text(special_fn_kind fn)
{
    switch (fn) {
        case FN_FUN: return "fun";
        case FN_FOR: return "for";
        case FN_IF: return "if";
        case FN_LIST: return "list";
        case FN_MAP: return "map";
        case FN_HASHMAP: return "hashmap";
        case FN_RECORD: return "record";
        case FN_TUPLE: return "tuple";
        case FN_MAPF: return "mapf";
        case FN_MATCH: return "match";
        case FN_REDUCE: return "reduce";
        case FN_WHILE: return "while";
        case FN_IMPORT: return "import";
        case FN_LENGTH: return "length";
        case FN_RECORD_GET_OR_NIL: return "record-get?";
        case FN_HASHMAP_GET_OR_NIL: return "hashmap-get?";
    }
    return "";
}

const char* keyword_text(keyword_kind keyword)
{
    switch (keyword) {
        case KEYWORD_AND: return "and";
        case KEYWORD_ELSE: return "else";
        case KEYWORD_DO: return "do";
        case KEYWORD_END: return "end";
        case KEYWORD_IN: return "in";
        case KEYWORD_OR: return "or";
        case KEYWORD_NOT: return "not";
        case KEYWORD_SELF: return "self";
        case KEYWORD_WHEN: return "when";
    }
    return "";
}

static bool number_format_builder(double number, sv_str_builder* b, const sv_allocator_t* a)
{
    if (number == 0)
        return sv_strb_add_char(b, '0', a) != -1;

    char buf[32];
    int n = 0;
    int precision = 1;
    for (; precision < 17; precision++) {
        n = snprintf(buf, sizeof(buf), "%.*g", precision, number);
        if (n < 0 || n > (int)sizeof(buf) - 3)
            return false;
        if (strtod(buf, NULL) == number)
            break;
    }
    if (precision == 17) {
        n = snprintf(buf, sizeof(buf), "%.17g", number);
        if (n < 0 || n > (int)sizeof(buf) - 3)
            return false;
    }

    const char* e = strchr(buf, 'e');
    if (e != NULL) {
        int exp = (int)strtol(&e[1], NULL, 10);
        if (exp >= -4 && exp < 16) {
            n = snprintf(buf, sizeof(buf), "%.*g", exp + 1, number);
            if (n < 0 || n > (int)sizeof(buf) - 3)
                return false;
        }
    } else if (strpbrk(buf, ".n") == NULL && n - (buf[0] == '-') >= 17) {
        n = snprintf(buf, sizeof(buf), "%.*e", precision - 1, number);
        if (n < 0 || n > (int)sizeof(buf) - 3)
            return false;
    }

    return sv_strb_add(b, buf, n, a) != -1;
}

static bool literal_format_builder(literal_t lit, sv_str_builder* b, const sv_allocator_t* a)
{
    switch (lit.kind) {
        case LITERAL_IDENTIFIER:
            return sv_strb_add(b, lit.literal.chars, lit.literal.size, a) != -1;
        case LITERAL_STRING:
            if (sv_strb_add_char(b, '"', a) == -1)
                return false;
            if (sv_strb_add(b, lit.str.chars, lit.str.size, a) == -1)
                return false;
            return sv_strb_add_char(b, '"', a) != -1;
        case LITERAL_NUMBER:
            return number_format_builder(lit.number, b, a);
        case LITERAL_NIL: return add_text(b, "nil", a);
        case LITERAL_TRUE: return add_text(b, "true", a);
        case LITERAL_FALSE: return add_text(b, "false", a);
    }
    return false;
}

bool token_format_builder(token_t token, sv_str_builder* b, const sv_allocator_t* a)
{
    switch (token.kind) {
        case TOKEN_RIGHT_PAREN: return add_text(b, ")", a);
        case TOKEN_RIGHT_BRACE: return add_text(b, "}", a);
        case TOKEN_RIGHT_BRACKET: return add_text(b, "]", a);
        case TOKEN_LEFT_BRACE: return add_text(b, "{", a);
        case TOKEN_PERCENT_BRACE: return add_text(b, "%{", a);
        case TOKEN_COLON: return add_text(b, ":", a);
        case TOKEN_DOT_DOT: return add_text(b, "..", a);
        case TOKEN_COMMA: return add_text(b, ",", a);
        case TOKEN_SEMICOLON: return add_text(b, ";", a);
        case TOKEN_HASH: return add_text(b, "#", a);
        case TOKEN_PIPE: return add_text(b, "|", a);
        case TOKEN_EOF: return add_text(b, "<EOF>", a);
        case TOKEN_ERROR: return add_text(b, "<ERROR>", a);
        case TOKEN_OPERATOR: return add_text(b, operator_text(token.operator), a);
        case TOKEN_SP_FUNCTION: return add_text(b, special_fn_text(token.fn), a);
        case TOKEN_KEYWORD: return add_text(b, keyword_text(token.keyword), a);
        case TOKEN_LITERAL: return literal_format_builder(token.literal, b, a);
    }
    return false;
}

sv_str_t token_format(token_t token, const sv_allocator_t* a)
{
    sv_str_builder b = sv_strb_init();
    if (!token_format_builder(token, &b, a)) {
        sv_strb_deinit(&b, a);
        return sv_str_err();
    }

    return sv_strb_to_str(&b);
}
