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

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
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

static inline void sv_test_compiler_runtime_errors(sv_testing_t* t)
{
   ctx_t ctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t vm = compile("for x in 5 do x end", &ctx);
   sv_test_run(t, vm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, err.is_some);
   sv_test_run(t, err.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);

   ctx_t actx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t avm = compile("fun f(x) = x end f(1, 2)", &actx);
   sv_test_run(t, avm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) aerr = vm_run(&avm, &sv_gpa);
   sv_test_run(t, aerr.is_some);
   sv_test_run(t, aerr.value.error_code == VM_ERR_BAD_ARITY);
   sv_test_run(t, ((vm_arity_err*)aerr.value.payload)->expected == 1);
   sv_test_run(t, ((vm_arity_err*)aerr.value.payload)->got == 2);
   vm_err_deinit(&aerr.value, &sv_gpa);
   vm_deinit(&avm, &sv_gpa);

   ctx_t cctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t cvm = compile("[1] < 2", &cctx);
   sv_test_run(t, cvm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) cerr = vm_run(&cvm, &sv_gpa);
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
   sv_test_run(t, sv_test_compiler_err("1 |> 2") == C_ERR_NOT_IMPLEMENTED);
}

static inline void sv_test_compiler(sv_testing_t* t)
{
   sv_test_compiler_basics(t);
   sv_test_compiler_comparisons(t);
   sv_test_compiler_maps(t);
   sv_test_compiler_for(t);
   sv_test_compiler_functions(t);
   sv_test_compiler_closures(t);
   sv_test_compiler_recursion(t);
   sv_test_compiler_groups(t);
   sv_test_compiler_errors(t);
   sv_test_compiler_runtime_errors(t);
}

#endif
