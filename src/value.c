#include "value.h"
#include "obj/list.h"
#include "obj/iterator.h"


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
    }
}

value_t value_init_list(const value_t* vs, int64_t len, const sv_allocator_t* a)
{
    list_t list = ll_init(vs, len, a);
    if (list.cell == NULL)
        return (value_t){ .kind = VALUE_OBJ };

    obj_t o = { .kind = OBJ_LIST, .list = list };
    sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, obj_free, a);
    if (rc.cell == NULL) {
        ll_deinit(&list, a);
        return (value_t){ .kind = VALUE_OBJ };
    }
    return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}

value_t value_init_iter(value_t from, const sv_allocator_t* a)
{
    obj_t o = { .kind = OBJ_ITER, .iter = iter_init(from) };
    sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, obj_free, a);
    if (rc.cell == NULL) {
        iter_deinit(&o.iter, a);
        return (value_t){ .kind = VALUE_OBJ };
    }
    return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}
