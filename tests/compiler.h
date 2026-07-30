#ifndef SV_TESTS_COMPILER_H
#define SV_TESTS_COMPILER_H

#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/compiler.h"
#include <math.h>

static inline value_t sv_test_compiler_eval(const char* src, bool* ok)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t vm = compile(src, &ctx);
   if (vm.chunk.bytecode.arr == NULL) {
      sv_str_deinit(&ctx.err.msg, &sv_gpa);
      *ok = false;
      return (value_t){ .kind = VALUE_NIL };
   }

   sv_opt_t(error_t) err = vm_run(&vm);
   *ok = !err.is_some && vm.stack.size == 1;
   value_t res = *ok ? value_borrow(sv_vec_last(vm.stack)) : (value_t){ .kind = VALUE_NIL };
   if (err.is_some)
      vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);
   return res;
}

static inline bool sv_test_compiler_num(const char* src, double expected)
{
   bool ok = false;
   value_t v = sv_test_compiler_eval(src, &ok);
   bool res = ok && v.kind == VALUE_NUMBER && v.number == expected;
   value_free(&v, &sv_gpa);
   return res;
}

static inline bool sv_test_compiler_kind(const char* src, value_kind expected)
{
   bool ok = false;
   value_t v = sv_test_compiler_eval(src, &ok);
   bool res = ok && v.kind == expected;
   value_free(&v, &sv_gpa);
   return res;
}

static inline bool sv_test_compiler_cmp(const char* src, value_t expected, bool (*cmp)(value_t, value_t))
{
   bool ok = false;
   value_t v = sv_test_compiler_eval(src, &ok);
   bool res = ok && cmp(v, expected);
   value_free(&v, &sv_gpa);
   value_free(&expected, &sv_gpa);
   return res;
}

static inline value_t sv_test_compiler_val(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline int sv_test_compiler_runtime_err(const char* src)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t vm = compile(src, &ctx);
   if (vm.chunk.bytecode.arr == NULL) {
      sv_str_deinit(&ctx.err.msg, &sv_gpa);
      return -1;
   }
   sv_opt_t(error_t) err = vm_run(&vm);
   int code = err.is_some ? err.value.error_code : -2;
   if (err.is_some)
      vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);
   return code;
}

static inline int sv_test_compiler_err(const char* src)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t vm = compile(src, &ctx);
   if (vm.chunk.bytecode.arr != NULL) {
      vm_deinit(&vm, &sv_gpa);
      return -1;
   }
   int code = ctx.err.error_code;
   sv_str_deinit(&ctx.err.msg, &sv_gpa);
   return code;
}

static inline void sv_test_compiler_basics(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("x = 5", 5));
   sv_test_run(t, sv_test_compiler_num("y = if x = 8.5 do x end", 8.5));
   sv_test_run(t, sv_test_compiler_num("1", 1));
   sv_test_run(t, sv_test_compiler_num("5 * 2.5", 12.5));
   sv_test_run(t, sv_test_compiler_num("8 / 2", 4));
   sv_test_run(t, sv_test_compiler_num("3 / 2", 1.5));
   sv_test_run(t, sv_test_compiler_num("3 - 2", 1));
   sv_test_run(t, sv_test_compiler_num("if 1 + 2 do 3 - 4 else 5 - 7 end", -1));
   sv_test_run(t, sv_test_compiler_num("if nil do 3 - 4 else 5 - 7 end", -2));
   sv_test_run(t, sv_test_compiler_num("if true do 3 - 4 end", -1));
   sv_test_run(t, sv_test_compiler_kind("if false do 3 - 4 end", VALUE_NIL));
   sv_test_run(t, sv_test_compiler_num("1 / 0", (double)INFINITY));
   sv_test_run(t, sv_test_compiler_kind("2 == 2", VALUE_BOOL));
   sv_test_run(t, sv_test_compiler_num("if 2 == 2 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 2 == 3 do 1 else 0 end", 0));
}

static inline void sv_test_compiler_comparisons(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_kind("1 < 2", VALUE_BOOL));
   sv_test_run(t, sv_test_compiler_num("if 1 < 2 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 2 < 2 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if 2 <= 2 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 3 <= 2 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if 2 > 1 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 1 > 1 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if 1 >= 1 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 1 >= 2 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if 1 != 2 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if 2 != 2 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if nil != 1 do 1 else 0 end", 1));
}

static inline void sv_test_compiler_maps(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_kind("%{}", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_kind("%{\"a\": 1, 2: 0}", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2} == %{1: 2} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2} == %{1: 3} do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2} == %{} do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2, 3: 4} == %{3: 4, 1: 2} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2, 1: 3} == %{1: 3} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if %{[1, 2]: \"v\"} == %{[1, 2]: \"v\"} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("x = 5\nif %{x: 1} == %{5: 1} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if %{\"k\": %{1: 2}} == %{\"k\": %{1: 2}} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if [1, 2, 3] == [1, 2, 3] do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if [1, 2] == [1, 2, 3] do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if [1, [2, 3]] == [1, [2, 3]] do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if \"a\" == \"a\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if \"a\" == \"b\" do 1 else 0 end", 0));
}

static inline void sv_test_compiler_indexing(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("[10, 20, 30][0]", 10));
   sv_test_run(t, sv_test_compiler_num("[10, 20, 30][2]", 30));
   sv_test_run(t, sv_test_compiler_num("i = 1\nx = [5, 6]\nx[i]", 6));
   sv_test_run(t, sv_test_compiler_num("x = [1, [2, 3]]\nx[1][0]", 2));
   sv_test_run(t, sv_test_compiler_num("%{\"a\": 1}[\"a\"]", 1));
   sv_test_run(t, sv_test_compiler_num("%{1: 2, 3: 4}[3]", 4));
   sv_test_run(t, sv_test_compiler_num("m = %{[1, 2]: 9}\nm[[1, 2]]", 9));
   sv_test_run(t, sv_test_compiler_num("k = \"a\"\n%{\"a\": 5, \"b\": 6}[k]", 5));
   sv_test_run(t, sv_test_compiler_num("if %{1: 2}[1] == 2 do 1 else 0 end", 1));

   sv_test_run(t, sv_test_compiler_runtime_err("[1, 2][5]") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("[1][0 - 1]") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("%{1: 2}[3]") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("[1][\"a\"]") == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, sv_test_compiler_runtime_err("[1][0.5]") == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, sv_test_compiler_runtime_err("5[0]") == VM_ERR_OP_UNSUPPORTED_ARGS);
}

static inline void sv_test_compiler_tuples(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_kind("{}", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_num("t = {x: 10, y: 20}\nt.x", 10));
   sv_test_run(t, sv_test_compiler_num("t = {x: 10, y: 20}\nt.y", 20));
   sv_test_run(t, sv_test_compiler_num("{a: {b: 5}}.a.b", 5));
   sv_test_run(t, sv_test_compiler_num("if {x: 1, y: 2} == {y: 2, x: 1} do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if {x: 1} == {y: 1} do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if {x: 1} == {x: 2} do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num(
      "fun add(v1, v2) =\n"
      "    x = v1.x + v2.x\n"
      "    y = v1.y + v2.y\n"
      "    {x: x, y: y}\n"
      "end\n"
      "add({x: 1, y: 2}, {x: 3, y: 4}).y", 6));
   sv_test_run(t, sv_test_compiler_num("a = {y: 1}\nb = {x: 2, y: 3}\nb.y", 3));

   value_t add_items[] = {
      sv_test_compiler_val(0), sv_test_compiler_val(4),
      sv_test_compiler_val(1), sv_test_compiler_val(6),
   };
   sv_test_run(t, sv_test_compiler_cmp(
      "fun add(v1, v2) =\n"
      "    x = v1.x + v2.x\n"
      "    y = v1.y + v2.y\n"
      "    {x: x, y: y}\n"
      "end\n"
      "add({x: 1, y: 2}, {x: 3, y: 4})",
      value_init_tuple(add_items, 2, &sv_gpa), value_eql));

   value_t inner_items[] = { sv_test_compiler_val(1), sv_test_compiler_val(5) };
   value_t inner = value_init_tuple(inner_items, 1, &sv_gpa);
   value_t outer_items[] = { sv_test_compiler_val(0), inner };
   sv_test_run(t, sv_test_compiler_cmp("{a: {b: 5}}",
      value_init_tuple(outer_items, 1, &sv_gpa), value_eql));
   value_free(&inner, &sv_gpa);

   sv_test_run(t, sv_test_compiler_err("{x: 1, x: 2}") == C_ERR_REDEFINED);
   sv_test_run(t, sv_test_compiler_runtime_err("{x: 1}.y") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("t = 5\nt.x") == VM_ERR_OP_UNSUPPORTED_ARGS);

   ctx_t pctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t pvm = compile("t = {x: 10, y: 20}\nt", &pctx);
   sv_test_run(t, pvm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) perr = vm_run(&pvm);
   sv_test_run(t, !perr.is_some);
   sv_str_t ptext = value_to_str(pvm.stack.arr[pvm.stack.size - 1], &pvm.ctx);
   sv_test_run(t, sv_str_comp(ptext, sv_str_init("{x: 10, y: 20}")));
   sv_str_deinit(&ptext, &sv_gpa);
   vm_deinit(&pvm, &sv_gpa);
}

static inline void sv_test_compiler_logic(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("if not false do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if not 5 do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if not nil do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_kind("not nil", VALUE_BOOL));

   sv_test_run(t, sv_test_compiler_num("1 and 2", 2));
   sv_test_run(t, sv_test_compiler_kind("nil and 2", VALUE_NIL));
   sv_test_run(t, sv_test_compiler_num("false or 3", 3));
   sv_test_run(t, sv_test_compiler_num("1 or 2", 1));
   sv_test_run(t, sv_test_compiler_num("x = nil\ny = x or 5\ny + 1", 6));
   sv_test_run(t, sv_test_compiler_kind("fun boom() = [1][9] end\nfalse and boom()", VALUE_BOOL));
   sv_test_run(t, sv_test_compiler_num("fun boom() = [1][9] end\n1 or boom()", 1));
   sv_test_run(t, sv_test_compiler_num("if 1 == 1 and 2 == 2 do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if (false and true) or true do 1 else 0 end", 1));

   sv_test_run(t, sv_test_compiler_num("if \"a\" + \"b\" == \"ab\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("s = \"ab\"\nif s + s == \"abab\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_runtime_err("\"a\" + 1") == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, sv_test_compiler_runtime_err("1 + \"a\"") == VM_ERR_OP_UNSUPPORTED_ARGS);

   sv_test_run(t, sv_test_compiler_num("1; 2", 2));
   sv_test_run(t, sv_test_compiler_num("2;", 2));
   sv_test_run(t, sv_test_compiler_num("x = nil; (x or 5) + 1", 6));
   sv_test_run(t, sv_test_compiler_num("i = 1; [5, 6][i]", 6));
   sv_test_run(t, sv_test_compiler_num("fun f() = 1; 2 end f()", 2));

   sv_test_run(t, sv_test_compiler_num("fun inc(x) = x + 1 end\n5 |> inc()", 6));
   sv_test_run(t, sv_test_compiler_num("fun add2(x, y) = x + y end\n1 |> add2(2)", 3));
   sv_test_run(t, sv_test_compiler_num("fun inc(x) = x + 1 end\n5 |> inc() |> inc()", 7));
   sv_test_run(t, sv_test_compiler_err("fun inc(x) = x + 1 end\n5 |> inc") == C_ERR_UNEXPECTED_SEXPR);
}

static inline void sv_test_compiler_runtime_errors(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_runtime_err("for x in 5 do x end") == VM_ERR_OP_UNSUPPORTED_ARGS);

   ctx_t actx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t avm = compile("fun f(x) = x end f(1, 2)", &actx);
   sv_test_run(t, avm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) aerr = vm_run(&avm);
   sv_test_run(t, aerr.is_some);
   sv_test_run(t, aerr.value.error_code == VM_ERR_BAD_ARITY);
   sv_test_run(t, ((vm_arity_err*)aerr.value.payload)->expected == 1);
   sv_test_run(t, ((vm_arity_err*)aerr.value.payload)->got == 2);
   vm_err_deinit(&aerr.value, &sv_gpa);
   vm_deinit(&avm, &sv_gpa);

   ctx_t cctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t cvm = compile("[1] < 2", &cctx);
   sv_test_run(t, cvm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) cerr = vm_run(&cvm);
   sv_test_run(t, cerr.is_some);
   sv_test_run(t, cerr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_err_deinit(&cerr.value, &sv_gpa);
   vm_deinit(&cvm, &sv_gpa);
}

static inline void sv_test_compiler_for(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num(
      "for x in [1, 2, 3] do\n"
      "     k = x + 2\n"
      "     k\n"
      "end", 5));

   sv_test_run(t, sv_test_compiler_num("if (for c in \"abc\" do c end) == \"c\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if (for c in \"aé😀\" do c end) == \"😀\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if (for c in \"ab\" do c + \"!\" end) == \"b!\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_kind("for x in [] do x end", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_kind("for c in \"\" do c end", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_runtime_err("for x in %{} do x end") == VM_ERR_OP_UNSUPPORTED_ARGS);
}

static inline void sv_test_compiler_functions(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("fun f(x) = x + 1 end f(2)", 3));
   sv_test_run(t, sv_test_compiler_num("x = 10\nfun f(y) = x + y end f(5)", 15));
   sv_test_run(t, sv_test_compiler_num("x = 1\ny = 2\nfun f() = x + y end\nz = 4\nf() + z", 7));
   sv_test_run(t, sv_test_compiler_num("fun add(x, y) = x + y end add(3, 4)", 7));
   sv_test_run(t, sv_test_compiler_num("fun f(x) = x + 1 end f(f(2))", 4));
   sv_test_run(t, sv_test_compiler_num(
      "fun h(x) =\n"
      "     k = x + 1\n"
      "     k\n"
      "end\n"
      "h(2)", 3));
   sv_test_run(t, sv_test_compiler_num(
      "fun f() =\n"
      "  fun g(x) =\n"
      "      x + 4\n"
      "  end\n"
      "\n"
      "  g\n"
      "end\n"
      "f()(5)", 9));
   sv_test_run(t, sv_test_compiler_kind("fun g(x) = x end g([1, 2, 3])", VALUE_OBJ));
}

static inline void sv_test_compiler_closures(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) =\n"
      "     fun g[x](y) =\n"
      "         x + y\n"
      "     end\n"
      "     g\n"
      "end\n"
      "f(4)(5)", 9));
   sv_test_run(t, sv_test_compiler_num(
      "x = 5\n"
      "fun f[x](y) =\n"
      "     x + y\n"
      "end\n"
      "f(4)\n"
      "f(5)\n"
      "f(9)", 14));
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) =\n"
      "     fun g[x](y) =\n"
      "         x + y\n"
      "     end\n"
      "     g\n"
      "end\n"
      "a = f(1)\n"
      "b = f(2)\n"
      "a(10) + b(10)", 23));
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) =\n"
      "     fun g[x](y) =\n"
      "         fun h[x,y](z) =\n"
      "             x + y + z\n"
      "         end\n"
      "         h\n"
      "     end\n"
      "     g\n"
      "end\n"
      "f(1)(2)(3)", 6));
}

static inline void sv_test_compiler_recursion(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num(
      "fun fib(y) =\n"
      "     if y == 0 do 0\n"
      "     else if y == 1 do 1\n"
      "     else fib(y - 1) + fib(y - 2)\n"
      "     end\n"
      "end\n"
      "fib(10)", 55));
   sv_test_run(t, sv_test_compiler_kind("fun f(x) = f end f(1)", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_kind(
      "x = [1, 2]\n"
      "fun f[x](y) =\n"
      "     f\n"
      "end\n"
      "f(1)", VALUE_OBJ));
}

static inline void sv_test_compiler_groups(sv_testing_t* t)
{
   bool ok = false;
   value_t v = sv_test_compiler_eval(
      "fun\n"
      "| is_even(x) =\n"
      "     if x == 0 do true\n"
      "     else is_odd(x - 1)\n"
      "     end\n"
      "| is_odd(x) =\n"
      "     if x == 0 do false\n"
      "     else is_even(x - 1)\n"
      "     end\n"
      "end\n"
      "is_even(10)", &ok);
   sv_test_run(t, ok);
   sv_test_run(t, v.kind == VALUE_BOOL);
   sv_test_run(t, v.boolean);
   value_free(&v, &sv_gpa);
}

static inline void sv_test_compiler_errors(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_err("y + 1") == C_ERR_UNDEFINED_VARIABLE);
   sv_test_run(t, sv_test_compiler_err("x = 1\nx = 2") == C_ERR_REDEFINED);
   sv_test_run(t, sv_test_compiler_err("1 |> 2") == C_ERR_UNEXPECTED_SEXPR);
   sv_test_run(t, sv_test_compiler_err("1, 2") == C_ERR_NOT_IMPLEMENTED);
}

static inline void sv_test_compiler(sv_testing_t* t)
{
   sv_test_compiler_basics(t);
   sv_test_compiler_comparisons(t);
   sv_test_compiler_maps(t);
   sv_test_compiler_indexing(t);
   sv_test_compiler_tuples(t);
   sv_test_compiler_logic(t);
   sv_test_compiler_for(t);
   sv_test_compiler_functions(t);
   sv_test_compiler_closures(t);
   sv_test_compiler_recursion(t);
   sv_test_compiler_groups(t);
   sv_test_compiler_errors(t);
   sv_test_compiler_runtime_errors(t);
}

#endif
