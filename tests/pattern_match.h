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
#define T7 "$000000000007"
#define T8 "$000000000008"

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
   sv_test_match_ok(t, "match x end",
      "(do (= " T1 " x) (match-fail " T1 "))");
   sv_test_match_ok(t, "match x | 1 do 2 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " 1) (do 2) $fail) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | 1 do 2 | 0 do 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1 " 1) (do 2) (if (== " T1
      " 0) (do 3) $fail)) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | nil do 1 end",
      "(do (= " T1 " x) (| (if (is-nil? " T1 ") (do 1) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | true do 1 | false do 2 end",
      "(do (= " T1 " x) (| (if (is-bool? " T1 ") (if (== " T1 " true) (do 1) (if (== " T1
      " false) (do 2) $fail)) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | 1 do 2 | \"a\" do 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " 1) (do 2) $fail) (if (is-str? " T1 ") (if (== " T1
      " \"a\") (do 3) $fail) $fail)) (match-fail " T1 ")))");
}

static inline void sv_test_match_defaults(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | _ do 1 end",
      "(do (= " T1 " x) (| (do 1) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | y do y end",
      "(do (= " T1 " x) (| (do (= y " T1 ") (do y)) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | 1 do 2 | _ do 3 end",
      "(do (= " T1 " x) (| (| (if (is-number? " T1 ") (if (== " T1
      " 1) (do 2) $fail) $fail) (do 3)) (match-fail " T1 ")))");
}

static inline void sv_test_match_tuples(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | (1,) do 3 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 1) (do (= " T2 " ([ " T1
      " 0)) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 3) $fail) $fail)) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | (1, 2) do 3 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (if (is-number? " T2 ") (if (== " T2 " 1) (if (is-number? " T3
      ") (if (== " T3 " 2) (do 3) $fail) $fail) $fail) $fail))) $fail) (match-fail " T1
      ")))");

   /* Rows sharing a prefix must both stay reachable. */
   sv_test_match_ok(t, "match x | (1, 2) do a | (1, 3) do b end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (if (is-number? " T2 ") (if (== " T2 " 1) (if (is-number? " T3
      ") (if (== " T3 " 2) (do a) (if (== " T3
      " 3) (do b) $fail)) $fail) $fail) $fail))) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | ((1, 2), 3) do z end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (if (is-tuple? " T2 " 2) (do (= " T4 " ([ " T2 " 0)) (do (= " T5
      " ([ " T2 " 1)) (if (is-number? " T4 ") (if (== " T4 " 1) (if (is-number? " T5
      ") (if (== " T5 " 2) (if (is-number? " T3 ") (if (== " T3
      " 3) (do z) $fail) $fail) $fail) $fail) $fail) $fail))) $fail))) $fail) (match-fail "
      T1 ")))");
}

static inline void sv_test_match_scoring(sv_testing_t* t)
{
   /* Column 0 scores 0 (a variable in row 0) and column 1 scores 2, so the
    * second position is tested first. */
   sv_test_match_ok(t, "match x | (a, 1) do p | (b, 2) do q end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (if (is-number? " T3 ") (if (== " T3 " 1) (do (= a " T2
      ") (do p)) (if (== " T3 " 2) (do (= b " T2
      ") (do q)) $fail)) $fail))) $fail) (match-fail " T1 ")))");
}

static inline void sv_test_match_records(sv_testing_t* t)
{
   sv_test_match_ok(t, "match x | {a: 1} do 2 end",
      "(do (= " T1 " x) (| (if (is-record? " T1 " 1) (if (has-field? " T1 " a) (do (= "
      T2 " (. " T1 " a)) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 2) $fail) $fail)) $fail) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | {a: 1, ..} do 3 end",
      "(do (= " T1 " x) (| (if (is-record? " T1 ") (if (has-field? " T1 " a) (do (= " T2
      " (. " T1 " a)) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 3) $fail) $fail)) $fail) $fail) (match-fail " T1 ")))");

   /* Exact and open groups overlap, so they chain through `|`. */
   sv_test_match_ok(t, "match x | {a: 1} do 2 | {a: 1, ..} do 3 end",
      "(do (= " T1 " x) (| (| (if (is-record? " T1 " 1) (if (has-field? " T1
      " a) (do (= " T3 " (. " T1 " a)) (if (is-number? " T3 ") (if (== " T3
      " 1) (do 2) $fail) $fail)) $fail) $fail) (if (is-record? " T1 ") (if (has-field? "
      T1 " a) (do (= " T2 " (. " T1 " a)) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 3) $fail) $fail)) $fail) $fail)) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | {a: 1, b: v} do v end",
      "(do (= " T1 " x) (| (if (is-record? " T1 " 2) (if (has-field? " T1
      " a) (if (has-field? " T1 " b) (do (= " T2 " (. " T1 " a)) (do (= " T3 " (. " T1
      " b)) (if (is-number? " T2 ") (if (== " T2 " 1) (do (= v " T3
      ") (do v)) $fail) $fail))) $fail) $fail) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | %{\"k\": 1} do 2 end",
      "(do (= " T1 " x) (| (if (is-hashmap? " T1 " 1) (if (has-key? " T1
      " \"k\") (do (= " T2 " ([ " T1 " \"k\")) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 2) $fail) $fail)) $fail) $fail) (match-fail " T1 ")))");

   /* An open map tests only that it is a map, and only for the keys it names. */
   sv_test_match_ok(t, "match x | %{\"k\": v, ..} do v end",
      "(do (= " T1 " x) (| (if (is-hashmap? " T1 ") (if (has-key? " T1
      " \"k\") (do (= " T2 " ([ " T1 " \"k\")) (do (= v " T2
      ") (do v))) $fail) $fail) (match-fail " T1 ")))");

   /* Exact and open map groups overlap, so they chain through `|` like records. */
   sv_test_match_ok(t, "match x | %{\"k\": 1} do 2 | %{\"k\": 1, ..} do 3 end",
      "(do (= " T1 " x) (| (| (if (is-hashmap? " T1 " 1) (if (has-key? " T1
      " \"k\") (do (= " T3 " ([ " T1 " \"k\")) (if (is-number? " T3 ") (if (== " T3
      " 1) (do 2) $fail) $fail)) $fail) $fail) (if (is-hashmap? " T1 ") (if (has-key? " T1
      " \"k\") (do (= " T2 " ([ " T1 " \"k\")) (if (is-number? " T2 ") (if (== " T2
      " 1) (do 3) $fail) $fail)) $fail) $fail)) (match-fail " T1 ")))");

   /* A negative number is folded into the literal at parse time. */
   sv_test_match_ok(t, "match x | -1 do 2 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " -1) (do 2) $fail) $fail) (match-fail " T1 ")))");

   sv_test_match_err(t, "match x | {a: 1, a: 2} do 1 end", C_ERR_REDEFINED);
}

static inline void sv_test_match_lists(sv_testing_t* t)
{
   /* The empty list is the else arm: NIL and CONS are disjoint. */
   sv_test_match_ok(t, "match x | [] do 0 end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1
      ") $fail (do 0)) $fail) (match-fail " T1 ")))");

   /* One `list-uncons` binds both parts, and no `is-list?` is emitted for a tail
    * column, which is known to be a list already. */
   sv_test_match_ok(t, "match x | [a] do a end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (if (is-cons? " T3 ") $fail (do (= a " T2
      ") (do a)))) $fail) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | [h, ..t] do h end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (do (= t " T3 ") (do (= h " T2
      ") (do h)))) $fail) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | [1, 2] do 3 end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (if (is-number? " T2 ") (if (== " T2 " 1) (if (is-cons? " T3
      ") (list-uncons " T3 " " T4 " " T5 " (if (is-number? " T4 ") (if (== " T4
      " 2) (if (is-cons? " T5
      ") $fail (do 3)) $fail) $fail)) $fail) $fail) $fail)) $fail) $fail) (match-fail "
      T1 ")))");

   /* Rows sharing a prefix share the head test and the uncons, then branch. */
   sv_test_match_ok(t, "match x | [1, 2] do a | [1, 3] do b end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (if (is-number? " T2 ") (if (== " T2 " 1) (if (is-cons? " T3
      ") (list-uncons " T3 " " T4 " " T5 " (if (is-number? " T4 ") (if (== " T4
      " 2) (if (is-cons? " T5 ") $fail (do a)) (if (== " T4 " 3) (if (is-cons? " T5
      ") $fail (do b)) $fail)) $fail)) $fail) $fail) $fail)) $fail) $fail) (match-fail "
      T1 ")))");

   sv_test_match_ok(t, "match x | [] do 0 | [a, ..r] do a end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (do (= r " T3 ") (do (= a " T2
      ") (do a)))) (do 0)) $fail) (match-fail " T1 ")))");
   /* A pattern tail: `..[]` is the exact length case and needs no extra test
    * beyond the remainder being empty. */
   sv_test_match_ok(t, "match x | [a, ..[]] do a end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (if (is-cons? " T3 ") $fail (do (= a " T2
      ") (do a)))) $fail) $fail) (match-fail " T1 ")))");
   sv_test_match_ok(t, "match x | [a, ..[b]] do b end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T2 " " T3 " (if (is-cons? " T3 ") (list-uncons " T3 " " T4 " " T5
      " (if (is-cons? " T5 ") $fail (do (= a " T2 ") (do (= b " T4
      ") (do b))))) $fail)) $fail) $fail) (match-fail " T1 ")))");
}

static inline void sv_test_match_guards(sv_testing_t* t)
{
   /* Both rows carry the same literal, so they reach the empty rule together and
    * a failing guard must fall through to the next row rather than to the
    * match default. This is what the `|` chain in the empty rule buys. */
   sv_test_match_ok(t, "match x | 1 when g do 2 | 1 do 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " 1) (| (if g (do 2) $fail) (do 3)) $fail) $fail) (match-fail " T1 ")))");

   /* Different literals cannot both match, so the guard falls to the default. */
   sv_test_match_ok(t, "match x | 1 when g do 2 | 2 do 3 end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " 1) (if g (do 2) $fail) (if (== " T1 " 2) (do 3) $fail)) $fail) (match-fail " T1
      ")))");

   /* The binding wraps the guard, so a guard sees its own pattern variables. */
   sv_test_match_ok(t, "match x | y when y > 0 do 1 | _ do 2 end",
      "(do (= " T1 " x) (| (| (do (= y " T1
      ") (if (> y 0) (do 1) $fail)) (do 2)) (match-fail " T1 ")))");

   /* A guarded wildcard is no longer an unconditional catch all. */
   sv_test_match_ok(t, "match x | _ when g do 1 | _ do 2 end",
      "(do (= " T1 " x) (| (| (if g (do 1) $fail) (do 2)) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | (a, b) when a > b do a | _ do 0 end",
      "(do (= " T1 " x) (| (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1
      " 0)) (do (= " T3 " ([ " T1 " 1)) (do (= b " T3 ") (do (= a " T2
      ") (if (> a b) (do a) $fail))))) $fail) (do 0)) (match-fail " T1 ")))");

   /* The guard sits inside the uncons, whose head and tail are locals that
    * compile_fail has to pop on the way out. */
   sv_test_match_ok(t, "match x | [h, ..t] when h < 0 do h | _ do 0 end",
      "(do (= " T1 " x) (| (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons "
      T1 " " T2 " " T3 " (do (= t " T3 ") (do (= h " T2
      ") (if (< h 0) (do h) $fail)))) $fail) $fail) (do 0)) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | {a: v} when v do 1 | _ do 2 end",
      "(do (= " T1 " x) (| (| (if (is-record? " T1 " 1) (if (has-field? " T1
      " a) (do (= " T2 " (. " T1 " a)) (do (= v " T2
      ") (if v (do 1) $fail))) $fail) $fail) (do 2)) (match-fail " T1 ")))");

   /* A guard binds tighter than the clause's `=`, so it swallows `and`. */
   sv_test_match_ok(t, "match x | n when n > 0 and n < 10 do 1 end",
      "(do (= " T1 " x) (| (do (= n " T1
      ") (if (and (> n 0) (< n 10)) (do 1) $fail)) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | 1 when g do y = 3; y end",
      "(do (= " T1 " x) (| (if (is-number? " T1 ") (if (== " T1
      " 1) (if g (do (= y 3) y) $fail) $fail) $fail) (match-fail " T1 ")))");
}

static inline void sv_test_match_repeats(sv_testing_t* t)
{
   /* The base case: the repeat becomes a fresh temp plus one equality. */
   sv_test_match_ok(t, "match x | (a, a) do a end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T3 " ([ " T1 " 0)) (do (= "
      T4 " ([ " T1 " 1)) (do (= " T2 " " T4 ") (do (= a " T3 ") (if (== a " T2
      ") (do a) $fail))))) $fail) (match-fail " T1 ")))");

   /* A non-adjacent repeat. `b` sits between the two occurrences and
    * gains no constraint of its own. */
   sv_test_match_ok(t, "match x | (a, b, a) do b end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 3) (do (= " T3 " ([ " T1 " 0)) (do (= "
      T4 " ([ " T1 " 1)) (do (= " T5 " ([ " T1 " 2)) (do (= " T2 " " T5 ") (do (= b " T4
      ") (do (= a " T3 ") (if (== a " T2 ") (do b) $fail))))))) $fail) (match-fail " T1
      ")))");

   /* Three occurrences give two constraints, both against the first, so
    * `a == e2 and a == e3` implies `e2 == e3`. */
   sv_test_match_ok(t, "match x | (a, a, a) do a end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 3) (do (= " T4 " ([ " T1 " 0)) (do (= "
      T5 " ([ " T1 " 1)) (do (= " T6 " ([ " T1 " 2)) (do (= " T3 " " T6 ") (do (= " T2
      " " T5 ") (do (= a " T4 ") (if (and (== a " T2 ") (== a " T3
      ")) (do a) $fail))))))) $fail) (match-fail " T1 ")))");

   /* Two independent repeats, interleaved. */
   sv_test_match_ok(t, "match x | (a, b, b, a) do 1 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 4) (do (= " T4 " ([ " T1 " 0)) (do (= "
      T5 " ([ " T1 " 1)) (do (= " T6 " ([ " T1 " 2)) (do (= " T7 " ([ " T1 " 3)) (do (= "
      T3 " " T7 ") (do (= " T2 " " T6 ") (do (= b " T5 ") (do (= a " T4
      ") (if (and (== b " T2 ") (== a " T3 ")) (do 1) $fail))))))))) $fail) (match-fail "
      T1 ")))");

   /* A repeat that crosses a container boundary. */
   sv_test_match_ok(t, "match x | (a, [b, a]) do b end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T3 " ([ " T1 " 0)) (do (= "
      T4 " ([ " T1 " 1)) (if (is-list? " T4 ") (if (is-cons? " T4 ") (list-uncons " T4
      " " T5 " " T6 " (if (is-cons? " T6 ") (list-uncons " T6 " " T7 " " T8
      " (if (is-cons? " T8 ") $fail (do (= a " T3 ") (do (= b " T5 ") (do (= " T2 " " T7
      ") (if (== a " T2
      ") (do b) $fail)))))) $fail)) $fail) $fail))) $fail) (match-fail " T1 ")))");

   /* Fixed list elements, with the tail left unconstrained. */
   sv_test_match_ok(t, "match x | [p, p, ..ps] do p end",
      "(do (= " T1 " x) (| (if (is-list? " T1 ") (if (is-cons? " T1 ") (list-uncons " T1
      " " T3 " " T4 " (if (is-cons? " T4 ") (list-uncons " T4 " " T5 " " T6 " (do (= p "
      T3 ") (do (= ps " T6 ") (do (= " T2 " " T5 ") (if (== p " T2
      ") (do p) $fail))))) $fail)) $fail) $fail) (match-fail " T1 ")))");

   /* Record values are variables; the field names are not. */
   sv_test_match_ok(t, "match x | {a: v, b: v} do v end",
      "(do (= " T1 " x) (| (if (is-record? " T1 " 2) (if (has-field? " T1
      " a) (if (has-field? " T1 " b) (do (= " T3 " (. " T1 " a)) (do (= " T4 " (. " T1
      " b)) (do (= " T2 " " T4 ") (do (= v " T3 ") (if (== v " T2
      ") (do v) $fail))))) $fail) $fail) $fail) (match-fail " T1 ")))");

   /* A repeat merges into an existing guard, equality first so the cheap
    * test short circuits before the user's expression. */
   sv_test_match_ok(t, "match x | (a, a) when a > 1 do 5 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T3 " ([ " T1 " 0)) (do (= "
      T4 " ([ " T1 " 1)) (do (= " T2 " " T4 ") (do (= a " T3 ") (if (and (== a " T2
      ") (> a 1)) (do 5) $fail))))) $fail) (match-fail " T1 ")))");

   /* Wildcards are exempt, so these lower with no guard at all and consume
    * no temp. */
   sv_test_match_ok(t, "match x | (_, _) do 7 end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 2) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (do 7))) $fail) (match-fail " T1 ")))");

   sv_test_match_ok(t, "match x | (_, a, _) do a end",
      "(do (= " T1 " x) (| (if (is-tuple? " T1 " 3) (do (= " T2 " ([ " T1 " 0)) (do (= "
      T3 " ([ " T1 " 1)) (do (= " T4 " ([ " T1 " 2)) (do (= a " T3
      ") (do a))))) $fail) (match-fail " T1 ")))");
}

static inline void sv_test_match_oom(sv_testing_t* t)
{
   const char* src =
      "match x | 1 do 2 | \"a\" do 3 | (4, 5) do 6 | {k: 7, j: 8} do 9"
      " | [1, ..zs] do zs | (a, 1) do a | b when b > 0 do b"
      " | (c, d, d, c) do c"
      " | %{\"m\": q, ..} do q | y do y end";
   int64_t errored = 0;
   int64_t completed = 0;

   for (int64_t budget = 0; budget < 260; budget++) {
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
   sv_test_match_guards(t);
   sv_test_match_repeats(t);
   sv_test_match_oom(t);
}

#endif
