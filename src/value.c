#include "value.h"


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
