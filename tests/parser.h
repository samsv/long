#ifndef SV_TESTS_PARSER_H
#define SV_TESTS_PARSER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/parser.h"
#include "string.h"

static inline ctx_t sv_test_parse_ctx(void)
{
   return (ctx_t){
      .alloc = sv_gpa,
      .logger = sv_std_logger,
      .err = { 0 },
   };
}

static inline void sv_test_parse_error(sv_testing_t* t, const char* src, int expected_code)
{
   ctx_t ctx = sv_test_parse_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   sexpr_t e = parser_expr(&s, &ctx);

   sv_test_run_msg(t, e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR,
                   "expected parse error for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.error_code == expected_code, "error code for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.msg.size > 0, "error msg for \"%s\"", src);
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);
}

static inline void sv_test_parser_exprs(sv_testing_t* t)
{
   sv_test_parse_error(t, ", 2", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "()", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "(1, 2", PARSER_ERROR_EOF);
}

static inline void sv_test_parser_program_fn(sv_testing_t* t)
{
   ctx_t ctx = sv_test_parse_ctx();

   scanner_t s = scanner_init(sv_str_init("1 + 1\n2 * 2"));
   sexpr_t e = parser_program(&s, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sexpr_free(&e, &ctx.alloc);

   scanner_t empty = scanner_init(sv_str_init(""));
   sexpr_t ep = parser_program(&empty, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sexpr_free(&ep, &ctx.alloc);

   scanner_t bad = scanner_init(sv_str_init("1 + 1\n)"));
   sexpr_t eb = parser_program(&bad, &ctx);
   sv_test_run(t, eb.tag == S_ATOM && eb.atom.kind == TOKEN_ERROR);
   sv_test_run(t, ctx.err.error_code == (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);

   ctx.err = (error_t){ 0 };
   scanner_t commas = scanner_init(sv_str_init("x, y = 1, 2"));
   sexpr_t ec = parser_program(&commas, &ctx);
   sv_test_run(t, ec.tag == S_ATOM && ec.atom.kind == TOKEN_ERROR);
   sv_test_run(t, ctx.err.error_code == (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);

   ctx.err = (error_t){ 0 };
   scanner_t semis = scanner_init(sv_str_init("x = 1; y = 2;"));
   sexpr_t es = parser_program(&semis, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sexpr_free(&es, &ctx.alloc);
}

static inline void sv_test_parser_maps(sv_testing_t* t)
{
   sv_test_parse_error(t, "x = ;", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "f(1; 2)", PARSER_ERROR_UNEXPECTED_TOKEN);

   sv_test_parse_error(t, "{1: 2}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "{x 1}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "{x: 1", PARSER_ERROR_EOF);

   sv_test_parse_error(t, "%{1, 2}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "%{1: }", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "%{1: 2", PARSER_ERROR_EOF);
   sv_test_parse_error(t, "% 1", SCANNER_ERROR_UNKNOWN_TOKEN);
}

static inline void sv_test_parser_match(sv_testing_t* t)
{
   sv_test_parse_error(t, "match x | 1 do 2", PARSER_ERROR_EOF);
   sv_test_parse_error(t, "match x | 1 2 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | = 2 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [..xs] do 1 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [.., t] do 1 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [..t, 1] do 1 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | %{k: 1} do 1 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 + 1 do 2 end", PARSER_ERROR_UNEXPECTED_TOKEN);
}

static inline void sv_test_parser_errors(sv_testing_t* t)
{
   sv_test_parse_error(t, "", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "(1 + 2", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "if x do y", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "fun f(x) do x", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, ")", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "* 3", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "1 2", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "for x xs do x end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun 1(x) do x end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);

   /* `=` is no longer a separator anywhere. */
   sv_test_parse_error(t, "fun f(x) = x + 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 = 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "do 1", (int)PARSER_ERROR_EOF);

   /* The list tail keeps its guards, and `..` stays illegal in a tuple and in a
    * capture list, since only lists opt into it. */
   sv_test_parse_error(t, "[..xs] = l", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[.., t] = l", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[..t, 1] = l", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[h, ..1] = l", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [a, ..1] do a end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | -x do 1 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | -\"s\" do 1 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [a, ..\"s\"] do a end",
                       (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "(a, ..b) = t", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f[a, ..b](x) x", (int)PARSER_ERROR_UNEXPECTED_TOKEN);

   /* Clauses already match the parameters, so destructuring them first is not
    * allowed; a field with neither a colon nor a comma is still unclosed. */
   sv_test_parse_error(t, "fun f((a, b)) | x do x end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "p = {x y}", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(x) | end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f() | 0 do 1 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(a, b) | 1 do 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(a, b) | (1, 2, 3) do x end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(x + 1) | 0 do 1 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(x) | 0 do 1", (int)PARSER_ERROR_EOF);

   /* A guarded clause separates its body with `do`, so `=` is rejected: that is
    * what stops `when z = 1` from silently reading as a guard plus no body. */
   sv_test_parse_error(t, "match x | 1 when g = 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 when 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 when g 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 when g end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 when g", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "match x | 1 when g do", (int)PARSER_ERROR_EOF);

   /* A clause holds one pattern, so a second `|` before the body is not an
    * alternation; the `|` that separates clauses comes after a body. */
   sv_test_parse_error(t, "match x | 1 | 2 do 3 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(x) | 0 | 1 do 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | (1, a) | (2, b) do a end",
                       (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 | do 2 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | 1 | 2 3 end", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(a, b) | (1, y) | 2 do y end",
                       (int)PARSER_ERROR_UNEXPECTED_TOKEN);

   /* `when` is a keyword now, so it is no longer usable as a name. */
   sv_test_parse_error(t, "when = 1", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(when) 1", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "world(1, 2", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "x[0", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "while", (int)PARSER_ERROR_NOT_IMPLEMENTED);
   sv_test_parse_error(t, "@", (int)SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_parse_error(t, "1 + @", (int)SCANNER_ERROR_UNKNOWN_TOKEN);
   sv_test_parse_error(t, "\"abc", (int)SCANNER_ERROR_UNCLOSED_STRING);
   sv_test_parse_error(t, "\xFF", (int)SCANNER_ERROR_INVALID_UTF8);
   sv_test_parse_error(t, "x\xC3", (int)SCANNER_ERROR_INVALID_UTF8);

   ctx_t ctx = sv_test_parse_ctx();
   scanner_t s = scanner_init(sv_str_init("if x do\ny end extra"));
   sexpr_t e = parser_expr(&s, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sexpr_free(&e, &ctx.alloc);

   scanner_t msg_s = scanner_init(sv_str_init("(1 + 2"));
   sexpr_t msg_e = parser_expr(&msg_s, &ctx);
   sv_test_run(t, msg_e.tag == S_ATOM && msg_e.atom.kind == TOKEN_ERROR);
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "')'"));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 1"));
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);

   scanner_t line_s = scanner_init(sv_str_init("1 +\n@"));
   sexpr_t line_e = parser_expr(&line_s, &ctx);
   sv_test_run(t, line_e.tag == S_ATOM && line_e.atom.kind == TOKEN_ERROR);
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 2"));
   sv_str_deinit(&ctx.err.msg, &ctx.alloc);
}

static inline void sv_test_parser_oom(sv_testing_t* t)
{
   ctx_t fail_ctx = {
      .alloc = sv_test_fail_alloc,
      .logger = sv_std_logger,
      .err = { 0 },
   };
   scanner_t s = scanner_init(sv_str_init("world(1, 2, 3)"));
   sexpr_t e = parser_expr(&s, &fail_ctx);
   sv_test_run(t, e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR);
   sv_test_run(t, fail_ctx.err.error_code == (int)PARSER_ERROR_OOM);

   sv_test_countdown_t counter = { .remaining = 3 };
   sv_allocator_t countdown = {
      .vtable = &sv_test_countdown_vtable,
      .self = &counter,
   };
   ctx_t cd_ctx = {
      .alloc = countdown,
      .logger = sv_std_logger,
      .err = { 0 },
   };
   scanner_t s2 = scanner_init(sv_str_init("f(1 + 2, g(3), [4, 5], x.y |> h())"));
   sexpr_t e2 = parser_expr(&s2, &cd_ctx);
   sv_test_run(t, e2.tag == S_ATOM && e2.atom.kind == TOKEN_ERROR);
   sv_test_run(t, cd_ctx.err.error_code != 0);
   sv_str_deinit(&cd_ctx.err.msg, &cd_ctx.alloc);

   /* Sweep a clause list with a guard, a list tail and an open record, so every
    * allocation on that path is exercised under failure. */
   int64_t errored = 0;
   int64_t completed = 0;
   for (int64_t budget = 0; budget < 80; budget++) {
      sv_test_countdown_t c = { .remaining = budget };
      sv_allocator_t cd = { .vtable = &sv_test_countdown_vtable, .self = &c };
      ctx_t ctx = { .alloc = cd, .logger = sv_std_logger, .err = { 0 } };

      scanner_t sc = scanner_init(sv_str_init(
         "match x | (1, a) when a > 0 do a + 1 | [h, ..t] do h | {k: v, ..} do v"
         " | _ do 0 end"));
      sexpr_t e3 = parser_expr(&sc, &ctx);
      if (e3.tag == S_ATOM && e3.atom.kind == TOKEN_ERROR)
         errored++;
      else
         completed++;

      c.remaining = 1000000;
      if (ctx.err.msg.size > 0)
         sv_str_deinit(&ctx.err.msg, &ctx.alloc);
      sexpr_free(&e3, &ctx.alloc);
   }
   sv_test_run(t, errored > 0);
   sv_test_run(t, completed > 0);
}

static inline void sv_test_parser(sv_testing_t* t)
{
   sv_test_parser_exprs(t);
   sv_test_parser_program_fn(t);
   sv_test_parser_maps(t);
   sv_test_parser_match(t);
   sv_test_parser_errors(t);
   sv_test_parser_oom(t);
}

#endif
