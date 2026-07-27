#include "closure.h"
#include "../value.h"
#include "../vm.h"

vm_t cls_get_vm(closure_t cls)
{
    return *cls.function;
}

void cls_deinit(closure_t* cls, const sv_allocator_t* a)
{
    sv_vec_foreach(value_t, v, &cls->upvalues)
        value_free(&v, a);
    sv_vec_deinit(&cls->upvalues, a);
}

void clsg_deinit(closure_group_t* g, const sv_allocator_t* a)
{
    sv_vec_foreach(value_t, v, &g->upvalues)
        value_free(&v, a);
    sv_vec_deinit(&g->upvalues, a);
}

vm_t clsm_get_vm(closure_member_t m)
{
    return m.group.cell->value.members.arr[m.index];
}

void clsm_deinit(closure_member_t* m, const sv_allocator_t* a)
{
    sv_rc_deinit(&m->group, a);
}
