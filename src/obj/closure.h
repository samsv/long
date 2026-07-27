#ifndef LONG_CLOSURE_H
#define LONG_CLOSURE_H

#include "../common.h"
#include "../std/vector.h"
#include "../std/rc.h"

typedef struct vm_t vm_t;

sv_vec_def(vm_t);

typedef struct {
    vm_t* function;
    sv_vec_t(value_t) upvalues;
} closure_t;


typedef struct {
    // non-owning view of the chunk's storage, in block order
    sv_vec_t(vm_t) members;
    // every member's captured upvalues, concatenated; each member's offset is
    // baked into its get_upvalue operands at compile time
    sv_vec_t(value_t) upvalues;
} closure_group_t;

sv_rc_def(closure_group_t);

typedef struct {
    sv_rc_t(closure_group_t) group;
    size_t index;
} closure_member_t;

vm_t cls_get_vm(closure_t);
void cls_deinit(closure_t*, const sv_allocator_t*);

void clsg_deinit(closure_group_t*, const sv_allocator_t*);

vm_t clsm_get_vm(closure_member_t);
void clsm_deinit(closure_member_t*, const sv_allocator_t*);

#endif
