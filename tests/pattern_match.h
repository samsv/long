#ifndef SV_TESTS_PATTERN_MATCH_H
#define SV_TESTS_PATTERN_MATCH_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/parser.h"
#include "../src/pattern_match.h"
#include "string.h"

static inline void sv_test_match_ok(sv_testing_t* t, const char* src, const char* expected)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = { 0 } };
   scanner_t s = scanner_init(sv_str_init(src));
   sexpr_t e = parser_expr(&s, &ctx);
   sv_test_run_msg(t, ctx.err.error_code == 0, "parse error for \"%s\"", src);

   e = match_compile(e, &ctx);
   sv_test_run_msg(t, !(e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR),
                   "lowering error for \"%s\"", src);

   sv_str_t formatted = sexpr_format(e, &ctx.alloc);
   sv_test_run_msg(t, sv_str_comp(formatted, sv_str_init(expected)),
                   "\"%s\" should lower to %s, got %.*s",
                   src, expected, (int)formatted.size, formatted.chars);

   sv_str_deinit(&formatted, &ctx.alloc);
   sexpr_free(&e, &ctx.alloc);
}

static inline void sv_test_match_literals(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x end", "(do (= $1 x) nil)");
   sv_test_match_ok(t, "match x | 1 = 2 end",
                    "(do (= $1 x) (if (is-number? $1) (if (== $1 1) 2 fail) nil))");
   sv_test_match_ok(t, "match x | 1 = 2 | 0 = 3 end",
                    "(do (= $1 x) (if (is-number? $1)"
                    " (if (== $1 1) 2 (if (== $1 0) 3 fail)) nil))");
   sv_test_match_ok(t, "match f(1) | \"a\" = 3 end",
                    "(do (= $1 (f 1)) (if (is-str? $1) (if (== $1 \"a\") 3 fail) nil))");
   sv_test_match_ok(t, "match x | nil = 1 end",
                    "(do (= $1 x) (if (is-nil? $1) (if (== $1 nil) 1 fail) nil))");
   sv_test_match_ok(t, "match x | true = 1 | false = 2 end",
                    "(do (= $1 x) (if (is-bool? $1)"
                    " (if (== $1 true) 1 (if (== $1 false) 2 fail)) nil))");
}

static inline void sv_test_match_groups(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | 1 = 2 | \"a\" = 3 end",
                    "(do (= $1 x) (if (is-str? $1) (if (== $1 \"a\") 3 fail)"
                    " (if (is-number? $1) (if (== $1 1) 2 fail) nil)))");
   sv_test_match_ok(t, "match x | 1 = a | \"s\" = b | 2 = c end",
                    "(do (= $1 x) (if (is-str? $1) (if (== $1 \"s\") b fail)"
                    " (if (is-number? $1) (if (== $1 1) a (if (== $1 2) c fail)) nil)))");
   sv_test_match_ok(t, "match x | [1, 2] = 3 end",
                    "(do (= $1 x) (if (is-list? $1) (if (== $1 (list 1 2)) 3 fail) nil))");
   sv_test_match_ok(t, "match x | {a: 1} = 2 end",
                    "(do (= $1 x) (if (is-record? $1) (if (== $1 (record a 1)) 2 fail) nil))");
   sv_test_match_ok(t, "match x | (1, 2) = 3 end",
                    "(do (= $1 x) (if (is-tuple? $1) (if (== $1 (tuple 1 2)) 3 fail) nil))");
   sv_test_match_ok(t, "match x | %{\"k\": 1} = 2 end",
                    "(do (= $1 x) (if (is-hashmap? $1)"
                    " (if (== $1 (hashmap \"k\" 1)) 2 fail) nil))");
}

static inline void sv_test_match_defaults(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | _ = 1 end", "(do (= $1 x) 1)");
   sv_test_match_ok(t, "match x | y = y end", "(do (= $1 x) (do (= y $1) y))");
   sv_test_match_ok(t, "match x | 1 = 2 | _ = 3 end",
                    "(do (= $1 x) (if (is-number? $1) (if (== $1 1) 2 fail) 3))");
   sv_test_match_ok(t, "match x | 1 = 2 | y = y end",
                    "(do (= $1 x) (if (is-number? $1) (if (== $1 1) 2 fail) (do (= y $1) y)))");
   sv_test_match_ok(t, "match x | _ = 1 | 2 = 3 end", "(do (= $1 x) 1)");
}

static inline void sv_test_match_full(sv_testing_t* t)
{
   sv_test_match_ok(t,
      "match (a, b)\n"
      "| (false, y) = y\n"
      "| [x, y, ..xs] = f(x, xs)\n"
      "| 1 = true\n"
      "| 0 = true\n"
      "| \"hello\" = true\n"
      "| {x: a, y: b} = true\n"
      "| {x: a, y: b, ..} = true\n"
      "| _ = true\n"
      "end",
      "(do (= $1 (tuple a b))"
      " (if (is-str? $1) (if (== $1 \"hello\") true fail)"
      " (if (is-number? $1) (if (== $1 1) true (if (== $1 0) true fail))"
      " (if (is-list? $1) (if (== $1 (list x y (.. xs))) (f x xs) fail)"
      " (if (is-record? $1) (if (== $1 (record x a y b))"
      " true (if (== $1 (record x a y b ..)) true fail))"
      " (if (is-tuple? $1) (if (== $1 (tuple false y)) y fail) true))))))");
}

static inline void sv_test_match_oom(sv_testing_t* t)
{
   const char* src = "match x | 1 = 2 | \"a\" = 3 | (4, 5) = 6 | y = y end";
   int64_t errored = 0;
   int64_t completed = 0;

   for (int64_t budget = 0; budget < 30; budget++) {
      sv_test_countdown_t counter = { .remaining = 1000000 };
      sv_allocator_t countdown = { .vtable = &sv_test_countdown_vtable, .self = &counter };
      ctx_t ctx = { .alloc = countdown, .logger = sv_std_logger, .err = { 0 } };

      scanner_t s = scanner_init(sv_str_init(src));
      sexpr_t e = parser_expr(&s, &ctx);
      sv_test_run_msg(t, ctx.err.error_code == 0, "%s", "oom sweep should parse");

      counter.remaining = budget;
      e = match_compile(e, &ctx);
      if (e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR)
         errored++;
      else
         completed++;

      if (ctx.err.msg.size > 0)
         sv_str_deinit(&ctx.err.msg, &ctx.alloc);
      sexpr_free(&e, &ctx.alloc);
   }

   sv_test_run(t, errored > 0);
   sv_test_run(t, completed > 0);
}

static inline void sv_test_pattern_match(sv_testing_t* t)
{
   sv_test_match_literals(t);
   sv_test_match_groups(t);
   sv_test_match_defaults(t);
   sv_test_match_full(t);
   sv_test_match_oom(t);
}

#endif
