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

   vm_t vm = vmb_build(&b);
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

   vm_t jvm = vmb_build(&jb);
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

   vm_t lvm = vmb_build(&lb);
   sv_opt_t(error_t) lerr = vm_run(&lvm, &sv_gpa);
   sv_test_run(t, !lerr.is_some);
   sv_test_run(t, lvm.stack.size == 1);
   sv_test_run(t, sv_vec_last(lvm.stack).number == 7);
   vm_deinit(&lvm, &sv_gpa);

   vm_builder_t cb = vmb_init(sv_str_init("built_closure"));
   vm_builder_t fb = vmb_init(sv_str_init("fn"));
   vmb_add_constant(&fb, sv_test_vm_num(1), &sv_gpa);
   sv_opt_t(uint8_t) fi = vmb_add_closure(&cb, 2, vmb_build(&fb), &sv_gpa);
   sv_test_run(t, fi.is_some && fi.value == 0);
   sv_test_run(t, cb.vm.chunk.functions.size == 1);
   sv_test_run(t, cb.vm.chunk.bytecode.size == 3);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[0] == OP_LOAD_CLOSURE);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[1] == 0);
   sv_test_run(t, cb.vm.chunk.bytecode.arr[2] == 2);
   vm_t cvm = vmb_build(&cb);
   vm_deinit(&cvm, &sv_gpa);

   vm_builder_t ob = vmb_init(sv_str_init("built_oom"));
   sv_opt_t(uint8_t) oc = vmb_add_constant(&ob, sv_test_vm_num(1), &sv_test_fail_alloc);
   sv_test_run(t, !oc.is_some);
   vm_deinit(&ob.vm, &sv_gpa);
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
   vmb_add_closure(&b, 0, vmb_build(&fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 0, 2, &sv_gpa);

   vm_t pvm = vmb_build(&b);
   sv_opt_t(error_t) perr = vm_run(&pvm, &sv_gpa);
   sv_test_run(t, perr.is_some);
   sv_test_run(t, perr.value.error_code == VM_ERR_OP_UNSUPPORTED_ARGS);
   vm_op_err* payload = perr.value.payload;
   sv_test_run(t, payload->line == 77);
   sv_test_run(t, payload->ops_len == 2);
   vm_err_deinit(&perr.value, &sv_gpa);
   vm_deinit(&pvm, &sv_gpa);
}

static inline void sv_test_vm_call_arity(sv_testing_t* t)
{
   vm_builder_t fb = vmb_init(sv_str_init("two_args"));
   fb.vm.arity = 2;
   vmb_add_bytes(&fb, OP_GET_LOCAL, 0, 1, &sv_gpa);

   vm_builder_t b = vmb_init(sv_str_init("caller"));
   vmb_add_constant(&b, sv_test_vm_num(6), &sv_gpa);
   vmb_add_closure(&b, 0, vmb_build(&fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 1, 8, &sv_gpa);

   vm_t vm = vmb_build(&b);
   sv_opt_t(error_t) err = vm_run(&vm, &sv_gpa);
   sv_test_run(t, err.is_some);
   sv_test_run(t, err.value.error_code == VM_ERR_BAD_ARITY);
   vm_arity_err* payload = err.value.payload;
   sv_test_run(t, payload->line == 8);
   sv_test_run(t, payload->expected == 2);
   sv_test_run(t, payload->got == 1);
   sv_test_run(t, vm.stack.size == 1);
   vm_err_deinit(&err.value, &sv_gpa);
   vm_deinit(&vm, &sv_gpa);
}

static inline void sv_test_vm_group(sv_testing_t* t)
{
   vm_t vm = vm_init(sv_str_init("group"));
   int success;

   vm_builder_t f0 = vmb_init(sv_str_init("member0"));
   vmb_add_bytes(&f0, OP_GET_UPVALUE, 0, 1, &sv_gpa);
   sv_vec_push(&vm.chunk.functions, vmb_build(&f0), &success, &sv_gpa);

   vm_builder_t f1 = vmb_init(sv_str_init("member1"));
   vmb_add_bytes(&f1, OP_GET_MEMBER, 0, 1, &sv_gpa);
   sv_vec_push(&vm.chunk.functions, vmb_build(&f1), &success, &sv_gpa);

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
   vmb_add_closure(&b, 1, vmb_build(&fb), &sv_gpa);
   vmb_add_bytes(&b, OP_CALL, 0, 2, &sv_gpa);

   vm_t vm = vmb_build(&b);
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
   sv_test_vm_math_errors(t);
   sv_test_vm_values(t);
   sv_test_vm_errors(t);
   sv_test_vm_negate(t);
   sv_test_vm_list_values(t);
   sv_test_vm_list_oom(t);
   sv_test_vm_builder(t);
   sv_test_vm_call_errors(t);
   sv_test_vm_call_arity(t);
   sv_test_vm_group(t);
   sv_test_vm_closure_values(t);
}

#endif
