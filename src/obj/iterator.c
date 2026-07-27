#include "iterator.h"
#include "../value.h"

iter_t iter_init(value_t from)
{
    switch (from.obj.cell->value.kind) {
        case OBJ_LIST: return (iter_t){ .kind = ITER_LIST, .list = ll_iter_init(from.obj.cell->value.list) };
        case OBJ_STR:
        case OBJ_ITER: break;
    }
    return (iter_t){0};
}

void iter_deinit(iter_t* it, const sv_allocator_t* a)
{
    switch (it->kind) {
        case ITER_LIST: ll_iter_deinit(&it->list, a); break;
    }
}

value_t iter_next(iter_t* it)
{
    switch (it->kind) {
        case ITER_LIST: {
            sv_opt_t(value_t) v = ll_iter_next(&it->list);
            return v.is_some ? v.value : (value_t){ .kind = VALUE_NIL };
        }
    }
    return (value_t){ .kind = VALUE_NIL };
}
