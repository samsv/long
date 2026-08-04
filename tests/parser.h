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

static inline void sv_test_parse_ok(sv_testing_t* t, const char* src, const char* expected)
{
   ctx_t ctx = sv_test_parse_ctx();
   scanner_t s = scanner_init(sv_str_init(src));
   sexpr_t e = parser_expr(&s, &ctx);

   sv_test_run_msg(t, ctx.err.error_code == 0, "parse error for \"%s\"", src);
   sv_str_t formatted = sexpr_format(e, &ctx.alloc);
   sv_test_run_msg(t, sv_str_comp(formatted, sv_str_init(expected)),
                   "\"%s\" should format as %s, got %.*s",
                   src, expected, (int)formatted.size, formatted.chars);

   sv_str_deinit(&formatted, &ctx.alloc);
   sexpr_free(&e, &ctx.alloc);
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
   sv_test_parse_ok(t, "1 * 2 + 3", "(+ (* 1 2) 3)");
   sv_test_parse_ok(t, "0", "0");
   sv_test_parse_ok(t, "(((0)))", "0");
   sv_test_parse_ok(t, "- 1 * (2 + 3)", "(* (- 1) (+ 2 3))");
   sv_test_parse_ok(t, "x.hwllo |> world()", "(|> (. x hwllo) (world))");
   sv_test_parse_ok(t, "x[0][1]", "([ ([ x 0) 1)");
   sv_test_parse_error(t, ", 2", PARSER_ERROR_UNEXPECTED_TOKEN);

   sv_test_parse_ok(t, "(1, 2)", "(tuple 1 2)");
   sv_test_parse_ok(t, "(a, b, c)", "(tuple a b c)");
   sv_test_parse_ok(t, "(1,)", "(tuple 1)");
   sv_test_parse_ok(t, "(a, b,)", "(tuple a b)");
   sv_test_parse_ok(t, "(1)", "1");
   sv_test_parse_ok(t, "((1, 2))", "(tuple 1 2)");
   sv_test_parse_ok(t, "(myfun(x, y),)",
                    "(tuple (myfun x y))");
   sv_test_parse_ok(t, "(1 + 2, 3)", "(tuple (+ 1 2) 3)");
   sv_test_parse_error(t, "()", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "(1, 2", PARSER_ERROR_EOF);
   sv_test_parse_ok(t, "x = y = 2", "(= x (= y 2))");
   sv_test_parse_ok(t, "world(1, 2, 3)", "(world 1 2 3)");
   sv_test_parse_ok(t, "3.14", "3.14");
   sv_test_parse_ok(t, "caf\xC3\xA9", "caf\xC3\xA9");
   sv_test_parse_ok(t, "\"a\nb\"", "\"a\nb\"");
}

static inline void sv_test_parser_if(sv_testing_t* t)
{
   sv_test_parse_ok(t, "if x + 5 do y + 1 end", "(if (+ x 5) (do (+ y 1)))");
   sv_test_parse_ok(t, "if x + 5 do y + 1 else z + 1 end",
                    "(if (+ x 5) (do (+ y 1)) (do (+ z 1)))");
   sv_test_parse_ok(t, "if x + 5 do 1 else z + 1 end", "(if (+ x 5) (do 1) (do (+ z 1)))");
   sv_test_parse_ok(t, "if x + 5 do 1 else if x + 6 do z + 1 else k + 9 end",
                    "(if (+ x 5) (do 1) (if (+ x 6) (do (+ z 1)) (do (+ k 9))))");
}

static inline void sv_test_parser_forms(sv_testing_t* t)
{
   sv_test_parse_ok(t, "for x in xs do x end", "(for (x xs) (do x))");
   sv_test_parse_ok(t, "fun add(x, y) x + y",
                    "(fun add (x y) (+ x y))");
   sv_test_parse_ok(t, "fun f[a](x) a + x",
                    "(fun f (a) (x) (+ a x))");
   sv_test_parse_ok(t, "fun | even(n) n | odd(n) n end",
                    "(fun (even (n) n) (odd (n) n))");

   /* `do ... end` is a block expression, so a function body needs no wrapper of
    * its own: a bare expression body produces no `do` node, and therefore no
    * redundant scope at compile time. */
   sv_test_parse_ok(t, "do 1 end", "(do 1)");
   sv_test_parse_ok(t, "do x = 1; x end", "(do (= x 1) x)");
   sv_test_parse_ok(t, "y = do 1 + 2 end", "(= y (do (+ 1 2)))");
   sv_test_parse_ok(t, "fun f(x) x + 2", "(fun f (x) (+ x 2))");
   sv_test_parse_ok(t, "fun f(x) do y = 3; y + x end",
                    "(fun f (x) (do (= y 3) (+ y x)))");

   /* Clauses after the parameter list desugar to a match on the parameters. */
   sv_test_parse_ok(t, "fun f(x) | 0 do true | _ do 1 end",
                    "(fun f (x) (match x (tuple 0 (do true)) (tuple _ (do 1))))");
   sv_test_parse_ok(t, "fun fn(a, b) | (1, y) do y | (x, 2) do x end",
                    "(fun fn (a b) (match (tuple a b) (tuple (tuple 1 y) (do y)) (tuple"
                    " (tuple x 2) (do x))))");
   sv_test_parse_ok(t, "fun | f(x) | 0 do 1 | g(y) | 0 do 2 end",
                    "(fun (f (x) (match x (tuple 0 (do 1)))) (g (y) (match y (tuple 0"
                    " (do 2)))))");
   sv_test_parse_ok(t, "fun | f(x) 1 | g(y) | 0 do 2 end",
                    "(fun (f (x) 1) (g (y) (match y (tuple 0 (do 2)))))");
   sv_test_parse_ok(t, "fun f[a](x) | 0 do a end",
                    "(fun f (a) (x) (match x (tuple 0 (do a))))");

   /* A variable binds the whole tuple; a tuple pattern is fine against one
    * parameter because that parameter may hold a tuple. */
   sv_test_parse_ok(t, "fun f(a, b) | x do x end",
                    "(fun f (a b) (match (tuple a b) (tuple x (do x))))");
   sv_test_parse_ok(t, "fun f(x) | (1, 2) do y end",
                    "(fun f (x) (match x (tuple (tuple 1 2) (do y))))");

   /* Clause bodies are blocks. */
   sv_test_parse_ok(t, "fun f(x) | 0 do y = 3; y + 1 end",
                    "(fun f (x) (match x (tuple 0 (do (= y 3) (+ y 1)))))");
   sv_test_parse_ok(t, "match x | 1 do y = 3; y + 1 end",
                    "(match x (tuple 1 (do (= y 3) (+ y 1))))");

   /* A guard rides inside the body slot, so a clause keeps a fixed size. */
   sv_test_parse_ok(t, "match x | 1 when g do 2 end",
                    "(match x (tuple 1 (when g (do 2))))");
   sv_test_parse_ok(t, "fun f(x) | 0 when g do 1 | _ do 2 end",
                    "(fun f (x) (match x (tuple 0 (when g (do 1))) (tuple _ (do 2))))");
   sv_test_parse_ok(t, "fun | f(x) | 0 when g do 1 end",
                    "(fun (f (x) (match x (tuple 0 (when g (do 1))))))");
   sv_test_parse_ok(t, "fun f(a, b) | (x, y) when x > y do x end",
                    "(fun f (a b) (match (tuple a b) (tuple (tuple x y) (when (> x y)"
                    " (do x)))))");
   sv_test_parse_ok(t, "fun fib(x) | x when x <= 1 do 1 | x do fib(x - 1) + fib(x - 2) end",
                    "(fun fib (x) (match x (tuple x (when (<= x 1) (do 1))) (tuple x (do"
                    " (+ (fib (- x 1)) (fib (- x 2)))))))");

   /* `do` closes the guard, so the guard is a full expression: it swallows `and`,
    * an assignment, and even a nested `if` with its own `do`. */
   sv_test_parse_ok(t, "match x | n when n > 0 and n < 10 do 1 end",
                    "(match x (tuple n (when (and (> n 0) (< n 10)) (do 1))))");
   sv_test_parse_ok(t, "match x | 1 when (g = 2) do 3 end",
                    "(match x (tuple 1 (when (= g 2) (do 3))))");
   sv_test_parse_ok(t, "match x | 1 when if g do 1 else 2 end do 3 end",
                    "(match x (tuple 1 (when (if g (do 1) (do 2)) (do 3))))");
   sv_test_parse_ok(t, "match x | 1 when g do y = 3; y end",
                    "(match x (tuple 1 (when g (do (= y 3) y))))");
   /* A destructuring LHS is parsed as an ordinary expression, so `..` is now legal
    * in list and record expressions and the compiler rejects it in value position. */
   sv_test_parse_ok(t, "(a, b) = f()", "(= (tuple a b) (f))");
   sv_test_parse_ok(t, "[h, ..t] = lst", "(= (list h (.. t)) lst)");
   sv_test_parse_ok(t, "{x: a, ..} = rec", "(= (record x a ..) rec)");
   sv_test_parse_ok(t, "[a, [b, ..r]] = l", "(= (list a (list b (.. r))) l)");
   sv_test_parse_ok(t, "%{\"k\": v} = m", "(= (hashmap \"k\" v) m)");

   sv_test_parse_ok(t, "[1, 2, 3]", "(list 1 2 3)");
   sv_test_parse_ok(t, "[]", "(list)");
   sv_test_parse_ok(t, "list(1, 2)", "(list 1 2)");
   sv_test_parse_ok(t, "f()", "(f)");
   sv_test_parse_ok(t, "\"hi\" |> print()", "(|> \"hi\" (print))");
}

static inline void sv_test_parser_program_fn(sv_testing_t* t)
{
   ctx_t ctx = sv_test_parse_ctx();

   scanner_t s = scanner_init(sv_str_init("1 + 1\n2 * 2"));
   sexpr_t e = parser_program(&s, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sv_str_t formatted = sexpr_format(e, &ctx.alloc);
   sv_test_run(t, sv_str_comp(formatted, sv_str_init("(do (+ 1 1) (* 2 2))")));
   sv_str_deinit(&formatted, &ctx.alloc);
   sexpr_free(&e, &ctx.alloc);

   scanner_t empty = scanner_init(sv_str_init(""));
   sexpr_t ep = parser_program(&empty, &ctx);
   sv_test_run(t, ctx.err.error_code == 0);
   sv_str_t ef = sexpr_format(ep, &ctx.alloc);
   sv_test_run(t, sv_str_comp(ef, sv_str_init("(do)")));
   sv_str_deinit(&ef, &ctx.alloc);
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
   sv_str_t sf = sexpr_format(es, &ctx.alloc);
   sv_test_run(t, sv_str_comp(sf, sv_str_init("(do (= x 1) (= y 2))")));
   sv_str_deinit(&sf, &ctx.alloc);
   sexpr_free(&es, &ctx.alloc);
}

static inline void sv_test_parser_maps(sv_testing_t* t)
{
   sv_test_parse_ok(t, "%{}", "(hashmap)");
   sv_test_parse_ok(t, "%{\"key1\": \"value1\", 2: 0}", "(hashmap \"key1\" \"value1\" 2 0)");
   sv_test_parse_ok(t, "%{[1, 2, 3]: \"other val\", x: y}", "(hashmap (list 1 2 3) \"other val\" x y)");
   sv_test_parse_ok(t, "%{1: %{2: 3}}", "(hashmap 1 (hashmap 2 3))");
   sv_test_parse_ok(t, "%{1 + 2: f(3)}", "(hashmap (+ 1 2) (f 3))");
   sv_test_parse_ok(t, "m = %{1: 2}", "(= m (hashmap 1 2))");

   sv_test_parse_ok(t, "a |> f() |> g()", "(|> (|> a (f)) (g))");
   sv_test_parse_ok(t, "not true", "(not true)");
   sv_test_parse_ok(t, "1 and 2 or 3", "(or (and 1 2) 3)");
   sv_test_parse_ok(t, "1 or 2 and 3", "(or 1 (and 2 3))");
   sv_test_parse_ok(t, "a and b == c", "(and a (== b c))");
   sv_test_parse_ok(t, "not a == b", "(not (== a b))");
   sv_test_parse_ok(t, "not a and b", "(and (not a) b)");
   sv_test_parse_ok(t, "f(a or b)", "(f (or a b))");
   sv_test_parse_ok(t, "if true do 1; 2 end", "(if true (do 1 2))");
   sv_test_parse_ok(t, "if true do 1; 2; end", "(if true (do 1 2))");
   sv_test_parse_error(t, "x = ;", PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "f(1; 2)", PARSER_ERROR_UNEXPECTED_TOKEN);

   sv_test_parse_ok(t, "{x: 1, y: 2}", "(record x 1 y 2)");
   sv_test_parse_ok(t, "{}", "(record)");
   sv_test_parse_ok(t, "{x: 1 + 2}", "(record x (+ 1 2))");
   sv_test_parse_ok(t, "v.x + v.y", "(+ (. v x) (. v y))");
   sv_test_parse_ok(t, "{x: 1}.x", "(. (record x 1) x)");
   sv_test_parse_ok(t, "t.a.b", "(. (. t a) b)");
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
   sv_test_parse_ok(t, "match x | 1 do 2 end",
                    "(match x (tuple 1 (do 2)))");
   sv_test_parse_ok(t, "match x end",
                    "(match x)");
   sv_test_parse_ok(t,
      "match (a, b)\n"
      "| (false, y) do y\n"
      "| (true, true) do false\n"
      "| (true, false) do true\n"
      "end",
                    "(match (tuple a b) (tuple (tuple false y) (do y)) "
                    "(tuple (tuple true true) (do false)) (tuple (tuple true "
                    "false) (do true)))");
   sv_test_parse_ok(t, "match x | [h, ..t] do h end",
                    "(match x (tuple (list h (.. t)) (do h)))");
   /* The list tail is a pattern, not just a variable: `..[]` pins an exact length
    * and `..[b]` matches a one element remainder. */
   sv_test_parse_ok(t, "match x | [a, ..[]] do a end",
                    "(match x (tuple (list a (.. (list))) (do a)))");
   sv_test_parse_ok(t, "match x | [a, ..[b]] do b end",
                    "(match x (tuple (list a (.. (list b))) (do b)))");
   sv_test_parse_ok(t, "match x | [a, ..[b, ..c]] do c end",
                    "(match x (tuple (list a (.. (list b (.. c)))) (do c)))");
   sv_test_parse_ok(t, "[a, ..[]] = l", "(= (list a (.. (list))) l)");
   sv_test_parse_ok(t, "match x | [] do 0 | [a, b] do a end",
                    "(match x (tuple (list) (do 0)) (tuple (list a b) "
                    "(do a)))");
   sv_test_parse_ok(t, "match x | {x: 1, y: p} do p end",
                    "(match x (tuple (record x 1 y p) (do p)))");
   sv_test_parse_ok(t, "match x | {x: 1, ..} do 1 end",
                    "(match x (tuple (record x 1 ..) (do 1)))");
   sv_test_parse_ok(t, "match x | {..} do 1 end",
                    "(match x (tuple (record ..) (do 1)))");
   sv_test_parse_ok(t, "match x | %{\"k\": v} do v end",
                    "(match x (tuple (hashmap \"k\" v) (do v)))");
   sv_test_parse_ok(t, "match x | (1, [2, ..r]) do r end",
                    "(match x (tuple (tuple 1 (list 2 (.. r))) (do r)))");
   sv_test_parse_ok(t, "match x | (1) do 2 end",
                    "(match x (tuple 1 (do 2)))");
   sv_test_parse_ok(t, "match x | (\"a\",) do 1 end",
                    "(match x (tuple (tuple \"a\") (do 1)))");
   sv_test_parse_ok(t, "match f(1) | y do y end",
                    "(match (f 1) (tuple y (do y)))");

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
   sv_test_parse_error(t, "match x | [a, ..\"s\"] do a end",
                       (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "(a, ..b) = t", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f[a, ..b](x) x", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
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

   /* `when` is a keyword now, so it is no longer usable as a name. */
   sv_test_parse_error(t, "when = 1", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "fun f(when) 1", (int)PARSER_ERROR_UNEXPECTED_TOKEN);
   sv_test_parse_error(t, "world(1, 2", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "x[0", (int)PARSER_ERROR_EOF);
   sv_test_parse_error(t, "class", (int)PARSER_ERROR_NOT_IMPLEMENTED);
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
}

static inline void sv_test_parser(sv_testing_t* t)
{
   sv_test_parser_exprs(t);
   sv_test_parser_if(t);
   sv_test_parser_forms(t);
   sv_test_parser_program_fn(t);
   sv_test_parser_maps(t);
   sv_test_parser_match(t);
   sv_test_parser_errors(t);
   sv_test_parser_oom(t);
}

#endif
