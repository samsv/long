#ifndef SV_TESTS_VM_H
#define SV_TESTS_VM_H

#include <math.h>
#include "../src/std/test.h"
#include "../src/std/allocator_std.h"
#include "../src/value.h"
#include "../src/vm.h"
#include "string.h"

static int sv_test_vm_obj_frees = 0;

static inline void sv_test_vm_free_obj(obj_t* o, const sv_allocator_t* a)
{
   sv_str_deinit(&o->str, a);
   sv_test_vm_obj_frees++;
}

static inline value_t sv_test_vm_num(double n)
{
   return (value_t){ .kind = VALUE_NUMBER, .number = n };
}

static inline value_t sv_test_vm_bool(bool b)
{
   return (value_t){ .kind = VALUE_BOOL, .boolean = b };
}

static inline void sv_test_vm_emit(vm_t* vm, uint8_t byte, int64_t line)
{
   int success;
   sv_vec_push(&vm->chunk.bytecode, byte, &success, &sv_gpa);
   sv_vec_push(&vm->chunk.lines, line, &success, &sv_gpa);
}

static inline uint8_t sv_test_vm_const(vm_t* vm, value_t v)
{
   int success;
   sv_vec_push(&vm->chunk.constants, v, &success, &sv_gpa);
   return (uint8_t)(vm->chunk.constants.size - 1);
}

static inline void sv_test_vm_load(vm_t* vm, value_t v, int64_t line)
{
   uint8_t i = sv_test_vm_const(vm, v);
   sv_test_vm_emit(vm, OP_LOAD_CONSTANT, line);
   sv_test_vm_emit(vm, i, line);
}

static inline double sv_test_vm_math(uint8_t op, double n1, double n2)
{
   vm_t vm = vm_init(sv_str_init("math"));
   sv_test_vm_load(&vm, sv_test_vm_num(n1), 1);
   sv_test_vm_load(&vm, sv_test_vm_num(n2), 1);
   sv_test_vm_emit(&vm, op, 1);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   double res = !err.is_some && vm.stack.size == 1 ? sv_vec_last(vm.stack).number : -9999;
   vm_deinit(&vm, &sv_gpa);
   return res;
}

static inline void sv_test_vm_math_ops(sv_testing_t* t)
{
   sv_test_run(t, sv_test_vm_math(OP_ADD, 6, 3) == 9);
   sv_test_run(t, sv_test_vm_math(OP_SUB, 6, 3) == 3);
   sv_test_run(t, sv_test_vm_math(OP_MUL, 6, 3) == 18);
   sv_test_run(t, sv_test_vm_math(OP_DIV, 6, 3) == 2);
   sv_test_run(t, sv_test_vm_math(OP_DIV, 1, 0) == (double)INFINITY);
}

static inline void sv_test_vm_math_errors(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("math_errors"));
   sv_test_vm_load(&vm, sv_test_vm_bool(true), 41);
   sv_test_vm_load(&vm, sv_test_vm_num(2), 41);
   sv_test_vm_emit(&vm, OP_ADD, 42);

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, err.is_some);
   sv_test_run(t, err.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_op_err* payload = err.value.payload;
   sv_test_run(t, payload->line == 42);
   sv_test_run(t, payload->ops_len == 2);
   sv_test_run(t, payload->ops[0].kind == VALUE_BOOL);
   sv_test_run(t, payload->ops[1].kind == VALUE_NUMBER);
   sv_test_run(t, payload->ops[1].number == 2);
   vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);
}

static inline void sv_test_vm_globals_locals(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("globals"));
   sv_test_vm_load(&vm, sv_test_vm_num(7), 1);
   sv_test_vm_emit(&vm, OP_SET_GLOBAL, 2);
   sv_test_vm_emit(&vm, OP_GET_GLOBAL, 3);
   sv_test_vm_emit(&vm, 0, 3);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.globals.size == 1);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, sv_vec_last(vm.stack).number == 7);
   vm_deinit(&vm, &sv_gpa);

   vm_t lvm = vm_init(sv_str_init("locals"));
   sv_test_vm_load(&lvm, sv_test_vm_num(1), 1);
   sv_test_vm_emit(&lvm, OP_SET_LOCAL, 1);
   sv_test_vm_load(&lvm, sv_test_vm_num(2), 2);
   sv_test_vm_emit(&lvm, OP_SET_LOCAL, 2);
   sv_test_vm_emit(&lvm, OP_GET_LOCAL, 3);
   sv_test_vm_emit(&lvm, 0, 3);
   sv_test_vm_emit(&lvm, OP_POP, 4);
   sv_test_vm_emit(&lvm, OP_POP_LOCAL, 5);
   sv_test_vm_emit(&lvm, 1, 5);
   sv_opt_t(error_t) lerr = vm_run(&lvm, &sv_gpa);
   sv_test_run(t, !lerr.is_some);
   sv_test_run(t, lvm.locals.size == 1);
   sv_test_run(t, lvm.stack.size == 0);
   sv_test_run(t, sv_vec_last(lvm.locals).number == 1);
   vm_deinit(&lvm, &sv_gpa);
}

static inline bool sv_test_vm_eq(value_t v1, value_t v2)
{
   vm_t vm = vm_init(sv_str_init("equals"));
   sv_test_vm_load(&vm, v1, 1);
   sv_test_vm_load(&vm, v2, 1);
   sv_test_vm_emit(&vm, OP_EQUALS, 1);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   bool res = !err.is_some && vm.stack.size == 1
      && sv_vec_last(vm.stack).kind == VALUE_BOOL
      && sv_vec_last(vm.stack).boolean;
   vm_deinit(&vm, &sv_gpa);
   return res;
}

static inline void sv_test_vm_equals(sv_testing_t* t)
{
   sv_test_run(t, sv_test_vm_eq(sv_test_vm_num(2), sv_test_vm_num(2)));
   sv_test_run(t, !sv_test_vm_eq(sv_test_vm_num(2), sv_test_vm_num(3)));
   sv_test_run(t, !sv_test_vm_eq(sv_test_vm_num(2), sv_test_vm_bool(true)));
}

static inline void sv_test_vm_jumps(sv_testing_t* t)
{
   vm_t f = vm_init(sv_str_init("jif_false"));
   sv_test_vm_load(&f, sv_test_vm_bool(false), 1);
   sv_test_vm_emit(&f, OP_JUMP_IF_FALSE, 2);
   sv_test_vm_emit(&f, 4, 2);
   sv_test_vm_emit(&f, 0, 2);
   sv_test_vm_load(&f, sv_test_vm_num(111), 3);
   sv_test_vm_load(&f, sv_test_vm_num(222), 4);
   sv_opt_t(error_t) ferr = vm_run(&f, &sv_gpa);
   sv_test_run(t, !ferr.is_some);
   sv_test_run(t, f.stack.size == 1);
   sv_test_run(t, sv_vec_last(f.stack).number == 222);
   vm_deinit(&f, &sv_gpa);

   vm_t tr = vm_init(sv_str_init("jif_true"));
   sv_test_vm_load(&tr, sv_test_vm_bool(true), 1);
   sv_test_vm_emit(&tr, OP_JUMP_IF_FALSE, 2);
   sv_test_vm_emit(&tr, 4, 2);
   sv_test_vm_emit(&tr, 0, 2);
   sv_test_vm_load(&tr, sv_test_vm_num(111), 3);
   sv_test_vm_load(&tr, sv_test_vm_num(222), 4);
   sv_opt_t(error_t) terr = vm_run(&tr, &sv_gpa);
   sv_test_run(t, !terr.is_some);
   sv_test_run(t, tr.stack.size == 2);
   sv_test_run(t, tr.stack.arr[0].number == 111);
   vm_deinit(&tr, &sv_gpa);

   vm_t j = vm_init(sv_str_init("jump_back"));
   sv_test_vm_emit(&j, OP_JUMP, 1);
   sv_test_vm_emit(&j, 7, 1);
   sv_test_vm_emit(&j, 0, 1);
   sv_test_vm_load(&j, sv_test_vm_num(7), 2);
   sv_test_vm_emit(&j, OP_JUMP, 3);
   sv_test_vm_emit(&j, 5, 3);
   sv_test_vm_emit(&j, 0, 3);
   sv_test_vm_emit(&j, OP_JUMP_BACK, 4);
   sv_test_vm_emit(&j, 6, 4);
   sv_test_vm_emit(&j, 0, 4);
   sv_opt_t(error_t) jerr = vm_run(&j, &sv_gpa);
   sv_test_run(t, !jerr.is_some);
   sv_test_run(t, j.stack.size == 1);
   sv_test_run(t, sv_vec_last(j.stack).number == 7);
   vm_deinit(&j, &sv_gpa);
}

static inline void sv_test_vm_values(sv_testing_t* t)
{
   sv_test_vm_obj_frees = 0;

   obj_t o = { .kind = OBJ_STR, .str = sv_str_copy(sv_str_init("shared"), &sv_gpa) };
   sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, sv_test_vm_free_obj, &sv_gpa);
   value_t v = { .kind = VALUE_OBJ, .obj = rc };

   vm_t vm = vm_init(sv_str_init("values"));
   uint8_t i = sv_test_vm_const(&vm, value_borrow(v));
   sv_test_vm_emit(&vm, OP_LOAD_CONSTANT, 1);
   sv_test_vm_emit(&vm, i, 1);
   sv_test_vm_emit(&vm, OP_LOAD_CONSTANT, 2);
   sv_test_vm_emit(&vm, i, 2);
   sv_test_vm_emit(&vm, OP_SET_GLOBAL, 3);

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, vm.globals.size == 1);
   sv_test_run(t, v.obj.cell->count == 4);
   vm_deinit(&vm, &sv_gpa);

   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_vm_obj_frees == 0);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_vm_obj_frees == 1);
}

static inline void sv_test_vm_errors(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("oom"));
   sv_test_vm_load(&vm, sv_test_vm_num(1), 1);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_test_fail_alloc);
   sv_test_run(t, err.is_some);
   sv_test_run(t, err.value.error_code == VM_ERR_OOM);
   vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);

   vm_t nvm = vm_init(sv_str_init("not_implemented"));
   sv_test_vm_emit(&nvm, 200, 13);
   sv_opt_t(error_t) nerr = vm_run(&nvm, &sv_gpa);
   sv_test_run(t, nerr.is_some);
   sv_test_run(t, nerr.value.error_code == VM_ERR_NOT_IMPLEMENTED);
   vm_instruction_err* npayload = nerr.value.payload;
   sv_test_run(t, npayload->line == 13);
   sv_test_run(t, npayload->instruction == 200);
   vm_err_deinit(&nerr.value, &sv_gpa);
   vm_deinit(&nvm, &sv_gpa);
}

static inline void sv_test_vm_negate(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("negate"));
   sv_test_vm_load(&vm, sv_test_vm_num(5), 1);
   sv_test_vm_emit(&vm, OP_NEGATE, 1);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, sv_vec_last(vm.stack).number == -5);
   vm_deinit(&vm, &sv_gpa);

   vm_t bvm = vm_init(sv_str_init("negate_bool"));
   sv_test_vm_load(&bvm, sv_test_vm_bool(true), 7);
   sv_test_vm_emit(&bvm, OP_NEGATE, 7);
   sv_opt_t(error_t) berr = vm_run(&bvm, &sv_gpa);
   sv_test_run(t, berr.is_some);
   sv_test_run(t, berr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_op_err* payload = berr.value.payload;
   sv_test_run(t, payload->line == 7);
   sv_test_run(t, payload->ops_len == 1);
   sv_test_run(t, payload->ops[0].kind == VALUE_BOOL);
   vm_err_deinit(&berr.value, &sv_gpa);
   vm_deinit(&bvm, &sv_gpa);
}

static inline void sv_test_vm_list(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("list"));
   sv_test_vm_load(&vm, sv_test_vm_num(1), 1);
   sv_test_vm_load(&vm, sv_test_vm_num(2), 1);
   sv_test_vm_load(&vm, sv_test_vm_num(3), 1);
   sv_test_vm_emit(&vm, OP_LIST, 2);
   sv_test_vm_emit(&vm, 3, 2);

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   value_t lv = sv_vec_last(vm.stack);
   sv_test_run(t, lv.kind == VALUE_OBJ);
   sv_test_run(t, lv.obj.cell->value.kind == OBJ_LIST);
   list_t l = lv.obj.cell->value.list;
   sv_test_run(t, ll_count(l) == 3);
   sv_test_run(t, sv_opt_unwrap(ll_get(l, 0)).number == 1);
   sv_test_run(t, sv_opt_unwrap(ll_get(l, 1)).number == 2);
   sv_test_run(t, sv_opt_unwrap(ll_get(l, 2)).number == 3);
   vm_deinit(&vm, &sv_gpa);
}

static inline void sv_test_vm_iter(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("iter"));
   sv_test_vm_load(&vm, sv_test_vm_num(1), 1);
   sv_test_vm_load(&vm, sv_test_vm_num(2), 1);
   sv_test_vm_load(&vm, sv_test_vm_num(3), 1);
   sv_test_vm_emit(&vm, OP_LIST, 1);
   sv_test_vm_emit(&vm, 3, 1);
   sv_test_vm_emit(&vm, OP_ITER_CREATE, 2);
   sv_test_vm_emit(&vm, OP_SET_LOCAL, 2);
   for (int64_t i = 0; i < 4; i++) {
      sv_test_vm_emit(&vm, OP_GET_LOCAL, 3);
      sv_test_vm_emit(&vm, 0, 3);
      sv_test_vm_emit(&vm, OP_ITER_NEXT, 3);
   }

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 4);
   sv_test_run(t, vm.stack.arr[0].number == 1);
   sv_test_run(t, vm.stack.arr[1].number == 2);
   sv_test_run(t, vm.stack.arr[2].number == 3);
   sv_test_run(t, vm.stack.arr[3].kind == VALUE_NIL);
   vm_deinit(&vm, &sv_gpa);

   vm_t cvm = vm_init(sv_str_init("iter_create_err"));
   sv_test_vm_load(&cvm, sv_test_vm_num(4), 5);
   sv_test_vm_emit(&cvm, OP_ITER_CREATE, 5);
   sv_opt_t(error_t) cerr = vm_run(&cvm, &sv_gpa);
   sv_test_run(t, cerr.is_some);
   sv_test_run(t, cerr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, ((vm_op_err*)cerr.value.payload)->ops_len == 1);
   vm_err_deinit(&cerr.value, &sv_gpa);
   vm_deinit(&cvm, &sv_gpa);

   vm_t nvm = vm_init(sv_str_init("iter_next_err"));
   sv_test_vm_load(&nvm, sv_test_vm_num(4), 6);
   sv_test_vm_emit(&nvm, OP_ITER_NEXT, 6);
   sv_opt_t(error_t) nerr = vm_run(&nvm, &sv_gpa);
   sv_test_run(t, nerr.is_some);
   sv_test_run(t, nerr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, ((vm_op_err*)nerr.value.payload)->ops_len == 1);
   vm_err_deinit(&nerr.value, &sv_gpa);
   vm_deinit(&nvm, &sv_gpa);
}

static inline void sv_test_vm_list_values(sv_testing_t* t)
{
   sv_test_vm_obj_frees = 0;

   obj_t o = { .kind = OBJ_STR, .str = sv_str_copy(sv_str_init("boxed"), &sv_gpa) };
   sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, sv_test_vm_free_obj, &sv_gpa);
   value_t v = { .kind = VALUE_OBJ, .obj = rc };

   vm_t vm = vm_init(sv_str_init("list_values"));
   uint8_t i = sv_test_vm_const(&vm, value_borrow(v));
   sv_test_vm_emit(&vm, OP_LOAD_CONSTANT, 1);
   sv_test_vm_emit(&vm, i, 1);
   sv_test_vm_emit(&vm, OP_LIST, 2);
   sv_test_vm_emit(&vm, 1, 2);
   sv_test_vm_emit(&vm, OP_SET_GLOBAL, 3);

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.globals.size == 1);
   vm_deinit(&vm, &sv_gpa);

   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_vm_obj_frees == 0);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_vm_obj_frees == 1);
}

static inline void sv_test_vm_list_oom(sv_testing_t* t)
{
   vm_t ovm = vm_init(sv_str_init("list_oom"));
   int success;
   sv_vec_push(&ovm.stack, sv_test_vm_num(1), &success, &sv_gpa);
   sv_test_vm_emit(&ovm, OP_LIST, 3);
   sv_test_vm_emit(&ovm, 1, 3);
   sv_opt_t(error_t) oerr = vm_run(&ovm, &sv_test_fail_alloc);
   sv_test_run(t, oerr.is_some);
   sv_test_run(t, oerr.value.error_code == VM_ERR_OOM);
   sv_test_run(t, ovm.stack.size == 1);
   vm_err_deinit(&oerr.value, &sv_gpa);
   vm_deinit(&ovm, &sv_gpa);
}

static inline void sv_test_vm_builder(sv_testing_t* t)
{
   vm_builder_t b = vmb_init(sv_str_init("built"));
   sv_opt_t(uint8_t) c0 = vmb_add_constant(&b, sv_test_vm_num(6), &sv_gpa);
   sv_opt_t(uint8_t) c1 = vmb_add_constant(&b, sv_test_vm_num(3), &sv_gpa);
   sv_test_run(t, c0.is_some && c0.value == 0);
   sv_test_run(t, c1.is_some && c1.value == 1);
   sv_test_run(t, vmb_add_byte(&b, OP_ADD, 7, &sv_gpa));

   vm_t vm = vmb_build(b);
   sv_test_run(t, vm.chunk.bytecode.size == 5);
   sv_test_run(t, vm.chunk.lines.size == 5);
   sv_test_run(t, vm.chunk.lines.arr[0] == 0);
   sv_test_run(t, vm.chunk.lines.arr[4] == 7);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, sv_vec_last(vm.stack).number == 9);
   vm_deinit(&vm, &sv_gpa);

   vm_builder_t jb = vmb_init(sv_str_init("built_jif"));
   vmb_add_constant(&jb, sv_test_vm_bool(false), &sv_gpa);
   sv_opt_t(int64_t) patch = vmb_add_jump_if_false(&jb, 1, &sv_gpa);
   sv_test_run(t, patch.is_some);
   sv_test_run(t, jb.vm.chunk.bytecode.arr[patch.value] == 255);
   sv_test_run(t, jb.vm.chunk.bytecode.arr[patch.value + 1] == 255);
   vmb_add_constant(&jb, sv_test_vm_num(111), &sv_gpa);
   vmb_patch_jump(&jb, patch.value, (uint16_t)(jb.vm.chunk.bytecode.size - patch.value));
   vmb_add_constant(&jb, sv_test_vm_num(222), &sv_gpa);

   vm_t jvm = vmb_build(jb);
   sv_opt_t(error_t) jerr = vm_run(&jvm, &sv_gpa);
   sv_test_run(t, !jerr.is_some);
   sv_test_run(t, jvm.stack.size == 1);
   sv_test_run(t, sv_vec_last(jvm.stack).number == 222);
   vm_deinit(&jvm, &sv_gpa);

   vm_builder_t lb = vmb_init(sv_str_init("built_loop"));
   sv_opt_t(int64_t) fwd = vmb_add_jump(&lb, 1, &sv_gpa);
   sv_test_run(t, fwd.is_some);
   int64_t load_at = lb.vm.chunk.bytecode.size;
   vmb_add_constant(&lb, sv_test_vm_num(7), &sv_gpa);
   sv_opt_t(int64_t) out = vmb_add_jump(&lb, 2, &sv_gpa);
   sv_test_run(t, out.is_some);
   vmb_patch_jump(&lb, fwd.value, (uint16_t)(lb.vm.chunk.bytecode.size - fwd.value));
   sv_test_run(t, vmb_add_jump_back(&lb, load_at, 3, &sv_gpa));
   vmb_patch_jump(&lb, out.value, (uint16_t)(lb.vm.chunk.bytecode.size - out.value));

   vm_t lvm = vmb_build(lb);
   sv_opt_t(error_t) lerr = vm_run(&lvm, &sv_gpa);
   sv_test_run(t, !lerr.is_some);
   sv_test_run(t, lvm.stack.size == 1);
   sv_test_run(t, sv_vec_last(lvm.stack).number == 7);
   vm_deinit(&lvm, &sv_gpa);

   vm_builder_t cb = vmb_init(sv_str_init("built_closure"));
   vm_builder_t fb = vmb_init(sv_str_init("fn"));
   vmb_add_constant(&fb, sv_test_vm_num(1), &sv_gpa);
   sv_opt_t(uint8_t) fi = vmb_add_closure(&cb, 2, vmb_build(fb), &sv_gpa);
   sv_test_run(t, fi.is_some && fi.value == 0);
   sv_test_run(t, cb.vm.chunk.functions.size == 1);
   sv_test_run(t, cb.vm.chunk.bytecode.size == 3);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[0] == OP_LOAD_CLOSURE);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[1] == 0);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[2] == 2);
   vm_t cvm = vmb_build(cb);
   vm_deinit(&cvm, &sv_gpa);

   vm_builder_t ob = vmb_init(sv_str_init("built_oom"));
   sv_opt_t(uint8_t) oc = vmb_add_constant(&ob, sv_test_vm_num(1), &sv_test_fail_alloc);
   sv_test_run(t, !oc.is_some);
   vm_deinit(&ob.vm, &sv_gpa);
}

static inline void sv_test_vm_call(sv_testing_t* t)
{
   vm_builder_t fb = vmb_init(sv_str_init("add_fn"));
   vmb_add_bytes(&fb, OP_GET_GLOBAL, 0, 1, &sv_gpa);
   vmb_add_bytes(&fb, OP_GET_GLOBAL, 1, 1, &sv_gpa);
   vmb_add_byte(&fb, OP_ADD, 1, &sv_gpa);

   vm_builder_t b = vmb_init(sv_str_init("caller"));
   vmb_add_constant(&b, sv_test_vm_num(6), &sv_gpa);
   vmb_add_constant(&b, sv_test_vm_num(3), &sv_gpa);
   vmb_add_closure(&b, 0, vmb_build(fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 2, 2, &sv_gpa);

   vm_t vm = vmb_build(b);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, sv_vec_last(vm.stack).number == 9);
   vm_deinit(&vm, &sv_gpa);
}

static inline void sv_test_vm_call_upvalues(sv_testing_t* t)
{
   vm_builder_t fb = vmb_init(sv_str_init("up_fn"));
   vmb_add_bytes(&fb, OP_GET_UPVALUE, 0, 1, &sv_gpa);

   vm_builder_t b = vmb_init(sv_str_init("caller"));
   vmb_add_constant(&b, sv_test_vm_num(100), &sv_gpa);
   vmb_add_closure(&b, 1, vmb_build(fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 0, 2, &sv_gpa);

   vm_t vm = vmb_build(b);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, sv_vec_last(vm.stack).number == 100);
   vm_deinit(&vm, &sv_gpa);
}

static inline void sv_test_vm_call_errors(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("not_callable"));
   sv_test_vm_load(&vm, sv_test_vm_num(5), 1);
   sv_test_vm_emit(&vm, OP_CALL, 2);
   sv_test_vm_emit(&vm, 0, 2);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, err.is_some);
   sv_test_run(t, err.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   sv_test_run(t, ((vm_op_err*)err.value.payload)->ops_len == 1);
   vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);

   vm_builder_t fb = vmb_init(sv_str_init("bad_fn"));
   vmb_add_constant(&fb, sv_test_vm_bool(true), &sv_gpa);
   vmb_add_constant(&fb, sv_test_vm_num(1), &sv_gpa);
   vmb_add_byte(&fb, OP_ADD, 77, &sv_gpa);

   vm_builder_t b = vmb_init(sv_str_init("caller"));
   vmb_add_closure(&b, 0, vmb_build(fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 0, 2, &sv_gpa);

   vm_t pvm = vmb_build(b);
   sv_opt_t(error_t) perr = vm_run(&pvm, &sv_gpa);
   sv_test_run(t, perr.is_some);
   sv_test_run(t, perr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_op_err* payload = perr.value.payload;
   sv_test_run(t, payload->line == 77);
   sv_test_run(t, payload->ops_len == 2);
   vm_err_deinit(&perr.value, &sv_gpa);
   vm_deinit(&pvm, &sv_gpa);
}

static inline void sv_test_vm_group(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("group"));
   int success;

   vm_builder_t f0 = vmb_init(sv_str_init("member0"));
   vmb_add_bytes(&f0, OP_GET_UPVALUE, 0, 1, &sv_gpa);
   sv_vec_push(&vm.chunk.functions, vmb_build(f0), &success, &sv_gpa);

   vm_builder_t f1 = vmb_init(sv_str_init("member1"));
   vmb_add_bytes(&f1, OP_GET_MEMBER, 0, 1, &sv_gpa);
   sv_vec_push(&vm.chunk.functions, vmb_build(f1), &success, &sv_gpa);

   sv_test_vm_load(&vm, sv_test_vm_num(42), 2);
   sv_test_vm_emit(&vm, OP_CREATE_GROUP, 3);
   sv_test_vm_emit(&vm, 0, 3);
   sv_test_vm_emit(&vm, 2, 3);
   sv_test_vm_emit(&vm, 1, 3);
   sv_test_vm_emit(&vm, OP_CALL, 4);
   sv_test_vm_emit(&vm, 0, 4);

   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, vm.stack.size == 2);
   sv_test_run(t, sv_vec_last(vm.stack).number == 42);
   sv_test_run(t, vm.stack.arr[0].kind == VALUE_OBJ);
   sv_test_run(t, vm.stack.arr[0].obj.cell->value.kind == OBJ_CLOSURE_MEMBER);
   sv_test_vm_emit(&vm, OP_POP, 5);
   sv_test_vm_emit(&vm, OP_CALL, 6);
   sv_test_vm_emit(&vm, 0, 6);
   sv_opt_t(error_t) merr = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !merr.is_some);
   sv_test_run(t, vm.stack.size == 1);
   sv_test_run(t, sv_vec_last(vm.stack).kind == VALUE_OBJ);
   sv_test_run(t, sv_vec_last(vm.stack).obj.cell->value.kind == OBJ_CLOSURE_MEMBER);
   vm_deinit(&vm, &sv_gpa);

   vm_t nvm = vm_init(sv_str_init("no_group"));
   sv_test_vm_emit(&nvm, OP_GET_MEMBER, 9);
   sv_test_vm_emit(&nvm, 0, 9);
   sv_opt_t(error_t) nerr = vm_run(&nvm, &sv_gpa);
   sv_test_run(t, nerr.is_some);
   sv_test_run(t, nerr.value.error_code == VM_ERR_NO_GROUP);
   vm_instruction_err* npayload = nerr.value.payload;
   sv_test_run(t, npayload->line == 9);
   sv_test_run(t, npayload->instruction == OP_GET_MEMBER);
   vm_err_deinit(&nerr.value, &sv_gpa);
   vm_deinit(&nvm, &sv_gpa);
}

static inline void sv_test_vm_closure_values(sv_testing_t* t)
{
   sv_test_vm_obj_frees = 0;

   obj_t o = { .kind = OBJ_STR, .str = sv_str_copy(sv_str_init("captured"), &sv_gpa) };
   sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, sv_test_vm_free_obj, &sv_gpa);
   value_t v = { .kind = VALUE_OBJ, .obj = rc };

   vm_builder_t fb = vmb_init(sv_str_init("up_fn"));
   vmb_add_bytes(&fb, OP_GET_UPVALUE, 0, 1, &sv_gpa);

   vm_builder_t b = vmb_init(sv_str_init("caller"));
   vmb_add_constant(&b, value_borrow(v), &sv_gpa);
   vmb_add_closure(&b, 1, vmb_build(fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 0, 2, &sv_gpa);

   vm_t vm = vmb_build(b);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, !err.is_some);
   sv_test_run(t, sv_vec_last(vm.stack).obj.cell == v.obj.cell);
   vm_deinit(&vm, &sv_gpa);

   sv_test_run(t, v.obj.cell->count == 1);
   sv_test_run(t, sv_test_vm_obj_frees == 0);
   sv_rc_deinit(&v.obj, &sv_gpa);
   sv_test_run(t, sv_test_vm_obj_frees == 1);
}

static inline void sv_test_vm(sv_testing_t* t)
{
   sv_test_vm_math_ops(t);
   sv_test_vm_math_errors(t);
   sv_test_vm_globals_locals(t);
   sv_test_vm_equals(t);
   sv_test_vm_jumps(t);
   sv_test_vm_values(t);
   sv_test_vm_errors(t);
   sv_test_vm_negate(t);
   sv_test_vm_list(t);
   sv_test_vm_iter(t);
   sv_test_vm_list_values(t);
   sv_test_vm_list_oom(t);
   sv_test_vm_builder(t);
   sv_test_vm_call(t);
   sv_test_vm_call_upvalues(t);
   sv_test_vm_call_errors(t);
   sv_test_vm_group(t);
   sv_test_vm_closure_values(t);
}

#endif
