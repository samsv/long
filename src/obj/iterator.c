#include "iterator.h"
#include "../value.h"
#include "../obj.h"

#define FIELD_KEY 0
#define FIELD_VALUE 1

iter_t iter_init(value_t from)
{
    switch (from.obj.cell->value.kind) {
        case OBJ_LIST: return (iter_t){ .kind = ITER_LIST, .list = ll_iter_init(AS_LIST(from)) };
        case OBJ_STR: return (iter_t){ .kind = ITER_STR, .str = str_iter_init(&AS_STR(from)) };
        case OBJ_MAP: return (iter_t){ .kind = ITER_MAP, .map = map_iter_init(AS_MAP(from)) };
        case OBJ_ITER:
        case OBJ_RECORD:
        case OBJ_TUPLE:
        case OBJ_ERR:
        case OBJ_NATIVE_FN:
        case OBJ_CLOSURE:
        case OBJ_CLOSURE_MEMBER:
            break;
    }
    return (iter_t){0};
}

void iter_deinit(iter_t* it, const sv_allocator_t* a)
{
    switch (it->kind) {
        case ITER_LIST: ll_iter_deinit(&it->list, a); break;
        case ITER_STR: str_iter_deinit(&it->str, a); break;
        case ITER_MAP: map_iter_deinit(&it->map, a); break;
    }
}

value_t iter_next(iter_t* it, const sv_allocator_t* a)
{
    switch (it->kind) {
        case ITER_LIST: {
            sv_opt_t(value_t) v = ll_iter_next(&it->list);
            return v.is_some ? value_borrow(v.value) : (value_t){ .kind = VALUE_NIL };
        }
        case ITER_STR: return str_iter_next(&it->str, a);
        case ITER_MAP: {
            sv_opt_t(kv_t) kv = map_iter_next(&it->map);
            if (!kv.is_some)
                return (value_t){ .kind = VALUE_NIL };

            const value_t pair[4] = {
                { .kind = VALUE_NUMBER, .number = FIELD_KEY }, kv.value.key,
                { .kind = VALUE_NUMBER, .number = FIELD_VALUE }, kv.value.value,
            };
            return value_init_record(pair, 2, a);
        }
    }
    return (value_t){ .kind = VALUE_NIL };
}
