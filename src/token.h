#ifndef LONG_TOKEN_H
#define LONG_TOKEN_H

#include "std/allocator.h"

typedef enum {
    TOKEN_OPERATOR,
    TOKEN_SP_FUNCTION,
    TOKEN_LITERAL,
    TOKEN_KEYWORD,

    TOKEN_RIGHT_PAREN,
    TOKEN_RIGHT_BRACE,
    TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE,
    TOKEN_SEMICOLON,
    TOKEN_HASH,
    TOKEN_PIPE,
} token_kind;

typedef enum {
    OPERATOR_DOT,
    OPERATOR_MINUS,
    OPERATOR_PLUS,
    OPERATOR_PIPE_FORWARD,
    OPERATOR_STAR,
    OPERATOR_SLASH,

    OPERATOR_COMMA,
    OPERATOR_LEFT_PAREN,
    OPERATOR_LEFT_BRACKET,

    OPERATOR_BANG_EQUAL,
    OPERATOR_EQUAL,
    OPERATOR_EQUAL_EQUAL,
    OPERATOR_GREATER,
    OPERATOR_GREATER_EQUAL,
    OPERATOR_LESS,
    OPERATOR_LESS_EQUAL,
} operator_kind;

typedef enum {
    FN_CLASS,
    FN_FUN,
    FN_FOR,
    FN_IF,
    FN_LIST,
    FN_MAP,
    FN_MAPF,
    FN_MATCH,
    FN_REDUCE,
    FN_WHILE,
    FN_IMPORT,
} special_fn_kind;

typedef enum {
    KEYWORD_AND,
    KEYWORD_ELSE,
    KEYWORD_DO,
    KEYWORD_END,
    KEYWORD_IN,
    KEYWORD_OR,
    KEYWORD_SELF,
} keyword_kind;

typedef struct {
    token_kind kind;
    union {
        operator_kind operator;
        special_fn_kind fn;
        keyword_kind keyword;
    };
} token_t;


const char* token_format(token_t, sv_allocator_t*);

#endif
