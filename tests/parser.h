#ifndef SV_TESTS_PARSER_H
#define SV_TESTS_PARSER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/parser.h"
#include "../src/std/arena.h"
#include "string.h"

static inline ctx_t sv_test_parse_ctx(void)
{
   return (ctx_t){
      .alloc = sv_gpa,
      .logger = sv_std_logger,
      .err = { 0 },
   };
}

/* False when the parser failed, with the error in ctx->err. */
static inline bool sv_test_parse(const char* src, ctx_t* ctx, sv_arena_t* arena, sexpr_t* out, bool program)
{
   jmp_buf on_error;
   ctx->on_error = &on_error;
   arena->error_buffer = &on_error;
   switch (setjmp(on_error)) {
      case 1:
         error_set_oom(&ctx->err, (int)PARSER_ERROR_OOM, 0, &ctx->alloc);
         /* fallthrough */
      case 2:
         ctx->on_error = NULL;
         arena->error_buffer = NULL;
         return false;
   }

   sv_allocator_t a = sv_arena_allocator_init(arena);
   scanner_t s = scanner_init(sv_str_init(src));
   *out = program ? parser_program(&s, ctx, &a) : parser_expr(&s, ctx, &a);
   ctx->on_error = NULL;
   arena->error_buffer = NULL;
   return true;
}

static inline void sv_test_parse_error(sv_testing_t* t, const char* src, int expected_code)
{
   ctx_t ctx = sv_test_parse_ctx();
   sv_arena_t arena = sv_arena_init(1 << 12, &sv_gpa, NULL);
   sexpr_t e;
   bool parsed = sv_test_parse(src, &ctx, &arena, &e, false);

   sv_test_run_msg(t, !parsed, "expected parse error for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.error_code == expected_code, "error code for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.msg.size > 0, "error msg for \"%s\"", src);
   error_free(&ctx.err, &ctx.alloc);
   sv_arena_deinit(&arena);
}

static inline void sv_test_parse_ok(sv_testing_t* t, const char* src)
{
   ctx_t ctx = sv_test_parse_ctx();
   sv_arena_t arena = sv_arena_init(1 << 12, &sv_gpa, NULL);
   sexpr_t e;
   bool parsed = sv_test_parse(src, &ctx, &arena, &e, true);
   sv_test_run_msg(t, parsed && ctx.err.error_code == 0, "expected \"%s\" to parse", src);
   if (ctx.err.msg.size > 0)
      error_free(&ctx.err, &ctx.alloc);
   sv_arena_deinit(&arena);
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
   sv_arena_t arena = sv_arena_init(1 << 12, &sv_gpa, NULL);
   sexpr_t e;

   sv_test_run(t, sv_test_parse("1 + 1\n2 * 2", &ctx, &arena, &e, true));
   sv_test_run(t, ctx.err.error_code == 0);

   sv_test_run(t, sv_test_parse("", &ctx, &arena, &e, true));
   sv_test_run(t, ctx.err.error_code == 0);

   sv_test_run(t, !sv_test_parse("1 + 1\n)", &ctx, &arena, &e, true));
   sv_test_run(t, ctx.err.error_code == (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   error_free(&ctx.err, &ctx.alloc);

   ctx.err = (error_t){ 0 };
   sv_test_run(t, !sv_test_parse("x, y = 1, 2", &ctx, &arena, &e, true));
   sv_test_run(t, ctx.err.error_code == (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   error_free(&ctx.err, &ctx.alloc);

   ctx.err = (error_t){ 0 };
   sv_test_run(t, sv_test_parse("x = 1; y = 2;", &ctx, &arena, &e, true));
   sv_test_run(t, ctx.err.error_code == 0);
   sv_arena_deinit(&arena);
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

   /* Atoms are literals in every position, patterns included. */
   sv_test_parse_ok(t, ":ok");
   sv_test_parse_ok(t, "{s: :ok}");
   sv_test_parse_ok(t, "%{:k: 1, \"s\": :v}");
   sv_test_parse_ok(t, "m[:k]");
   sv_test_parse_ok(t, "match x | :ok do 1 | (:error, e) do e end");
   sv_test_parse_ok(t, "match x | %{:k: v} do v | {s: :ok} do 1 end");
   sv_test_parse_ok(t, "fun f(x) | :a do 1 | _ do 2 end");
   sv_test_parse_error(t, "match x | -:a do 1 end", PARSER_ERROR_UNEXPECTED_TOKEN);
}

static inline void sv_test_parser_alias(sv_testing_t* t)
{
   /* `pattern = name` binds the whole value, in every pattern position. */
   sv_test_parse_ok(t, "fun f({x, y, ..} = r, o) x");
   sv_test_parse_ok(t, "fun f(r = {x, ..}) x");
   sv_test_parse_ok(t, "fun f(a = b = c) a");
   sv_test_parse_ok(t, "match x | {a, ..} = r do r end");
   sv_test_parse_ok(t, "match x | r = [h, ..t] do r end");
   sv_test_parse_ok(t, "match x | (1, a = b) when a > 0 do a end");
   sv_test_parse_ok(t, "fun f(x) | r = {a, ..} do r | _ do 0 end");
   sv_test_parse_ok(t, "fun f(a, b) | (x, y) = t do t end");
   sv_test_parse_ok(t, "for (a, b) = p in xs do a end");
   sv_test_parse_ok(t, "({a, ..} = r) = e");

   /* One side must be a name; a list tail cannot be aliased; call arguments stay above `=`. */
   sv_test_parse_error(t, "match x | 1 = 2 do 3 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | (a, b) = [c] do 3 end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "match x | [h, ..t = w] do h end", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[h, ..t = w] = l", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "f(x = 1)", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(a, b) | (x, y) = t = u do t end", PARSER_ERROR_UNEXPECTED_TOKEN);
}

static inline void sv_test_parser_spread(sv_testing_t* t)
{
   /* A trailing `..base` in a literal is a spread, and it must come last. */
   sv_test_parse_ok(t, "{y: 1, ..o}");
   sv_test_parse_ok(t, "{..o}");
   sv_test_parse_ok(t, "%{\"k\": 1, ..m}");
   sv_test_parse_ok(t, "[1, ..f()]");
   sv_test_parse_ok(t, "[1, ..m::xs]");
   sv_test_parse_error(t, "{y: 1, ..o, z: 2}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "%{\"a\": 1, ..m, \"b\": 2}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "{..o, x: 1}", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[1, ..xs, 2]", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "[..xs]", PARSER_ERROR_UNEXPECTED_TOKEN);
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
   sv_arena_t arena = sv_arena_init(1 << 12, &sv_gpa, NULL);
   sexpr_t e;
   sv_test_run(t, sv_test_parse("if x do\ny end extra", &ctx, &arena, &e, false));
   sv_test_run(t, ctx.err.error_code == 0);

   sv_test_run(t, !sv_test_parse("(1 + 2", &ctx, &arena, &e, false));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "')'"));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 1"));
   error_free(&ctx.err, &ctx.alloc);

   sv_test_run(t, !sv_test_parse("1 +\n@", &ctx, &arena, &e, false));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 2"));
   error_free(&ctx.err, &ctx.alloc);
   sv_arena_deinit(&arena);
}

static inline void sv_test_parser_oom(sv_testing_t* t)
{
   /* An arena that cannot get a block fails the parse as PARSER_ERROR_OOM. */
   ctx_t fail_ctx = { .alloc = sv_test_fail_alloc, .logger = sv_std_logger, .err = { 0 } };
   sv_arena_t fail_arena = sv_arena_init(64, &sv_test_fail_alloc, NULL);
   sexpr_t e;
   sv_test_run(t, !sv_test_parse("world(1, 2, 3)", &fail_ctx, &fail_arena, &e, false));
   sv_test_run(t, fail_ctx.err.error_code == (int)PARSER_ERROR_OOM);
   sv_arena_deinit(&fail_arena);

   sv_test_countdown_t counter = { .remaining = 3 };
   sv_allocator_t countdown = { .vtable = &sv_test_countdown_vtable, .self = &counter };
   ctx_t cd_ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = { 0 } };
   sv_arena_t cd_arena = sv_arena_init(64, &countdown, NULL);
   sv_test_run(t, !sv_test_parse("f(1 + 2, g(3), [4, 5], x.y |> h())", &cd_ctx, &cd_arena, &e, false));
   sv_test_run(t, cd_ctx.err.error_code == (int)PARSER_ERROR_OOM);
   error_free(&cd_ctx.err, &cd_ctx.alloc);
   sv_arena_deinit(&cd_arena);

   /* Small blocks interrupt the parser at every block boundary. */
   int64_t errored = 0;
   int64_t completed = 0;
   for (int64_t budget = 0; budget < 120; budget++) {
      sv_test_countdown_t c = { .remaining = budget };
      sv_allocator_t cd = { .vtable = &sv_test_countdown_vtable, .self = &c };
      ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = { 0 } };
      sv_arena_t arena = sv_arena_init(256, &cd, NULL);

      sexpr_t e3;
      bool parsed = sv_test_parse(
         "match x | (1, a) when a > 0 do a + 1 | [h, ..t] do h | {k: v, ..} do v"
         " | {k: 1, ..} = r do {k: 2, ..r} | _ do 0 end", &ctx, &arena, &e3, false);
      if (parsed)
         completed++;
      else
         errored++;

      if (ctx.err.msg.size > 0)
         error_free(&ctx.err, &ctx.alloc);
      sv_arena_deinit(&arena);
   }
   sv_test_run(t, errored > 0);
   sv_test_run(t, completed > 0);
}

static inline void sv_test_parser_comments(sv_testing_t* t)
{
   sv_test_parse_ok(t, "# a full line comment");
   sv_test_parse_ok(t, "x = 1 # trailing");
   sv_test_parse_ok(t, "x = 1 #no space\ny = 2");
   sv_test_parse_ok(t, "# a\n# b\nx = 1");
   sv_test_parse_ok(t, "x = 1\n#");
   sv_test_parse_ok(t, "if x do # open\n  y # body\nend # close");
   sv_test_parse_ok(t, "x = \"a#b\"");
   sv_test_parse_error(t, "# nothing but a comment", (int)PARSER_ERROR_EOF);

   /* Comment lines still count, so the error lands on line 4. */
   ctx_t ctx = sv_test_parse_ctx();
   sv_arena_t arena = sv_arena_init(1 << 12, &sv_gpa, NULL);
   sexpr_t e;
   sv_test_run(t, !sv_test_parse("# one\n# two\n1 +\n@", &ctx, &arena, &e, false));
   sv_test_run(t, sv_str_cstr_in(ctx.err.msg, "line 4"));
   error_free(&ctx.err, &ctx.alloc);
   sv_arena_deinit(&arena);
}

static inline void sv_test_parser_newlines(sv_testing_t* t)
{
   /* A newline ends a complete expression. */
   sv_test_parse_ok(t, "x = 1\n(a, b) = (1, 2)");
   sv_test_parse_ok(t, "x = 5\n-1");
   sv_test_parse_ok(t, "xs = [1]\n[0]");
   sv_test_parse_ok(t, "x = 1\nx = 2");

   /* An incomplete expression continues. */
   sv_test_parse_ok(t, "x = 1 +\n2");
   sv_test_parse_ok(t, "x =\n1");
   sv_test_parse_ok(t, "x = (1 +\n2) * 3");
   sv_test_parse_ok(t, "f(\n1,\n2\n)");
   sv_test_parse_ok(t, "[\n1,\n2\n]");
   sv_test_parse_ok(t, "[1,\n2]");
   sv_test_parse_ok(t, "{x: 1,\n y: 2}");
   sv_test_parse_ok(t, "{x:\n1}");
   sv_test_parse_ok(t, "%{1: 2,\n 3: 4}");
   sv_test_parse_ok(t, "%{1: 2,\n..m}");
   sv_test_parse_ok(t, "{x,\n..\n}");
   sv_test_parse_ok(t, "fun f(x)\n  x");
   sv_test_parse_ok(t, "fun f(\n  a,\n  b\n) a");
   sv_test_parse_ok(t, "if x do\n1\nelse\n2\nend");
   sv_test_parse_ok(t, "if x\ndo 1 end");
   sv_test_parse_ok(t, "for x in\n[1] do\nx\nend");
   sv_test_parse_ok(t, "match x\n| 1 do 2\n| _ do 3\nend");
   sv_test_parse_ok(t, "match x\n| (1, a)\nwhen a > 0 do a\n| _ do 3\nend");
   sv_test_parse_ok(t, "fun\n| a(x) x\n| b(x)\n  x\nend");
   sv_test_parse_ok(t, "x\n|> f()\n|> g()");
   sv_test_parse_ok(t, "x # c\n|> f()");
   sv_test_parse_ok(t, "(a,\n b) = t");
   sv_test_parse_ok(t, "%{1:\nv} = m");

   /* Elements need a comma; a same-line juxtaposition is still an error. */
   sv_test_parse_error(t, "[1\n2]", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "(1 +\n2) 3", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "1 2", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
}

static inline void sv_test_parser(sv_testing_t* t)
{
   sv_test_parser_exprs(t);
   sv_test_parser_program_fn(t);
   sv_test_parser_maps(t);
   sv_test_parser_match(t);
   sv_test_parser_alias(t);
   sv_test_parser_spread(t);
   sv_test_parser_comments(t);
   sv_test_parser_newlines(t);
   sv_test_parser_errors(t);
   sv_test_parser_oom(t);
}

#endif
