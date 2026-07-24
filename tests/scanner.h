#ifndef SV_TESTS_SCANNER_H
#define SV_TESTS_SCANNER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/scanner.h"

static ctx_t sv_test_scan_ctx(void)
{
   return (ctx_t){
      .a = sv_gpa,
      .logger = sv_std_logger,
      .err = { 0 },
   };
}

static void sv_test_scan_kinds(sv_testing_t* t, const char* src, const token_kind* kinds, int64_t n)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   for (int64_t k = 0; k < n; k++) {
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == kinds[k], "kind %lld of \"%s\"", (long long)k, src);
   }
   sv_test_run_msg(t, scanner_next(&s, &ctx).kind == TOKEN_EOF, "EOF after \"%s\"", src);
}

static void sv_test_scan_error(sv_testing_t* t, const char* src, scanner_error_kind expected)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   token_t tok = scanner_next(&s, &ctx);
   while (tok.kind != TOKEN_EOF && tok.kind != TOKEN_ERROR)
      tok = scanner_next(&s, &ctx);

   sv_test_run_msg(t, tok.kind == TOKEN_ERROR, "expected error for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.error_code == (int)expected, "error code for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.msg.size > 0, "error msg for \"%s\"", src);
   sv_str_deinit(&ctx.err.msg, &ctx.a);
}

static inline void sv_test_scanner_punctuation(sv_testing_t* t)
{
   const token_kind singles[] = {
      TOKEN_OPERATOR, TOKEN_RIGHT_PAREN, TOKEN_OPERATOR, TOKEN_RIGHT_BRACKET,
      TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE, TOKEN_SEMICOLON, TOKEN_HASH,
      TOKEN_OPERATOR, TOKEN_OPERATOR, TOKEN_OPERATOR, TOKEN_OPERATOR,
      TOKEN_OPERATOR, TOKEN_OPERATOR, TOKEN_PIPE,
   };
   sv_test_scan_kinds(t, "()[]{};#.,+-*/|", singles, 15);

   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("|> != >= <= == > < = |"));
   const operator_kind ops[] = {
      OPERATOR_PIPE_FORWARD, OPERATOR_BANG_EQUAL, OPERATOR_GREATER_EQUAL,
      OPERATOR_LESS_EQUAL, OPERATOR_EQUAL_EQUAL, OPERATOR_GREATER,
      OPERATOR_LESS, OPERATOR_EQUAL,
   };
   for (int64_t k = 0; k < 8; k++) {
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run(t, tok.kind == TOKEN_OPERATOR);
      sv_test_run_msg(t, tok.operator == ops[k], "operator %lld", (long long)k);
   }
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_PIPE);
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
}

static inline void sv_test_scanner_words(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();

   for (int k = KEYWORD_AND; k <= KEYWORD_SELF; k++) {
      scanner_t s = scanner_init(sv_str_init(keyword_text((keyword_kind)k)));
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == TOKEN_KEYWORD && tok.keyword == (keyword_kind)k,
                      "keyword round-trip %d", k);
   }

   for (int k = FN_CLASS; k <= FN_IMPORT; k++) {
      scanner_t s = scanner_init(sv_str_init(special_fn_text((special_fn_kind)k)));
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == TOKEN_SP_FUNCTION && tok.fn == (special_fn_kind)k,
                      "special fn round-trip %d", k);
   }

   scanner_t s = scanner_init(sv_str_init("nil true false"));
   token_t tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_NIL);
   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_TRUE);
   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_FALSE);
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
}

static inline void sv_test_scanner_identifiers(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("foo _bar x1 caf\xC3\xA9 _ selfish"));
   const char* expected[] = { "foo", "_bar", "x1", "caf\xC3\xA9", "_", "selfish" };

   for (int64_t k = 0; k < 6; k++) {
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == TOKEN_LITERAL, "identifier kind %lld", (long long)k);
      sv_test_run_msg(t, tok.literal.kind == LITERAL_IDENTIFIER, "identifier lit %lld", (long long)k);
      sv_test_run_msg(t, sv_str_comp(tok.literal.literal, sv_str_init(expected[k])),
                      "identifier text %lld", (long long)k);
   }
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
}

static inline void sv_test_scanner_numbers(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("1 3.14 0.5 42"));
   const double expected[] = { 1.0, 3.14, 0.5, 42.0 };

   for (int64_t k = 0; k < 4; k++) {
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == TOKEN_LITERAL, "number kind %lld", (long long)k);
      sv_test_run_msg(t, tok.literal.kind == LITERAL_NUMBER, "number lit %lld", (long long)k);
      sv_test_run_msg(t, tok.literal.number == expected[k], "number value %lld", (long long)k);
   }
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);

   scanner_t dot = scanner_init(sv_str_init("1."));
   token_t tok = scanner_next(&dot, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.number == 1.0);
   tok = scanner_next(&dot, &ctx);
   sv_test_run(t, tok.kind == TOKEN_OPERATOR && tok.operator == OPERATOR_DOT);
   sv_test_run(t, scanner_next(&dot, &ctx).kind == TOKEN_EOF);

   sv_test_scan_error(t, "1..2", SCANNER_ERROR_INVALID_NUMBER);
   sv_test_scan_error(t, "1.2.3", SCANNER_ERROR_INVALID_NUMBER);
   sv_test_scan_error(t, "1.x", SCANNER_ERROR_INVALID_NUMBER);
}

static inline void sv_test_scanner_strings(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("\"hello\" \"\" \"a\nb\""));

   token_t tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_STRING);
   sv_test_run(t, sv_str_comp(tok.literal.str, sv_str_init("hello")));
   sv_test_run(t, tok.line == 1);

   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.str.size == 0);

   tok = scanner_next(&s, &ctx);
   sv_test_run(t, sv_str_comp(tok.literal.str, sv_str_init("a\nb")));
   sv_test_run(t, tok.line == 2);

   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);

   sv_test_scan_error(t, "\"abc", SCANNER_ERROR_UNCLOSED_STRING);
}

static inline void sv_test_scanner_lines(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("a\nb\n\nc"));

   sv_test_run(t, scanner_next(&s, &ctx).line == 1);
   sv_test_run(t, scanner_next(&s, &ctx).line == 2);
   sv_test_run(t, scanner_next(&s, &ctx).line == 4);
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
}

static inline void sv_test_scanner_peek(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("fun x"));

   token_t peeked = scanner_peek(&s, &ctx);
   sv_test_run(t, peeked.kind == TOKEN_SP_FUNCTION && peeked.fn == FN_FUN);
   peeked = scanner_peek(&s, &ctx);
   sv_test_run(t, peeked.kind == TOKEN_SP_FUNCTION);

   token_t tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_SP_FUNCTION && tok.fn == FN_FUN);

   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_IDENTIFIER);

   sv_test_run(t, scanner_peek(&s, &ctx).kind == TOKEN_EOF);
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
   sv_test_run(t, scanner_next(&s, &ctx).kind == TOKEN_EOF);
}

static inline void sv_test_scanner_errors(sv_testing_t* t)
{
   sv_test_scan_error(t, "@", SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_scan_error(t, "!x", SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_scan_error(t, "!", SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_scan_error(t, "x @", SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_scan_error(t, "\xFF", SCANNER_ERROR_INVALID_UTF8);
   sv_test_scan_error(t, "abc \xC3", SCANNER_ERROR_INVALID_UTF8);
   sv_test_scan_error(t, "\xED\xA0\x80", SCANNER_ERROR_INVALID_UTF8);
   sv_test_scan_error(t, "\xC0\xAF", SCANNER_ERROR_INVALID_UTF8);

   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("@"));
   token_t tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_ERROR);
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 1"));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "@"));
   sv_str_deinit(&ctx.err.msg, &ctx.a);
}

static inline void sv_test_scanner_eof(sv_testing_t* t)
{
   ctx_t ctx = sv_test_scan_ctx();

   scanner_t empty = scanner_init(sv_str_init(""));
   sv_test_run(t, scanner_next(&empty, &ctx).kind == TOKEN_EOF);

   scanner_t blank = scanner_init(sv_str_init("  \n\t "));
   token_t tok = scanner_next(&blank, &ctx);
   sv_test_run(t, tok.kind == TOKEN_EOF);
   sv_test_run(t, tok.line == 2);
}

static inline void sv_test_scanner(sv_testing_t* t)
{
   sv_test_scanner_punctuation(t);
   sv_test_scanner_words(t);
   sv_test_scanner_identifiers(t);
   sv_test_scanner_numbers(t);
   sv_test_scanner_strings(t);
   sv_test_scanner_lines(t);
   sv_test_scanner_peek(t);
   sv_test_scanner_errors(t);
   sv_test_scanner_eof(t);
}

#endif
