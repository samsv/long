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
    for (int64_t i = vec.size; i > 0; i--)
        vec.arr[vec.size - i] = value_borrow(vs[i - 1]);

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

const value_t* ll_get(list_t l, int64_t n)
{
    node_t node = l.cell->value;
    if (node.len < n || n < 0)
        return node.len != 0 ? ll_get(node.tail, n - node.len) : NULL;

    return &node.bucket.cell->value.arr[node.start + node.len - 1 - n];
}

const value_t* ll_head(list_t l)
{
    node_t node = l.cell->value;
    return node.len > 0 ?
        &node.bucket.cell->value.arr[node.start + node.len - 1]
        : NULL;
}

static int64_t ll_count_rec(list_t l, int64_t acc)
{
    node_t node = l.cell->value;
    if (node.len == 0)
        return acc;
    return ll_count_rec(node.tail, acc + node.len);
}

int64_t ll_count(list_t l)
{
    return ll_count_rec(l, 0);
}
