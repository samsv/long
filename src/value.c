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
