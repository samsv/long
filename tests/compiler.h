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
      "fun add(v1, v2) do\n"
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
      "fun add(v1, v2) do\n"
      "    x = v1.x + v2.x\n"
      "    y = v1.y + v2.y\n"
      "    {x: x, y: y}\n"
      "end\n"
      "add({x: 1, y: 2}, {x: 3, y: 4})",
      value_init_record(add_items, 2, &sv_gpa), value_eql));

   value_t inner_items[] = { sv_test_compiler_val(1), sv_test_compiler_val(5) };
   value_t inner = value_init_record(inner_items, 1, &sv_gpa);
   value_t outer_items[] = { sv_test_compiler_val(0), inner };
   sv_test_run(t, sv_test_compiler_cmp("{a: {b: 5}}",
      value_init_record(outer_items, 1, &sv_gpa), value_eql));
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

static inline void sv_test_compiler_tuple_type(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("t = (1, \"a\")\nt[0]", 1));
   sv_test_run(t, sv_test_compiler_num("if (1, \"a\")[1] == \"a\" do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("(1,)[0]", 1));
   sv_test_run(t, sv_test_compiler_num("x = ((1, 2), 3)\nx[0][1]", 2));
   sv_test_run(t, sv_test_compiler_num("if (1, 2) == (1, 2) do 1 else 0 end", 1));
   sv_test_run(t, sv_test_compiler_num("if (1, 2) == (2, 1) do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("if (1, 2) == (1, 2, 3) do 1 else 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("i = 1\nt = (5, 6)\nt[i]", 6));

   sv_test_run(t, sv_test_compiler_runtime_err("(1, 2)[5]") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("(1, 2)[0 - 1]") == VM_ERR_KEY_NOT_FOUND);
   sv_test_run(t, sv_test_compiler_runtime_err("(1, 2)[0.5]") == VM_ERR_OP_UNSUPPORTED_ARGS);

   ctx_t tctx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t tvm = compile("t = (1, (2, \"s\"))\nt", &tctx);
   sv_test_run(t, tvm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) terr = vm_run(&tvm);
   sv_test_run(t, !terr.is_some);
   sv_str_t ttext = value_to_str(tvm.stack.arr[tvm.stack.size - 1], &tvm.ctx);
   sv_test_run(t, sv_str_comp(ttext, sv_str_init("(1, (2, s))")));
   sv_str_deinit(&ttext, &sv_gpa);
   vm_deinit(&tvm, &sv_gpa);

   ctx_t octx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t ovm = compile("(1,)", &octx);
   sv_test_run(t, ovm.chunk.bytecode.arr != NULL);
   sv_opt_t(error_t) oerr = vm_run(&ovm);
   sv_test_run(t, !oerr.is_some);
   sv_str_t otext = value_to_str(ovm.stack.arr[ovm.stack.size - 1], &ovm.ctx);
   sv_test_run(t, sv_str_comp(otext, sv_str_init("(1,)")));
   sv_str_deinit(&otext, &sv_gpa);
   vm_deinit(&ovm, &sv_gpa);
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
   sv_test_run(t, sv_test_compiler_kind("fun boom() do [1][9] end\nfalse and boom()", VALUE_BOOL));
   sv_test_run(t, sv_test_compiler_num("fun boom() do [1][9] end\n1 or boom()", 1));
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
   sv_test_run(t, sv_test_compiler_num("fun f() do 1; 2 end f()", 2));

   sv_test_run(t, sv_test_compiler_num("fun inc(x) do x + 1 end\n5 |> inc()", 6));
   sv_test_run(t, sv_test_compiler_num("fun add2(x, y) do x + y end\n1 |> add2(2)", 3));
   sv_test_run(t, sv_test_compiler_num("fun inc(x) do x + 1 end\n5 |> inc() |> inc()", 7));
   sv_test_run(t, sv_test_compiler_err("fun inc(x) do x + 1 end\n5 |> inc") == C_ERR_UNEXPECTED_SEXPR);
}

static inline void sv_test_compiler_runtime_errors(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_runtime_err("for x in 5 do x end") == VM_ERR_OP_UNSUPPORTED_ARGS);

   ctx_t actx = { .alloc = sv_gpa, .logger = sv_std_logger, .err = error_init() };
   vm_t avm = compile("fun f(x) do x end f(1, 2)", &actx);
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
   sv_test_run(t, sv_test_compiler_num("fun f(x) do x + 1 end f(2)", 3));
   sv_test_run(t, sv_test_compiler_num("x = 10\nfun f(y) do x + y end\nf(5)", 15));
   sv_test_run(t, sv_test_compiler_num("x = 1\ny = 2\nfun f() do x + y end\nz = 4\nf() + z", 7));
   sv_test_run(t, sv_test_compiler_num("fun add(x, y) do x + y end add(3, 4)", 7));
   sv_test_run(t, sv_test_compiler_num("fun f(x) do x + 1 end f(f(2))", 4));
   sv_test_run(t, sv_test_compiler_num(
      "fun h(x) do\n"
      "     k = x + 1\n"
      "     k\n"
      "end\n"
      "h(2)", 3));
   sv_test_run(t, sv_test_compiler_num(
      "fun f() do\n"
      "  fun g(x) do\n"
      "      x + 4\n"
      "  end\n"
      "\n"
      "  g\n"
      "end\n"
      "f()(5)", 9));
   sv_test_run(t, sv_test_compiler_kind("fun g(x) do x end g([1, 2, 3])", VALUE_OBJ));
}

static inline void sv_test_compiler_closures(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) do\n"
      "     fun g[x](y) do\n"
      "         x + y\n"
      "     end\n"
      "     g\n"
      "end\n"
      "f(4)(5)", 9));
   sv_test_run(t, sv_test_compiler_num(
      "x = 5\n"
      "fun f[x](y) do\n"
      "     x + y\n"
      "end\n"
      "f(4)\n"
      "f(5)\n"
      "f(9)", 14));
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) do\n"
      "     fun g[x](y) do\n"
      "         x + y\n"
      "     end\n"
      "     g\n"
      "end\n"
      "a = f(1)\n"
      "b = f(2)\n"
      "a(10) + b(10)", 23));
   sv_test_run(t, sv_test_compiler_num(
      "fun f(x) do\n"
      "     fun g[x](y) do\n"
      "         fun h[x,y](z) do\n"
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
      "fun fib(y) do\n"
      "     if y == 0 do 0\n"
      "     else if y == 1 do 1\n"
      "     else fib(y - 1) + fib(y - 2)\n"
      "     end\n"
      "end\n"
      "fib(10)", 55));
   sv_test_run(t, sv_test_compiler_kind("fun f(x) do f end f(1)", VALUE_OBJ));
   sv_test_run(t, sv_test_compiler_kind(
      "x = [1, 2]\n"
      "fun f[x](y) do\n"
      "     f\n"
      "end\n"
      "f(1)", VALUE_OBJ));
}

static inline void sv_test_compiler_groups(sv_testing_t* t)
{
   bool ok = false;
   value_t v = sv_test_compiler_eval(
      "fun\n"
      "| is_even(x)\n"
      "     if x == 0 do true\n"
      "     else is_odd(x - 1)\n"
      "     end\n"
      "| is_odd(x)\n"
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
}

static inline void sv_test_compiler_match(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("match 1 | 1 do 2 end", 2));
   sv_test_run(t, sv_test_compiler_num("match 2 | 1 do 2 | 2 do 3 end", 3));
   sv_test_run(t, sv_test_compiler_runtime_err("match 9 | 1 do 2 end")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_num("match 9 | 1 do 2 | _ do 7 end", 7));
   sv_test_run(t, sv_test_compiler_num("match 5 | y do y + 1 end", 6));
   sv_test_run(t, sv_test_compiler_num("match \"a\" | 1 do 2 | \"a\" do 3 end", 3));
   sv_test_run(t, sv_test_compiler_kind("match true | true do nil end", VALUE_NIL));

   sv_test_run(t, sv_test_compiler_num("match (1, 2) | (1, b) do b end", 2));
   sv_test_run(t, sv_test_compiler_num("match (1, 2) | (1, 3) do 8 | (1, 2) do 9 end", 9));
   sv_test_run(t, sv_test_compiler_runtime_err("match (1, 2, 3) | (1, b) do b end")
               == VM_ERR_NO_CLAUSE);

   sv_test_run(t, sv_test_compiler_num("match {a: 1, b: 4} | {a: 1, b: v} do v end", 4));
   sv_test_run(t, sv_test_compiler_runtime_err("match {a: 1} | {b: v} do v end")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_runtime_err("match {a: 1, b: 2} | {a: 1} do 5 end")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_num("match {a: 1, b: 2} | {a: 1, ..} do 5 end", 5));
   sv_test_run(t, sv_test_compiler_num("match %{\"k\": 3} | %{\"k\": v} do v end", 3));

   sv_test_run(t, sv_test_compiler_num("match [] | [] do 1 | [a] do a end", 1));
   sv_test_run(t, sv_test_compiler_num("match [7] | [] do 1 | [a] do a end", 7));
   sv_test_run(t, sv_test_compiler_runtime_err("match [7, 8] | [] do 1 | [a] do a end")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_num("match [1, 2] | [h, ..t] do h end", 1));
   sv_test_run(t, sv_test_compiler_num("match [1, 3] | [1, 2] do 8 | [1, 3] do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num("match [1, 2, 3] | [a, ..r] do match r | [b, ..s] do b end end", 2));

   /* An open map pattern requires only the keys it names, the way an open record
    * does; the exact form still pins the size. */
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1, \"b\": 2} | %{\"a\": v, ..} do v | _ do 0 end", 1));
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1} | %{\"a\": v, ..} do v | _ do 0 end", 1));
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1, \"b\": 2} | %{\"a\": v} do v | _ do 0 end", 0));
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"b\": 2} | %{\"a\": v, ..} do v | _ do 0 end", 0));
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1, \"b\": 2, \"c\": 3} | %{\"a\": p, \"b\": q, ..} do p + q | _ do 0 end", 3));

   /* Exact is tried before open, so the more specific clause wins. */
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1} | %{\"a\": v} do 1 | %{\"a\": w, ..} do 2 | _ do 0 end", 1));
   sv_test_run(t, sv_test_compiler_num(
      "match %{\"a\": 1, \"z\": 9} | %{\"a\": v} do 1 | %{\"a\": w, ..} do 2 | _ do 0 end", 2));

   /* Negative number literals. */
   sv_test_run(t, sv_test_compiler_num("match 0 - 1 | -1 do 7 | _ do 0 end", 7));
   sv_test_run(t, sv_test_compiler_num("match 1 | -1 do 7 | _ do 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("match 0 - 2 | -1 do 7 | _ do 0 end", 0));
   sv_test_run(t, sv_test_compiler_num("match 0 - 1.5 | -1.5 do 7 | _ do 0 end", 7));
   sv_test_run(t, sv_test_compiler_num("match 0 | -0 do 7 | _ do 0 end", 7));
   sv_test_run(t, sv_test_compiler_num("match (0 - 3, 4) | (-3, b) do b | _ do 0 end", 4));
   sv_test_run(t, sv_test_compiler_num("match [0 - 1, 2] | [-1, b] do b | _ do 0 end", 2));
   sv_test_run(t, sv_test_compiler_num("match {a: 0 - 1} | {a: -1} do 7 | _ do 0 end", 7));

   /* A pattern in the tail position: `..[]` pins an exact length, `..[b]` a one
    * element remainder, and they nest. */
   sv_test_run(t, sv_test_compiler_num("match [7] | [a, ..[]] do a | _ do 9 end", 7));
   sv_test_run(t, sv_test_compiler_num("match [7, 8] | [a, ..[]] do a | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num("match [7, 8] | [a, ..[b]] do b | _ do 9 end", 8));
   sv_test_run(t, sv_test_compiler_num("match [7] | [a, ..[b]] do b | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num(
      "match [7, 8, 9] | [a, ..[b, ..c]] do b | _ do 0 end", 8));

   /* The doc's first_neg_or_last, which needs the `..[]` tail. */
   sv_test_run(t, sv_test_compiler_num(
      "fun f(lst)\n"
      "| [x, ..xs] when x < 0 do x\n"
      "| [x, ..[]] do x\n"
      "| [x, ..xs] do f(xs)\n"
      "end\n"
      "f([1, -2, 3])", -2));
   sv_test_run(t, sv_test_compiler_num(
      "fun f(lst)\n"
      "| [x, ..xs] when x < 0 do x\n"
      "| [x, ..[]] do x\n"
      "| [x, ..xs] do f(xs)\n"
      "end\n"
      "f([1, 2, 3])", 3));

   /* Fail paths that unwind one and two list-uncons scopes. */
   sv_test_run(t, sv_test_compiler_num("match [1] | [1, 2] do 8 | _ do 5 end", 5));
   sv_test_run(t, sv_test_compiler_num("match [1, 9] | [1, 2] do 8 | [1, 3] do 9 | _ do 5 end", 5));
   sv_test_run(t, sv_test_compiler_num("match {a: 2, b: 3} | {a: 1, b: v} do v | _ do 6 end", 6));
   sv_test_run(t, sv_test_compiler_num("match [[1], 9] | [[1], 2] do 8 | _ do 4 end", 4));
}

static inline void sv_test_compiler_fun_clauses(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("fun f(x) | 0 do 10 | _ do 20 end f(0)", 10));
   sv_test_run(t, sv_test_compiler_num("fun f(x) | 0 do 10 | _ do 20 end f(3)", 20));
   /* A function whose clauses do not cover the argument raises, as Erlang does. */
   sv_test_run(t, sv_test_compiler_runtime_err("fun f(x) | 0 do 10 end\nf(3)")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_num("fun f(x) | y do y + 1 end f(4)", 5));

   /* A block expression, and a function body that is just an expression. */
   sv_test_run(t, sv_test_compiler_num("do 1; 2 end", 2));
   sv_test_run(t, sv_test_compiler_num("x = 1\ny = do x + 1 end\ny", 2));
   sv_test_run(t, sv_test_compiler_num("fun f(x) x + 2\nf(1)", 3));
   sv_test_run(t, sv_test_compiler_num("fun f(x) do y = 3; y + x end\nf(1)", 4));
   sv_test_run(t, sv_test_compiler_num("fun | a(x) x + 1 | b(y) y * 2 end\na(1) + b(3)", 8));

   /* Clause bodies are blocks. */
   sv_test_run(t, sv_test_compiler_num("fun f(x) | 0 do y = 3; y + 1 end f(0)", 4));
   sv_test_run(t, sv_test_compiler_num("match 0 | 0 do y = 3; y + 1 end", 4));

   /* Guards. A failing guard reaches the next clause, and compile_fail has to
    * pop everything the clause bound before jumping there. */
   sv_test_run(t, sv_test_compiler_num("match 1 | y when y > 0 do 10 | _ do 20 end", 10));
   sv_test_run(t, sv_test_compiler_num("match 0 | y when y > 0 do 10 | _ do 20 end", 20));
   sv_test_run(t, sv_test_compiler_runtime_err("match 0 | y when y > 0 do 10 end")
               == VM_ERR_NO_CLAUSE);

   /* Both rows share a literal, so the fallthrough is the empty rule's `|`. */
   sv_test_run(t, sv_test_compiler_num("g = true\nmatch 1 | 1 when g do 2 | 1 do 3 end", 2));
   sv_test_run(t, sv_test_compiler_num("g = false\nmatch 1 | 1 when g do 2 | 1 do 3 end", 3));

   sv_test_run(t, sv_test_compiler_num(
      "fun fib(x)\n"
      "| x when x <= 1 do 1\n"
      "| x do fib(x - 1) + fib(x - 2)\n"
      "end\n"
      "fib(10)", 89));

   /* A guard inside an uncons: its head and tail locals must be popped. */
   sv_test_run(t, sv_test_compiler_num(
      "fun first_neg(l)\n"
      "| [x, ..xs] when x < 0 do x\n"
      "| [_, ..xs] do first_neg(xs)\n"
      "| [] do 0\n"
      "end\n"
      "first_neg([1, 2, -3, 4])", -3));
   sv_test_run(t, sv_test_compiler_num(
      "fun first_neg(l)\n"
      "| [x, ..xs] when x < 0 do x\n"
      "| [_, ..xs] do first_neg(xs)\n"
      "| [] do 0\n"
      "end\n"
      "first_neg([1, 2, 3])", 0));

   sv_test_run(t, sv_test_compiler_num(
      "fun m(a, b) | (x, y) when x > y do x * 10 | (x, y) do y * 100 end m(2, 1)", 20));
   sv_test_run(t, sv_test_compiler_num(
      "fun m(a, b) | (x, y) when x > y do x * 10 | (x, y) do y * 100 end m(1, 2)", 200));

   /* The guard is a plain expression, so a block body works behind one. */
   sv_test_run(t, sv_test_compiler_num("match 1 | 1 when true do y = 3; y + 1 end", 4));

   /* A repeated variable constrains the positions to be equal. The non-adjacent
    * cases are the ones that matter: an implementation comparing only neighbouring
    * slots would pass (a, a) and fail every one of these. */
   sv_test_run(t, sv_test_compiler_num("match (1, 1) | (a, a) do a | _ do 99 end", 1));
   sv_test_run(t, sv_test_compiler_num("match (1, 2) | (a, a) do a | _ do 99 end", 99));
   sv_test_run(t, sv_test_compiler_num("match (1, 2, 1) | (a, b, a) do b | _ do 99 end", 2));
   sv_test_run(t, sv_test_compiler_num("match (1, 2, 3) | (a, b, a) do b | _ do 99 end", 99));
   sv_test_run(t, sv_test_compiler_num("match (1, 1, 1) | (a, a, a) do a | _ do 9 end", 1));
   sv_test_run(t, sv_test_compiler_num("match (1, 1, 2) | (a, a, a) do a | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num("match (1, 2, 1) | (a, a, a) do a | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num(
      "match (1, 2, 2, 1) | (a, b, b, a) do 1 | _ do 0 end", 1));
   sv_test_run(t, sv_test_compiler_num(
      "match (1, 2, 2, 3) | (a, b, b, a) do 1 | _ do 0 end", 0));
   sv_test_run(t, sv_test_compiler_num(
      "match (1, 2, 3, 1) | (a, b, b, a) do 1 | _ do 0 end", 0));

   /* A repeat spanning a nested container. */
   sv_test_run(t, sv_test_compiler_num("match (1, [2, 1]) | (a, [b, a]) do b | _ do 9 end", 2));
   sv_test_run(t, sv_test_compiler_num("match (1, [2, 3]) | (a, [b, a]) do b | _ do 9 end", 9));

   sv_test_run(t, sv_test_compiler_num("match [1, 1, 5] | [x, x, ..xs] do x | _ do 9 end", 1));
   sv_test_run(t, sv_test_compiler_num("match [1, 2, 5] | [x, x, ..xs] do x | _ do 9 end", 9));

   /* A tail variable repeating a head variable compares against the tail list,
    * so it holds only when the head equals that list. */
   sv_test_run(t, sv_test_compiler_num("match [[]] | [x, ..x] do 7 | _ do 9 end", 7));
   sv_test_run(t, sv_test_compiler_num("match [[], []] | [x, ..x] do 7 | _ do 9 end", 9));

   sv_test_run(t, sv_test_compiler_num("match {a: 1, b: 1} | {a: v, b: v} do v | _ do 9 end", 1));
   sv_test_run(t, sv_test_compiler_num("match {a: 1, b: 2} | {a: v, b: v} do v | _ do 9 end", 9));

   /* Duplicate field *names* are not variables, so they constrain nothing. */
   sv_test_run(t, sv_test_compiler_num("match {a: 1, b: 1} | {a: 1, b: 1} do 5 | _ do 9 end", 5));

   /* `==` is structural here, so a repeat compares deeply. */
   sv_test_run(t, sv_test_compiler_num("match ([1], [1]) | (a, a) do 1 | _ do 2 end", 1));

   /* Wildcards are exempt: `_` never constrains. */
   sv_test_run(t, sv_test_compiler_num("match (1, 2) | (_, _) do 7 end", 7));
   sv_test_run(t, sv_test_compiler_num("match (1, 2, 3) | (_, a, _) do a end", 2));

   /* A repeat composes with a guard, and either can fail independently. */
   sv_test_run(t, sv_test_compiler_num("match (2, 2) | (a, a) when a > 1 do 5 | _ do 9 end", 5));
   sv_test_run(t, sv_test_compiler_num("match (1, 1) | (a, a) when a > 1 do 5 | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num("match (2, 3) | (a, a) when a > 1 do 5 | _ do 9 end", 9));

   /* A guard fails to the nearest default: an inner match's guard lands on the
    * inner default, so the outer clause still succeeds with its value. */
   /* The inner match has no matching clause, so it raises rather than letting the
    * outer clause succeed with nil. */
   sv_test_run(t, sv_test_compiler_runtime_err(
      "match 1 | 1 do match 2 | 2 when false do 7 end | _ do 8 end")
               == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_num(
      "match 1 | 1 when false do match 2 | 2 do 3 end | _ do 9 end", 9));
   sv_test_run(t, sv_test_compiler_num(
      "match 1 | 1 when (match 2 | 2 do true end) do 5 | _ do 6 end", 5));
   sv_test_run(t, sv_test_compiler_num(
      "match (1, [2, 3]) | (a, [h, ..t]) when h == 2 do 4 | _ do 0 end", 4));

   sv_test_run(t, sv_test_compiler_num(
      "fun swap(a, b) | (1, y) do y | (x, 2) do x end swap(1, 9)", 9));
   sv_test_run(t, sv_test_compiler_num(
      "fun swap(a, b) | (1, y) do y | (x, 2) do x end swap(7, 2)", 7));
   sv_test_run(t, sv_test_compiler_num("fun pair(a, b) | t do 5 end pair(1, 2)", 5));

   sv_test_run(t, sv_test_compiler_num(
      "fun len(l)\n"
      "| [] do 0\n"
      "| [_, ..r] do 1 + len(r)\n"
      "end\n"
      "len([4, 5, 6])", 3));

   /* The doc's three clause example, with all three arms reachable. */
   sv_test_run(t, sv_test_compiler_num(
      "fun fn(f, l1, l2)\n"
      "| (f, [], ys) do 1\n"
      "| (f, xs, []) do 2\n"
      "| (f, [x, ..xs], [y, ..ys]) do 3\n"
      "end\n"
      "fn(0, [], [1]) + fn(0, [1], []) * 10 + fn(0, [1], [2]) * 100", 321));

   bool ok = false;
   value_t v = sv_test_compiler_eval(
      "fun\n"
      "| is_even(x)\n"
      "| 0 do true\n"
      "| _ do is_odd(x - 1)\n"
      "| is_odd(x)\n"
      "| 0 do false\n"
      "| _ do is_even(x - 1)\n"
      "end\n"
      "is_even(10)", &ok);
   sv_test_run(t, ok);
   sv_test_run(t, v.kind == VALUE_BOOL && v.boolean);
   value_free(&v, &sv_gpa);
}

static inline void sv_test_compiler_destructure(sv_testing_t* t)
{
   sv_test_run(t, sv_test_compiler_num("(a, b) = (1, 2)\na + b", 3));
   sv_test_run(t, sv_test_compiler_num("(a, [b, c]) = (1, [2, 3])\na + b + c", 6));
   sv_test_run(t, sv_test_compiler_num("[h, ..t] = [1, 2, 3]\nh", 1));
   sv_test_run(t, sv_test_compiler_num("[h, ..t] = [1, 2, 3]\nt[1]", 3));
   sv_test_run(t, sv_test_compiler_num("[a, b] = [1, 2]\na + b", 3));
   sv_test_run(t, sv_test_compiler_num("{x: a} = {x: 5}\na", 5));
   sv_test_run(t, sv_test_compiler_num("{x: a, ..} = {x: 5, y: 6}\na", 5));
   sv_test_run(t, sv_test_compiler_num("%{\"k\": v} = %{\"k\": 7}\nv", 7));

   sv_test_run(t, sv_test_compiler_num("%{\"a\": v, ..} = %{\"a\": 5, \"b\": 6}\nv", 5));
   sv_test_run(t, sv_test_compiler_runtime_err("%{\"a\": v} = %{\"a\": 5, \"b\": 6}\nv")
               == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_num("-1 = 0 - 1\n42", 42));
   sv_test_run(t, sv_test_compiler_runtime_err("-1 = 5\n42") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_num("(-1, b) = (0 - 1, 9)\nb", 9));

   /* A pattern tail works on the left of `=` too. */
   sv_test_run(t, sv_test_compiler_num("[a, ..[]] = [7]\na", 7));
   sv_test_run(t, sv_test_compiler_num("[a, ..[b]] = [7, 8]\nb", 8));
   sv_test_run(t, sv_test_compiler_runtime_err("[a, ..[]] = [7, 8]\na")
               == VM_ERR_MATCH_FAILED);

   /* Wildcards bind nothing, and a literal is a pure assertion. */
   sv_test_run(t, sv_test_compiler_num("(_, b) = (1, 2)\nb", 2));
   sv_test_run(t, sv_test_compiler_num("[h, .._] = [1, 2, 3]\nh", 1));
   sv_test_run(t, sv_test_compiler_num("_ = 5\n42", 42));
   sv_test_run(t, sv_test_compiler_num("1 = 1\n42", 42));

   /* The assignment is still an expression, evaluating to the right hand side. */
   sv_test_run(t, sv_test_compiler_num("y = ((a, b) = (1, 2))\nb", 2));

   /* A repeat constrains, exactly as it does in a match clause. */
   sv_test_run(t, sv_test_compiler_num("(a, a) = (1, 1)\na", 1));
   sv_test_run(t, sv_test_compiler_num("(a, b, a) = (1, 2, 1)\nb", 2));

   /* Scope: locals inside a function, globals at the top level. */
   sv_test_run(t, sv_test_compiler_num("fun f(p) do\n(a, b) = p\na + b\nend\nf((3, 4))", 7));
   sv_test_run(t, sv_test_compiler_num("z = do\n(a, b) = (10, 20)\na + b\nend\nz", 30));

   /* Every mismatch raises rather than yielding nil. */
   sv_test_run(t, sv_test_compiler_runtime_err("(a, b) = (1, 2, 3)\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("(a, b) = 5\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("[a, b] = [1, 2, 3]\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("[a, b] = [1]\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("{x: a} = {x: 5, y: 6}\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("{z: a, ..} = {x: 5}\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("%{\"z\": v} = %{\"k\": 7}\nv")
               == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("1 = 2\n42") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("(a, a) = (1, 2)\na") == VM_ERR_MATCH_FAILED);
   sv_test_run(t, sv_test_compiler_runtime_err("(a, b, a) = (1, 2, 3)\nb")
               == VM_ERR_MATCH_FAILED);

   /* `..` is pattern and LHS syntax only, in all three container kinds. */
   sv_test_run(t, sv_test_compiler_err("y = 1\nx = [1, ..y]\nx") == C_ERR_UNEXPECTED_SEXPR);
   sv_test_run(t, sv_test_compiler_err("x = {a: 1, ..}\nx") == C_ERR_UNEXPECTED_SEXPR);
   sv_test_run(t, sv_test_compiler_err("x = %{\"a\": 1, ..}\nx") == C_ERR_UNEXPECTED_SEXPR);

   /* A match with no matching clause raises rather than yielding nil. */
   sv_test_run(t, sv_test_compiler_runtime_err("match 5 | 1 do 2 end") == VM_ERR_NO_CLAUSE);
   sv_test_run(t, sv_test_compiler_runtime_err("match 5 end") == VM_ERR_NO_CLAUSE);
}

static inline void sv_test_compiler(sv_testing_t* t)
{
   sv_test_compiler_basics(t);
   sv_test_compiler_comparisons(t);
   sv_test_compiler_maps(t);
   sv_test_compiler_indexing(t);
   sv_test_compiler_tuples(t);
   sv_test_compiler_tuple_type(t);
   sv_test_compiler_logic(t);
   sv_test_compiler_for(t);
   sv_test_compiler_functions(t);
   sv_test_compiler_closures(t);
   sv_test_compiler_recursion(t);
   sv_test_compiler_groups(t);
   sv_test_compiler_match(t);
   sv_test_compiler_fun_clauses(t);
   sv_test_compiler_destructure(t);
   sv_test_compiler_errors(t);
   sv_test_compiler_runtime_errors(t);
}

#endif
