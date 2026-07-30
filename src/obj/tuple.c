#include "tuple.h"

tuple_t tuple_init(const value_t* values, uint8_t size, const sv_allocator_t* a)
{
    tuple_item_t* items = sv_malloc(a, size * sizeof(tuple_item_t));
    if (items == NULL)
        return (tuple_t){0};

    for (uint16_t i = 0; i < 2 * (uint16_t) size; i += 2)
        items[i / 2] = (tuple_item_t){ .id = (uint32_t)values[i].number, .value = value_borrow(values[i + 1]) };

    return (tuple_t){ .items = items, .size = size };
}

void tuple_deinit(tuple_t* t, const sv_allocator_t* a)
{
    for (uint8_t i = 0; i < t->size; i++)
        value_free(&t->items[i].value, a);
    sv_free(a, t->items);
}

bool tuple_eql(tuple_t t1, tuple_t t2)
{
    if (t1.size != t2.size)
        return false;

    for (uint8_t i = 0; i < t1.size; i++)
        if (t1.items[i].id != t2.items[i].id || !value_eql(t1.items[i].value, t2.items[i].value))
            return false;

    return true;
}

sv_opt_t(value_t) tuple_get(tuple_t tuple, uint32_t target)
{
    if (tuple.size == 0)
        return sv_opt_none_t(value_t);

    int low = 0;
    int high = tuple.size - 1;

    tuple_item_t* items = tuple.items;
    while (low <= high) {
        int mid = low + (high - low) / 2;

        if (items[mid].id == target)
            return sv_opt_some_t(value_t, items[mid].value);
        else if (items[mid].id < target)
            low = mid + 1;
        else
            high = mid - 1;
    }
    return sv_opt_none_t(value_t);
}
