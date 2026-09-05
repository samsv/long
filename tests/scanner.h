#ifndef SV_TESTS_SCANNER_H
#define SV_TESTS_SCANNER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/scanner.h"

static inline ctx_t sv_test_scan_ctx(void)
{
   return (ctx_t){
      .alloc = sv_gpa,
      .logger = sv_std_logger,
      .err = { 0 },
   };
}

static inline void sv_test_scan_kinds(sv_testing_t* t, const char* src, const token_kind* kinds, int64_t n)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   for (int64_t k = 0; k < n; k++) {
      token_t tok = scanner_next(&s, &ctx);
      sv_test_run_msg(t, tok.kind == kinds[k], "kind %lld of \"%s\"", (long long)k, src);
   }
   sv_test_run_msg(t, scanner_next(&s, &ctx).kind == TOKEN_EOF, "EOF after \"%s\"", src);
}

static inline void sv_test_scan_error(sv_testing_t* t, const char* src, scanner_error_kind expected)
{
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   token_t tok = scanner_next(&s, &ctx);
   while (tok.kind != TOKEN_EOF && tok.kind != TOKEN_ERROR)
      tok = scanner_next(&s, &ctx);

   sv_test_run_msg(t, tok.kind == TOKEN_ERROR, "expected error for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.error_code == (int)expected, "error code for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.msg.size > 0, "error msg for \"%s\"", src);
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);
}

static inline void sv_test_scanner_comments(sv_testing_t* t)
{
   const token_kind two[] = { TOKEN_LITERAL, TOKEN_LITERAL };
   sv_test_scan_kinds(t, "1 # trailing\n2", two, 2);
   sv_test_scan_kinds(t, "1 #no space\n2", two, 2);
   sv_test_scan_kinds(t, "# a\n# b\n1 2", two, 2);
   sv_test_scan_kinds(t, "1\n2 # at EOF without a newline", two, 2);
   sv_test_scan_kinds(t, "1 # ( \" @ # still one comment\n2", two, 2);
   sv_test_scan_kinds(t, "# only a comment", NULL, 0);
   sv_test_scan_kinds(t, "#", NULL, 0);

   /* Comment lines still count. */
   ctx_t ctx = sv_test_scan_ctx();
   scanner_t s = scanner_init(sv_str_init("a # one\n# two\n\n# four\nb"));
   token_t tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.line == 1);
   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.line == 5);
   tok = scanner_next(&s, &ctx);
   sv_test_run(t, tok.kind == TOKEN_EOF && tok.line == 5);

   /* A `#` inside a string is content, and the string still closes. */
   scanner_t str = scanner_init(sv_str_init("\"a#b\" 1"));
   tok = scanner_next(&str, &ctx);
   sv_test_run(t, tok.kind == TOKEN_LITERAL && tok.literal.kind == LITERAL_STRING);
   sv_test_run(t, sv_str_comp(tok.literal.str, sv_str_init("a#b")));
   sv_test_run(t, scanner_next(&str, &ctx).kind == TOKEN_LITERAL);
   sv_test_run(t, scanner_next(&str, &ctx).kind == TOKEN_EOF);

   /* The file must be valid UTF-8 even inside a comment. */
   sv_test_scan_error(t, "# \xFF\n1", SCANNER_ERROR_INVALID_UTF8);
}

static inline void sv_test_scanner(sv_testing_t* t)
{
   sv_test_scanner_comments(t);
}

#endif
