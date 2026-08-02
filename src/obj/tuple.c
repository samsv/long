#include "tuple.h"
#include "../value.h"

tuple_t tuple_init(const value_t* values, uint8_t size, const sv_allocator_t* a)
{
    value_t* items = sv_malloc(a, size * sizeof(value_t));
    if (items == NULL)
        return (tuple_t){0};

    for (uint8_t i = 0; i < size; i++)
        items[i] = value_borrow(values[i]);

    return (tuple_t){ .items = items, .size = size };
}

void tuple_deinit(tuple_t* t, const sv_allocator_t* a)
{
    for (uint8_t i = 0; i < t->size; i++)
        value_free(&t->items[i], a);
    sv_free(a, t->items);
}

bool tuple_eql(tuple_t t1, tuple_t t2)
{
    if (t1.size != t2.size)
        return false;

    for (uint8_t i = 0; i < t1.size; i++)
        if (!value_eql(t1.items[i], t2.items[i]))
            return false;

    return true;
}

sv_opt_t(value_t) tuple_get(tuple_t tuple, int64_t i)
{
    if (i < 0 || i >= tuple.size)
        return sv_opt_none_t(value_t);
    return sv_opt_some_t(value_t, tuple.items[i]);
}
