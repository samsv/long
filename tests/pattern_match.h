#ifndef SV_TESTS_PATTERN_MATCH_H
#define SV_TESTS_PATTERN_MATCH_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/compiler.h"
#include "../src/parser.h"
#include "../src/pattern_match.h"
#include "string.h"

#define T1 "$000000000001"
#define T2 "$000000000002"
#define T3 "$000000000003"
#define T4 "$000000000004"
#define T5 "$000000000005"
#define T6 "$000000000006"

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

static inline void sv_test_match_err(sv_testing_t* t, const char* src, int expected_code)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = { 0 } };
   scanner_t s = scanner_init(sv_str_init(src));
   sexpr_t e = parser_expr(&s, &ctx);
   sv_test_run_msg(t, ctx.err.error_code == 0, "parse error for \"%s\"", src);

   e = match_compile(e, &ctx);
   sv_test_run_msg(t, e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR,
                   "expected lowering error for \"%s\"", src);
   sv_test_run_msg(t, ctx.err.error_code == expected_code, "error code for \"%s\"", src);

   if (ctx.err.msg.size > 0)
      sv_str_deinit(&ctx.err.msg, &ctx.alloc);
   sexpr_free(&e, &ctx.alloc);
}

static inline void sv_test_match_literals(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x end", "(do (= " T1 " x) nil)");
   sv_test_match_ok(t, "match x | 1 = 2 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ")"
      " (if (== " T1 " 1) 2 fail) fail) nil))");
   sv_test_match_ok(t, "match x | 1 = 2 | 0 = 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ")"
      " (if (== " T1 " 1) 2 (if (== " T1 " 0) 3 fail)) fail) nil))");
   sv_test_match_ok(t, "match x | nil = 1 end",
      "(do (= " T1 " x) (| (if (is-nil? " T1 ") 1 fail) nil))");
   sv_test_match_ok(t, "match x | true = 1 | false = 2 end",
      "(do (= " T1 " x) (| (if (is-bool? " T1 ")"
      " (if (== " T1 " true) 1 (if (== " T1 " false) 2 fail)) fail) nil))");
   sv_test_match_ok(t, "match x | 1 = 2 | \"a\" = 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1 " 1) 2 fail)"
      " (if (is-str? " T1 ") (if (== " T1 " \"a\") 3 fail) fail)) nil))");
}

static inline void sv_test_match_defaults(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | _ = 1 end", "(do (= " T1 " x) (| 1 nil))");
   sv_test_match_ok(t, "match x | y = y end",
      "(do (= " T1 " x) (| (do (= y " T1 ") y) nil))");
   sv_test_match_ok(t, "match x | 1 = 2 | _ = 3 end",
      "(do (= " T1 " x) (| (| (if (is-number? " T1 ")"
      " (if (== " T1 " 1) 2 fail) fail) 3) nil))");
}

static inline void sv_test_match_tuples(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | (1,) = 3 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 1)"
      " (do (= " T2 " ([ " T1 " 0))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) 3 fail) fail)) fail) nil))");
   sv_test_match_ok(t, "match x | (1, 2) = 3 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2)"
      " (do (= " T2 " ([ " T1 " 0)) (do (= " T3 " ([ " T1 " 1))"
      " (if (is-number? " T2 ") (if (== " T2 " 1)"
      " (if (is-number? " T3 ") (if (== " T3 " 2) 3 fail) fail) fail) fail))) fail) nil))");

   /* Rows sharing a prefix must both stay reachable. */
   sv_test_match_ok(t, "match x | (1, 2) = a | (1, 3) = b end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2)"
      " (do (= " T2 " ([ " T1 " 0)) (do (= " T3 " ([ " T1 " 1))"
      " (if (is-number? " T2 ") (if (== " T2 " 1)"
      " (if (is-number? " T3 ") (if (== " T3 " 2) a (if (== " T3 " 3) b fail)) fail)"
      " fail) fail))) fail) nil))");

   sv_test_match_ok(t, "match x | ((1, 2), 3) = z end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2)"
      " (do (= " T2 " ([ " T1 " 0)) (do (= " T3 " ([ " T1 " 1))"
      " (if (is-tuple? " T2 " 2)"
      " (do (= " T4 " ([ " T2 " 0)) (do (= " T5 " ([ " T2 " 1))"
      " (if (is-number? " T4 ") (if (== " T4 " 1)"
      " (if (is-number? " T5 ") (if (== " T5 " 2)"
      " (if (is-number? " T3 ") (if (== " T3 " 3) z fail) fail) fail) fail)"
      " fail) fail))) fail))) fail) nil))");
}

static inline void sv_test_match_scoring(sv_testing_t* t)
{
   /* Column 0 scores 0 (a variable in row 0) and column 1 scores 2, so the
    * second position is tested first. */
   sv_test_match_ok(t, "match x | (a, 1) = p | (b, 2) = q end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2)"
      " (do (= " T2 " ([ " T1 " 0)) (do (= " T3 " ([ " T1 " 1))"
      " (if (is-number? " T3 ")"
      " (if (== " T3 " 1) (do (= a " T2 ") p)"
      " (if (== " T3 " 2) (do (= b " T2 ") q) fail)) fail))) fail) nil))");
}

static inline void sv_test_match_records(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | {a: 1} = 2 end",
      "(do (= " T1 " x) (| (if (is-record? " T1 " 1)"
      " (if (has-field? " T1 " a) (do (= " T2 " (. " T1 " a))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) 2 fail) fail)) fail) fail) nil))");

   sv_test_match_ok(t, "match x | {a: 1, ..} = 3 end",
      "(do (= " T1 " x) (| (if (is-record? " T1 ")"
      " (if (has-field? " T1 " a) (do (= " T2 " (. " T1 " a))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) 3 fail) fail)) fail) fail) nil))");

   /* Exact and open groups overlap, so they chain through `|`. */
   sv_test_match_ok(t, "match x | {a: 1} = 2 | {a: 1, ..} = 3 end",
      "(do (= " T1 " x) (| (| (if (is-record? " T1 " 1)"
      " (if (has-field? " T1 " a) (do (= " T3 " (. " T1 " a))"
      " (if (is-number? " T3 ") (if (== " T3 " 1) 2 fail) fail)) fail) fail)"
      " (if (is-record? " T1 ")"
      " (if (has-field? " T1 " a) (do (= " T2 " (. " T1 " a))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) 3 fail) fail)) fail) fail)) nil))");

   sv_test_match_ok(t, "match x | {a: 1, b: v} = v end",
      "(do (= " T1 " x) (| (if (is-record? " T1 " 2)"
      " (if (has-field? " T1 " a) (if (has-field? " T1 " b)"
      " (do (= " T2 " (. " T1 " a)) (do (= " T3 " (. " T1 " b))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) (do (= v " T3 ") v) fail) fail)))"
      " fail) fail) fail) nil))");

   sv_test_match_ok(t, "match x | %{\"k\": 1} = 2 end",
      "(do (= " T1 " x) (| (if (is-hashmap? " T1 " 1)"
      " (if (has-key? " T1 " \"k\") (do (= " T2 " ([ " T1 " \"k\"))"
      " (if (is-number? " T2 ") (if (== " T2 " 1) 2 fail) fail)) fail) fail) nil))");

   sv_test_match_err(t, "match x | {a: 1, a: 2} = 1 end", C_ERR_REDEFINED);
}

static inline void sv_test_match_lists(sv_testing_t* t)
{
   /* Lists still compare by value until the CONS lowering lands. */
   sv_test_match_ok(t, "match x | [1, 2] = 3 end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ")"
      " (if (== " T1 " (list 1 2)) 3 fail) fail) nil))");
}

static inline void sv_test_match_oom(sv_testing_t* t)
{
   const char* src =
      "match x | 1 = 2 | \"a\" = 3 | (4, 5) = 6 | {k: 7, j: 8} = 9 | (a, 1) = a | y = y end";
   int64_t errored = 0;
   int64_t completed = 0;

   for (int64_t budget = 0; budget < 200; budget++) {
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

      counter.remaining = 1000000;
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
   sv_test_match_defaults(t);
   sv_test_match_tuples(t);
   sv_test_match_scoring(t);
   sv_test_match_records(t);
   sv_test_match_lists(t);
   sv_test_match_oom(t);
}

#endif
