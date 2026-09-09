#include "record.h"

record_t record_init(const value_t* values, uint8_t size, const sv_allocator_t* a)
{
    record_item_t* items = sv_malloc(a, size * sizeof(record_item_t));
    if (items == NULL)
        return (record_t){0};

    for (uint16_t i = 0; i < 2 * (uint16_t) size; i += 2)
        items[i / 2] = (record_item_t){ .id = (uint32_t)values[i].number, .value = value_borrow(values[i + 1]) };

    return (record_t){ .items = items, .size = size };
}

void record_deinit(record_t* t, const sv_allocator_t* a)
{
    for (uint8_t i = 0; i < t->size; i++)
        value_free(&t->items[i].value, a);
    sv_free(a, t->items);
}

bool record_eql(record_t t1, record_t t2)
{
    if (t1.size != t2.size)
        return false;

    for (uint8_t i = 0; i < t1.size; i++)
        if (t1.items[i].id != t2.items[i].id || !value_eql(t1.items[i].value, t2.items[i].value))
            return false;

    return true;
}

sv_opt_t(value_t) record_get(record_t tuple, uint32_t target)
{
    if (tuple.size == 0)
        return sv_opt_none_t(value_t);

    int low = 0;
    int high = tuple.size - 1;

    record_item_t* items = tuple.items;
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

record_t record_update(record_t base, const value_t* pairs, uint8_t n, int64_t* missing,
                       const sv_allocator_t* a)
{
    *missing = -1;
    record_item_t* items = sv_malloc(a, base.size * sizeof(record_item_t));
    if (items == NULL)
        return (record_t){0};

    uint8_t at = 0;
    for (uint16_t i = 0; i < 2 * (uint16_t)n; i += 2) {
        uint32_t id = (uint32_t)pairs[i].number;

        // the fields below this one are kept, then the field itself must be next
        for (; at < base.size && base.items[at].id < id; at++)
            items[at] = (record_item_t){ .id = base.items[at].id, .value = value_borrow(base.items[at].value) };

        if (at == base.size || base.items[at].id != id) {
            for (uint8_t j = 0; j < at; j++)
                value_free(&items[j].value, a);
            sv_free(a, items);
            *missing = id;
            return (record_t){0};
        }

        items[at++] = (record_item_t){ .id = id, .value = value_borrow(pairs[i + 1]) };
    }

    for (; at < base.size; at++)
        items[at] = (record_item_t){ .id = base.items[at].id, .value = value_borrow(base.items[at].value) };

    return (record_t){ .items = items, .size = base.size };
}
