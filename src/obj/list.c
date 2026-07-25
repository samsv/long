#include "list.h"
#include "../value.h"

static void bucket_free(bucket_t* b, const sv_allocator_t* a)
{
    sv_vec_foreach(value_t, v, b)
        value_free(&v, a);

    sv_vec_deinit(b, a);
}

static void node_free(node_t* n, const sv_allocator_t* a)
{
    sv_rc_deinit(&n->bucket, a);
    sv_rc_deinit(&n->tail, a);
}


list_t ll_init(const value_t* vs, int64_t len, const sv_allocator_t* a)
{
    if (len == 0)
        return ll_empty();

    sv_vec_t(value_t) vec = sv_vec_init_capacity(value_t, len, a);
    if (vec.arr == NULL)
        return (list_t) {0};

    vec.size = len;
    for (int64_t i = 0; i < vec.size; i++)
        vec.arr[i] = value_borrow(vs[i]);

    return ll_init_from_vec(vec, a);
}


list_t ll_init_from_vec(sv_vec_t(value_t) vec, const sv_allocator_t* a)
{
    sv_rc_t(bucket_t) bucket = sv_rc_init(bucket_t, vec, bucket_free, a);
    if (bucket.cell == NULL) {
        bucket_free(&vec, a);
        return (list_t){0};
    }

    node_t node = {
        .bucket = bucket,
        .start = 0,
        .len = vec.size,
        .tail = ll_empty(),
    };
    list_t list = sv_rc_init(node_t, node, node_free, a);
    if (list.cell == NULL)
        sv_rc_deinit(&bucket, a);

    return list;
}

static void mock_deinit(node_t* n, const sv_allocator_t* a)
{
    (void)n;
    (void)a;
}

list_t ll_empty(void)
{
    static sv_rc_cell_t(node_t) empty_cell = {
        .value = {
            .bucket = {0},
            .len = 0,
            .start = 0,
            .tail = { .cell = &empty_cell },
        },
        .count = 1,
        .free_fn = mock_deinit,
    };
    static list_t empty_list = {.cell = &empty_cell};
    return sv_rc_borrow(empty_list);
}

void ll_deinit(list_t* l, const sv_allocator_t* a)
{
    sv_rc_deinit(l, a);
}

