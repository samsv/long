#include "value.h"
#include "obj/list.h"
#include "obj/iterator.h"
#include "obj/closure.h"

#define ERR_VALUE (value_t){ .kind = VALUE_OBJ }


void value_free(value_t* v, const sv_allocator_t* a)
{
    if (v->kind == VALUE_OBJ)
        sv_rc_deinit(&v->obj, a);
}

value_t value_borrow(value_t v)
{
    switch (v.kind) {
        case VALUE_OBJ:
            return (value_t){
                .kind = VALUE_OBJ,
                .obj = sv_rc_borrow(v.obj),
            };
        case VALUE_NIL:
        case VALUE_BOOL:
        case VALUE_NUMBER:
        default:
            return v;
    }
}

bool value_eql(value_t x, value_t y)
{
    if (x.kind != y.kind)
        return false;
    switch (x.kind) {
        case VALUE_NUMBER: return x.number == y.number;
        case VALUE_NIL: return true;
        case VALUE_BOOL: return x.boolean == y.boolean;
        case VALUE_OBJ: return x.obj.cell == y.obj.cell;
    }
    return false;
}

static void obj_free(obj_t* o, const sv_allocator_t* a)
{
    switch (o->kind) {
        case OBJ_STR: sv_str_deinit(&o->str, a); break;
        case OBJ_LIST: ll_deinit(&o->list, a); break;
        case OBJ_ITER: iter_deinit(&o->iter, a); break;
        case OBJ_CLOSURE: cls_deinit(&o->closure, a); break;
        case OBJ_CLOSURE_MEMBER: clsm_deinit(&o->closure_member, a); break;
    }
}

static value_t obj_wrap(obj_t o, const sv_allocator_t* a)
{
    sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, obj_free, a);
    if (rc.cell == NULL)
        return ERR_VALUE;
    return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}

value_t value_init_list(const value_t* vs, int64_t len, const sv_allocator_t* a)
{
    list_t list = ll_init(vs, len, a);
    if (list.cell == NULL)
        return ERR_VALUE;

    value_t v = obj_wrap((obj_t){ .kind = OBJ_LIST, .list = list }, a);
    if (v.obj.cell == NULL)
        ll_deinit(&list, a);
    return v;
}

value_t value_init_iter(value_t from, const sv_allocator_t* a)
{
    iter_t iter = iter_init(from);
    value_t v = obj_wrap((obj_t){ .kind = OBJ_ITER, .iter = iter }, a);
    if (v.obj.cell == NULL)
        iter_deinit(&iter, a);
    return v;
}

value_t value_init_closure(vm_t* function, const value_t* ups, int64_t n, const sv_allocator_t* a)
{
    closure_t cls = { .function = function, .upvalues = sv_vec_init(value_t) };
    if (n > 0) {
        cls.upvalues = (sv_vec_t(value_t))sv_vec_init_capacity(value_t, n, a);
        if (cls.upvalues.arr == NULL)
            return ERR_VALUE;
        cls.upvalues.size = n;
        for (int64_t i = 0; i < n; i++)
            cls.upvalues.arr[i] = value_borrow(ups[i]);
    }

    value_t v = obj_wrap((obj_t){ .kind = OBJ_CLOSURE, .closure = cls }, a);
    if (v.obj.cell == NULL)
        cls_deinit(&cls, a);
    return v;
}

value_t value_init_closure_member(sv_rc_t(closure_group_t) group, int64_t index, const sv_allocator_t* a)
{
    value_t v = obj_wrap(
        (obj_t){ .kind = OBJ_CLOSURE_MEMBER, .closure_member = { .group = group, .index = (size_t)index } },
        a);
    if (v.obj.cell == NULL)
        sv_rc_deinit(&group, a);
    return v;
}
