#include "list.h"
#include "../value.h"

#define COPY_THRESH 32
#define ERR_LIST (list_t){0}
#define TRY_NOT_NULL(val) if ((val) == NULL) return ERR_LIST
#define TRY_POSITIVE(i) if (i < 0) return ERR_LIST

static void bucket_free(bucket_t* b, const sv_allocator_t* a)
{
    sv_vec_foreach(value_t, v, b)
        value_free(&v, a);

    sv_vec_deinit(b, a);
}

static void node_free(node_t* n, const sv_allocator_t* a)
{
    sv_rc_deinit(&n->bucket, a);
    if (n->tail.cell->value.len > 0)
        sv_rc_deinit(&n->tail, a);
}


list_t ll_init(const value_t* vs, int64_t len, const sv_allocator_t* a)
{
    if (len == 0)
        return ll_empty();

    sv_vec_t(value_t) vec = sv_vec_init_capacity(value_t, len, a);
    TRY_NOT_NULL(vec.arr);

    vec.size = len;
    for (int64_t i = 0; i < vec.size; i++)
        vec.arr[i] = value_borrow(vs[vec.size - i - 1]);

    return ll_init_from_vec_rev(vec, a);
}


list_t ll_init_from_vec_rev(sv_vec_t(value_t) vec, const sv_allocator_t* a)
{
    if (vec.size == 0) {
        sv_vec_deinit(&vec, a);
        return ll_empty();
    }

    sv_rc_t(bucket_t) bucket = sv_rc_init(bucket_t, vec, bucket_free, a);
    if (bucket.cell == NULL) {
        bucket_free(&vec, a);
        return ERR_LIST;
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

list_t ll_init_from_vec(sv_vec_t(value_t) vec, const sv_allocator_t* a)
{
    for (int64_t i = 0; i < vec.size / 2; i++) {
        value_t v = vec.arr[i];
        vec.arr[i] = vec.arr[vec.size - i - 1];
        vec.arr[vec.size - i - 1] = v;
    }
    return ll_init_from_vec_rev(vec, a);
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

sv_opt_t(value_t) ll_get(list_t l, int64_t n)
{
    node_t node = l.cell->value;
    if (n >= node.len || n < 0)
        return node.len != 0 ? ll_get(node.tail, n - node.len) : sv_opt_none_t(value_t);

    return sv_opt_some_t(value_t, node.bucket.cell->value.arr[node.start + node.len - 1 - n]);
}

sv_opt_t(value_t) ll_head(list_t l)
{
    node_t node = l.cell->value;
    return node.len > 0
        ? sv_opt_some_t(value_t, node.bucket.cell->value.arr[node.start + node.len - 1])
        : sv_opt_none_t(value_t);
}

static list_t init_with_tail(list_t tail, const value_t* vs, int64_t n, const sv_allocator_t* a) {
    list_t list = ll_init(vs, n, a);
    if (list.cell == NULL) {
        ll_deinit(&tail, a);
        return ERR_LIST;
    }
    ll_deinit(&list.cell->value.tail, a);
    list.cell->value.tail = tail;
    return list;
}

static list_t init_from_bucket(sv_rc_t(bucket_t) bucket, int64_t start, int64_t len,
                               list_t tail, const sv_allocator_t* a)
{
    node_t new_node = {
        .bucket = sv_rc_borrow(bucket),
        .start = start,
        .len = len,
        .tail = tail,
    };
    list_t list = sv_rc_init(node_t, new_node, node_free, a);

    if (list.cell == NULL)
        node_free(&new_node, a);
    return list;
}

list_t ll_prepend(list_t list, value_t v, const sv_allocator_t* a)
{
    node_t node = list.cell->value;
    if (node.len == 0)
        return init_with_tail(sv_rc_borrow(list), &v, 1, a);

    bucket_t* bucket = &node.bucket.cell->value;

    if (bucket->size != node.start + node.len) {
        // data has already been added to the bucket
        if (COPY_THRESH <= node.len)
            return init_with_tail(sv_rc_borrow(list), &v, 1, a);

        // allocate new bucket with old elements
        bucket_t new_bucket = sv_vec_init_capacity(value_t, node.len + 1, a);
        TRY_NOT_NULL(new_bucket.arr);
        new_bucket.size = node.len + 1;
        for (int64_t i = 0; i < new_bucket.size - 1; i++)
            new_bucket.arr[i] = value_borrow(bucket->arr[node.start + i]);
        new_bucket.arr[new_bucket.size - 1] = value_borrow(v);
        list_t new_list = ll_init_from_vec_rev(new_bucket, a);
        TRY_NOT_NULL(new_list.cell);
        new_list.cell->value.tail = sv_rc_borrow(node.tail);
        return new_list;
    }

    list_t new_list = init_from_bucket(node.bucket, node.start, node.len + 1, sv_rc_borrow(node.tail), a);
    TRY_NOT_NULL(new_list.cell);

    int success = 0;
    sv_vec_push(bucket, value_borrow(v), &success, a);
    if (!success) {
        ll_deinit(&new_list, a);
       return ERR_LIST;
    }

    return new_list;
}

list_t ll_prepend_arr(list_t list, const value_t* vs, int64_t n, const sv_allocator_t* a)
{
    TRY_POSITIVE(n);
    if (n == 0)
        return sv_rc_borrow(list);

    node_t node = list.cell->value;
    if (node.len == 0)
        return ll_init(vs, n, a);

    bucket_t* bucket = &node.bucket.cell->value;
    if (bucket->size != node.start + node.len) {
        // data has already been added to the bucket
        if (COPY_THRESH <= node.len)
            return init_with_tail(sv_rc_borrow(list), vs, n, a);

        // allocate new bucket with old elements
        bucket_t new_bucket = sv_vec_init_capacity(value_t, node.len + n, a);
        TRY_NOT_NULL(new_bucket.arr);
        for (int64_t i = 0; i < node.len; i++)
            new_bucket.arr[i] = value_borrow(bucket->arr[node.start + i]);
        for (int64_t i = 0; i < n; i++)
            new_bucket.arr[node.len + i] = value_borrow(vs[n - i - 1]);
        new_bucket.size = node.len + n;

        list_t new_list = ll_init_from_vec_rev(new_bucket, a);
        TRY_NOT_NULL(new_list.cell);
        new_list.cell->value.tail = sv_rc_borrow(node.tail);
        return new_list;
    }

    list_t new_list = init_from_bucket(node.bucket, node.start, node.len + n, sv_rc_borrow(node.tail), a);
    TRY_NOT_NULL(new_list.cell);

    int success = 0;
    int64_t new_size = bucket->size + n;
    sv_vec_grow_cap(bucket, new_size, &success, a);
    if (!success) {
        ll_deinit(&new_list, a);
       return ERR_LIST;
    }

    for (int64_t i = 0; i < n; i++)
        bucket->arr[bucket->size + i] = value_borrow(vs[n - i - 1]);
    bucket->size = new_size;

    return new_list;
}

list_t ll_add(list_t left, list_t right, const sv_allocator_t* a)
{
    node_t node = left.cell->value;
    if (node.len == 0)
        return sv_rc_borrow(right);
    if (right.cell->value.len == 0)
        return sv_rc_borrow(left);

    list_t new_tail = ll_add(node.tail, right, a);
    TRY_NOT_NULL(new_tail.cell);
    return init_from_bucket(node.bucket, node.start, node.len, new_tail, a);
}

#define SET_HEAD(...) do { head = __VA_ARGS__; if (head.cell == NULL) goto error; } while (0)
#define RETURN_RECURSIVE(function) do {\
    if (node.tail.cell->value.len == 0) return ERR_LIST; \
    list_t new_tail = function(node.tail, v, i - node.len, a); \
    TRY_NOT_NULL(new_tail.cell); \
    return init_from_bucket(node.bucket, node.start, node.len, new_tail, a); \
} while (0)
list_t ll_insert(list_t list, value_t v, int64_t i, const sv_allocator_t* a)
{
    TRY_POSITIVE(i);
    if (i == 0)
        return ll_prepend(list, v, a);

    node_t node = list.cell->value;
    if (i > node.len)
        RETURN_RECURSIVE(ll_insert);

    list_t head = sv_rc_borrow(node.tail);

    if (i < node.len)
        SET_HEAD(init_from_bucket(node.bucket, node.start, node.len - i, head, a));
    SET_HEAD(init_with_tail(head, &v, 1, a));
    SET_HEAD(init_from_bucket(node.bucket, node.start + node.len - i, i, head, a));
    return head;

error:
    ll_deinit(&head, a);
    return ERR_LIST;
}

list_t ll_update(list_t list, value_t v, int64_t i, const sv_allocator_t* a)
{
    TRY_POSITIVE(i);

    node_t node = list.cell->value;

    if (i >= node.len)
        RETURN_RECURSIVE(ll_update);

    list_t head = sv_rc_borrow(node.tail);

    if (i < node.len - 1)
        SET_HEAD(init_from_bucket(node.bucket, node.start, node.len - i - 1, head, a));
    SET_HEAD(init_with_tail(head, &v, 1, a));
    if (i > 0)
        SET_HEAD(init_from_bucket(node.bucket, node.start + node.len - i, i, head, a));
    return head;

error:
    ll_deinit(&head, a);
    return ERR_LIST;
}

static list_t ll_delete_at_impl(list_t list, void* v, int64_t i, const sv_allocator_t* a)
{
    TRY_POSITIVE(i);

    node_t node = list.cell->value;
    if (i == 0)
        return node.len > 0 ? ll_tail(list, a) : ERR_LIST;

    if (i >= node.len)
        RETURN_RECURSIVE(ll_delete_at_impl);

    list_t head = sv_rc_borrow(node.tail);

    if (i < node.len - 1)
        SET_HEAD(init_from_bucket(node.bucket, node.start, node.len - i - 1, head, a));
    SET_HEAD(init_from_bucket(node.bucket, node.start + node.len - i, i, head, a));
    return head;

error:
    ll_deinit(&head, a);
    return ERR_LIST;
}

list_t ll_delete_at(list_t list, int64_t i, const sv_allocator_t* a)
{
    return ll_delete_at_impl(list, NULL, i, a);
}

#undef SET_HEAD

static int64_t ll_count_rec(list_t l, int64_t acc)
{
    node_t node = l.cell->value;
    if (node.len == 0)
        return acc;
    return ll_count_rec(node.tail, acc + node.len);
}

list_t ll_tail(list_t list, const sv_allocator_t* a)
{
    node_t node = list.cell->value;
    return node.len > 1 ?
        init_from_bucket(node.bucket, node.start, node.len - 1, sv_rc_borrow(node.tail), a)
        : sv_rc_borrow(node.tail);
}

int64_t ll_count(list_t l)
{
    return ll_count_rec(l, 0);
}

ll_iter_t ll_iter_init(list_t l)
{
    return (ll_iter_t){
        .root = sv_rc_borrow(l),
        .node = l,
        .index = l.cell->value.start + l.cell->value.len - 1,
    };
}

void ll_iter_deinit(ll_iter_t* it, const sv_allocator_t* a)
{
    sv_rc_deinit(&it->root, a);
}

sv_opt_t(value_t) ll_iter_next(ll_iter_t* it)
{
    node_t node = it->node.cell->value;
    if (node.len == 0)
        return sv_opt_none_t(value_t);

    value_t v = node.bucket.cell->value.arr[it->index];
    if (it->index == node.start) {
        it->node = node.tail;
        it->index = node.tail.cell->value.start + node.tail.cell->value.len - 1;
    } else {
        it->index--;
    }
    return sv_opt_some_t(value_t, v);
}
