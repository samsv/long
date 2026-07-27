#include <limits.h>
#include <math.h>
#include "map.h"

#define ERR_SET (sparse_set_t){0}
#define TRY_NOT_NULL(val, err) if ((val) == NULL) return err

#define MAX_COPY_SIZE 32
#define MAX_DEPTH 5
#define ERR_MAP (map_t){0}

#define MAX_LOAD_PERCENTAGE 75
#define ERR_NODE (map_node_t){0}

sv_opt_def(sparse_item_t);

static void dense_free(sv_vec_t(sparse_item_t)* d, const sv_allocator_t* a)
{
    sv_vec_foreach(sparse_item_t, item, d) {
        value_free(&item.key, a);
        if (item.value.is_some)
            value_free(&item.value.value, a);
    }
    sv_vec_deinit(d, a);
}

static void sparse_free(sv_vec_t(int64_t)* s, const sv_allocator_t* a)
{
    sv_vec_deinit(s, a);
}

static sparse_item_t item_borrow(sparse_item_t item)
{
    item.key = value_borrow(item.key);
    if (item.value.is_some)
        item.value.value = value_borrow(item.value.value);
    return item;
}

static sparse_set_t set_init(int64_t capacity, const sv_allocator_t* a)
{
    sv_vec_t(int64_t) sparse_vec = sv_vec_init_capacity(int64_t, capacity, a);
    TRY_NOT_NULL(sparse_vec.arr, ERR_SET);
    sparse_vec.size = capacity;
    for (int64_t i = 0; i < capacity; i++)
        sparse_vec.arr[i] = -1;

    sparse_t sparse = sv_rc_init(sv_vec_t(int64_t), sparse_vec, sparse_free, a);
    if (sparse.cell == NULL) {
        sv_vec_deinit(&sparse_vec, a);
        return ERR_SET;
    }

    sv_vec_t(sparse_item_t) dense_vec = sv_vec_init(sparse_item_t);
    dense_t dense = sv_rc_init(sv_vec_t(sparse_item_t), dense_vec, dense_free, a);
    if (dense.cell == NULL) {
        sv_rc_deinit(&sparse, a);
        return ERR_SET;
    }

    return (sparse_set_t){ .dense = dense, .sparse = sparse, .len = 0 };
}

static void set_deinit(sparse_set_t* set, const sv_allocator_t* a)
{
    sv_rc_deinit(&set->dense, a);
    sv_rc_deinit(&set->sparse, a);
}

static sparse_set_t set_borrow(sparse_set_t set)
{
    return (sparse_set_t){
        .dense = sv_rc_borrow(set.dense),
        .sparse = sv_rc_borrow(set.sparse),
        .len = set.len,
    };
}

static int64_t set_capacity(sparse_set_t set)
{
    return set.sparse.cell->value.size;
}

static const sparse_item_t* set_get(sparse_set_t set, int64_t index)
{
    int64_t i = set.sparse.cell->value.arr[index];
    if (i < 0 || i >= set.len)
        return NULL;
    const sparse_item_t* item = &set.dense.cell->value.arr[i];
    return item->sparse_index == index ? item : NULL;
}

static bool set_has_space(sparse_set_t set)
{
    return set.dense.cell->value.size == set.len;
}

static sparse_set_t set_insert(sparse_set_t set, sparse_item_t item, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&set.dense.cell->value, item_borrow(item), &success, a);
    if (!success)
        return ERR_SET;

    set.sparse.cell->value.arr[item.sparse_index] = set.len;
    return (sparse_set_t){
        .dense = sv_rc_borrow(set.dense),
        .sparse = sv_rc_borrow(set.sparse),
        .len = set.len + 1,
    };
}

static bool set_insert_mut(sparse_set_t* set, sparse_item_t item, const sv_allocator_t* a)
{
    int success = 0;
    sv_vec_push(&set->dense.cell->value, item_borrow(item), &success, a);
    if (!success)
        return false;

    set->sparse.cell->value.arr[item.sparse_index] = set->len;
    set->len++;
    return true;
}

static sparse_set_t set_update(sparse_set_t set, sparse_item_t item, const sv_allocator_t* a)
{
    int64_t di = set.sparse.cell->value.arr[item.sparse_index];

    sv_vec_t(sparse_item_t) new_vec = sv_vec_init_capacity(sparse_item_t, set.len, a);
    TRY_NOT_NULL(new_vec.arr, ERR_SET);
    new_vec.size = set.len;
    for (int64_t i = 0; i < set.len; i++)
        new_vec.arr[i] = item_borrow(i == di ? item : set.dense.cell->value.arr[i]);

    dense_t new_dense = sv_rc_init(sv_vec_t(sparse_item_t), new_vec, dense_free, a);
    if (new_dense.cell == NULL) {
        dense_free(&new_vec, a);
        return ERR_SET;
    }

    return (sparse_set_t){
        .dense = new_dense,
        .sparse = sv_rc_borrow(set.sparse),
        .len = set.len,
    };
}

typedef enum {
    GET_ITEM,
    GET_EMPTY,
    GET_FULL,
} get_result_kind;

typedef struct {
    get_result_kind kind;
    const sparse_item_t* item;
    int64_t index;
} get_result_t;

static uint32_t number_hash(double n)
{
    if (n >= (double)INT64_MIN && n < -(double)INT64_MIN && n == (double)(int64_t)n)
        return (uint32_t)(int64_t)n;

    int exp;
    n = frexp(n, &exp) * -(double)INT_MIN;
    if (!(n >= (double)INT64_MIN && n < -(double)INT64_MIN))
        return 0;
    return (uint32_t)exp + (uint32_t)(int64_t)n;
}

static uint32_t str_hash(sv_str_t s)
{
    uint32_t h = (uint32_t)s.size;
    for (int64_t i = s.size; i > 0; i--)
        h ^= (h << 5) + (h >> 2) + (uint8_t)s.chars[i - 1];
    return h;
}

/**
 * Hashes a value using the algorithms from Lua 5.4.
 *
 * Numbers replicate Lua's table key handling: a double holding an integral
 * value is hashed as that integer (Lua normalizes such keys on insertion in
 * luaH_newkey and hashes integers by value in hashint); other doubles use
 * l_hashfloat, which scales the frexp mantissa to an integer and adds the
 * exponent. NaN and infinities fail the integer range check and hash to 0,
 * as in Lua. -0.0 is integral, so it hashes like 0.0, matching value_eql.
 * https://www.lua.org/source/5.4/ltable.c.html#l_hashfloat
 *
 * Lists and iterators hash to 0 (only identity equality applies).
 *
 * Strings use luaS_hash with a zero seed (Lua's seed exists for hash
 * flooding resistance).
 * https://www.lua.org/source/5.4/lstring.c.html#luaS_hash
 *
 * Nil and booleans are fixed small constants.
 */
static uint32_t value_hash(value_t v)
{
    switch (v.kind) {
        case VALUE_NUMBER: return number_hash(v.number);
        case VALUE_NIL: return 1;
        case VALUE_BOOL: return v.boolean ? 2 : 3;
        case VALUE_OBJ:
            switch (v.obj.cell->value.kind) {
                case OBJ_STR: return str_hash(v.obj.cell->value.str);
                case OBJ_LIST:
                case OBJ_ITER:
                case OBJ_CLOSURE:
                case OBJ_CLOSURE_MEMBER: return 0;
            }
            return 0;
    }
    return 0;
}

static int64_t capacity_for_size(int64_t size)
{
    int64_t cap = size * 100 / MAX_LOAD_PERCENTAGE + 1;
    int64_t pow = 8;
    while (pow < cap)
        pow *= 2;
    return pow;
}

static map_node_t node_init(sparse_set_t set, map_t child)
{
    return (map_node_t){
        .set = set,
        .child = sv_rc_borrow(child),
        .depth = child.cell != NULL ? child.cell->value.depth + 1 : 0,
    };
}

static void node_free(map_node_t* n, const sv_allocator_t* a)
{
    set_deinit(&n->set, a);
    sv_rc_deinit(&n->child, a);
}

static map_node_t node_init_capacity(int64_t size, map_t child, const sv_allocator_t* a)
{
    sparse_set_t set = set_init(size, a);
    TRY_NOT_NULL(set.dense.cell, ERR_NODE);
    return node_init(set, child);
}

static int64_t node_physical_count(map_node_t node)
{
    return node.set.len + (node.child.cell != NULL ? node_physical_count(node.child.cell->value) : 0);
}

static get_result_t node_get_hashed(map_node_t node, value_t key, uint32_t hash, map_t child)
{
    int64_t cap = set_capacity(node.set);
    int64_t i = (int64_t)hash % cap;
    int64_t start = i;
    bool found_tomb = false;

    const sparse_item_t* item;
    while ((item = set_get(node.set, i)) != NULL) {
        if (value_eql(item->key, key)) {
            if (item->value.is_some)
                return (get_result_t){ .kind = GET_ITEM, .item = item, .index = i };
            found_tomb = true;
        }
        i = (i + 1) % cap;
        if (start == i)
            break;
    }

    get_result_t local = item == NULL
        ? (get_result_t){ .kind = GET_EMPTY, .index = i }
        : (get_result_t){ .kind = GET_FULL };

    if (found_tomb || child.cell == NULL)
        return local;

    map_node_t c = child.cell->value;
    return node_get_hashed(c, key, hash, c.child);
}

static sv_opt_t(sparse_item_t) flat_iter_next(map_flat_iter_t* it)
{
    while (it->index < it->node.set.len) {
        sparse_item_t item = it->node.set.dense.cell->value.arr[it->index];
        it->index++;
        if (item.value.is_some)
            return sv_opt_some_t(sparse_item_t, item);
    }
    return sv_opt_none_t(sparse_item_t);
}

static const map_node_t* depth_iter_node(const map_depth_iter_t* it)
{
    if (it->depth == 0)
        return &it->parent;
    return it->map.cell != NULL ? &it->map.cell->value : NULL;
}

static sv_opt_t(sparse_item_t) depth_iter_next(map_depth_iter_t* it)
{
    for (;;) {
        const map_node_t* layer = depth_iter_node(it);
        if (layer == NULL)
            return sv_opt_none_t(sparse_item_t);

        if (it->index >= layer->set.len) {
            it->map = layer->child;
            it->index = 0;
            it->depth++;
            continue;
        }

        const sparse_item_t* item = &layer->set.dense.cell->value.arr[it->index];
        it->index++;
        get_result_t r = node_get_hashed(it->parent, item->key, value_hash(item->key), it->parent.child);
        if (r.kind != GET_ITEM || r.item != item)
            continue;
        return sv_opt_some_t(sparse_item_t, *item);
    }
}

static map_iter_t iter_init_node(map_node_t node)
{
    return node.depth == 0
    ? (map_iter_t){ .kind = MAP_ITER_FLAT, .flat = { .node = node } }
    : (map_iter_t){ .kind = MAP_ITER_DEPTH, .depth = { .parent = node } };
}

static sv_opt_t(sparse_item_t) iter_next_item(map_iter_t* it)
{
    switch (it->kind) {
        case MAP_ITER_FLAT: return flat_iter_next(&it->flat);
        case MAP_ITER_DEPTH: return depth_iter_next(&it->depth);
    }
    return sv_opt_none_t(sparse_item_t);
}

static bool node_insert_mut(map_node_t* node, value_t key, sv_opt_t(value_t) value, const sv_allocator_t* a)
{
    get_result_t r = node_get_hashed(*node, key, value_hash(key), (map_t){0});
    switch (r.kind) {
        case GET_EMPTY:
            return set_insert_mut(
                &node->set,
                (sparse_item_t){ .sparse_index = r.index, .key = key, .value = value },
                a);
        case GET_ITEM:
            return true;
        case GET_FULL:
            return false;
    }
    return false;
}

#define TRY_INSERT_MUT(node, k, v, a) do {\
    if (!node_insert_mut(&(node), k, v, a)) {\
        node_free(&(node), a);\
        return ERR_NODE;\
    }\
} while (0)

static map_node_t node_init_kvs(const kv_t* kvs, int64_t len, const sv_allocator_t* a)
{
    map_node_t node = node_init_capacity(capacity_for_size(len), (map_t){0}, a);
    TRY_NOT_NULL(node.set.dense.cell, ERR_NODE);
    for (int64_t i = 0; i < len; i++)
        TRY_INSERT_MUT(node, kvs[i].key, sv_opt_some_t(value_t, kvs[i].value), a);
    return node;
}

static map_node_t node_update(map_node_t node, value_t key, sv_opt_t(value_t) value, int64_t index, const sv_allocator_t* a)
{
    sparse_set_t new_set = set_update(
        node.set,
        (sparse_item_t){ .sparse_index = index, .key = key, .value = value },
        a);
    TRY_NOT_NULL(new_set.dense.cell, ERR_NODE);
    return node_init(new_set, node.child);
}

static map_node_t node_grow(map_node_t parent, value_t key, sv_opt_t(value_t) value, const sv_allocator_t* a)
{
    map_node_t node = node_init_capacity(capacity_for_size(node_physical_count(parent)), (map_t){0}, a);
    TRY_NOT_NULL(node.set.dense.cell, ERR_NODE);

    if (value.is_some)
        TRY_INSERT_MUT(node, key, value, a);

    map_iter_t it = iter_init_node(parent);
    for (sv_opt_t(sparse_item_t) item = iter_next_item(&it); item.is_some; item = iter_next_item(&it)) {
        if (!value.is_some && value_eql(item.value.key, key))
            continue;
        TRY_INSERT_MUT(node, item.value.key, item.value.value, a);
    }

    return node;
}

static map_t node_wrap(map_node_t node, const sv_allocator_t* a)
{
    TRY_NOT_NULL(node.set.dense.cell, ERR_MAP);
    map_t map = sv_rc_init(map_node_t, node, node_free, a);
    if (map.cell == NULL)
        node_free(&node, a);
    return map;
}

static map_node_t node_layer(map_t map, value_t key, sv_opt_t(value_t) value, const sv_allocator_t* a)
{
    map_node_t layer = node_init_capacity(capacity_for_size(1), map, a);
    TRY_NOT_NULL(layer.set.dense.cell, ERR_NODE);
    TRY_INSERT_MUT(layer, key, value, a);
    return layer;
}

#undef TRY_INSERT_MUT

static map_node_t map_update_node(map_t map, value_t key, sv_opt_t(value_t) value, int64_t index, const sv_allocator_t* a)
{
    map_node_t node = map.cell->value;
    if (node_physical_count(node) <= MAX_COPY_SIZE)
        return node_update(node, key, value, index, a);
    if (node.depth >= MAX_DEPTH)
        return node_grow(node, key, value, a);
    return node_layer(map, key, value, a);
}

map_t map_init(const kv_t* kvs, int64_t len, const sv_allocator_t* a)
{
    return node_wrap(node_init_kvs(kvs, len, a), a);
}

void map_deinit(map_t* map, const sv_allocator_t* a)
{
    sv_rc_deinit(map, a);
}

sv_opt_t(value_t) map_get(map_t map, value_t key)
{
    map_node_t node = map.cell->value;
    get_result_t r = node_get_hashed(node, key, value_hash(key), node.child);
    if (r.kind != GET_ITEM)
        return sv_opt_none_t(value_t);
    return sv_opt_some_t(value_t, r.item->value.value);
}

map_t map_put(map_t map, kv_t kv, const sv_allocator_t* a)
{
    map_node_t node = map.cell->value;
    sv_opt_t(value_t) value = sv_opt_some_t(value_t, kv.value);
    get_result_t r = node_get_hashed(node, kv.key, value_hash(kv.key), (map_t){0});

    map_node_t new_node;
    switch (r.kind) {
        case GET_EMPTY:
            if (node.set.len * 100 / set_capacity(node.set) >= MAX_LOAD_PERCENTAGE) {
                new_node = node_grow(node, kv.key, value, a);
            } else if (set_has_space(node.set)) {
                sparse_set_t new_set = set_insert(
                    node.set,
                    (sparse_item_t){ .sparse_index = r.index, .key = kv.key, .value = value },
                    a);
                if (new_set.dense.cell == NULL)
                    return ERR_MAP;
                new_node = node_init(new_set, node.child);
            } else if (node.depth >= MAX_DEPTH || node_physical_count(node) <= MAX_COPY_SIZE) {
                new_node = node_grow(node, kv.key, value, a);
            } else {
                new_node = node_layer(map, kv.key, value, a);
            }
            break;
        case GET_ITEM:
            new_node = map_update_node(map, kv.key, value, r.index, a);
            break;
        case GET_FULL:
            new_node = node_grow(node, kv.key, value, a);
            break;
    }

    return node_wrap(new_node, a);
}

map_t map_delete(map_t map, value_t key, const sv_allocator_t* a)
{
    map_node_t node = map.cell->value;
    get_result_t r = node_get_hashed(node, key, value_hash(key), node.child);

    map_node_t new_node = r.kind == GET_ITEM
        ? map_update_node(map, key, sv_opt_none_t(value_t), r.index, a)
        : node_init(set_borrow(node.set), node.child);

    return node_wrap(new_node, a);
}

int64_t map_count(map_t map)
{
    map_iter_t it = iter_init_node(map.cell->value);
    int64_t count = 0;
    for (sv_opt_t(sparse_item_t) item = iter_next_item(&it); item.is_some; item = iter_next_item(&it))
        count++;
    return count;
}

map_iter_t map_iter_init(map_t map)
{
    map_iter_t it = iter_init_node(map.cell->value);
    if (it.kind == MAP_ITER_FLAT)
        it.flat.root = sv_rc_borrow(map);
    else
        it.depth.root = sv_rc_borrow(map);
    return it;
}

void map_iter_deinit(map_iter_t* it, const sv_allocator_t* a)
{
    switch (it->kind) {
        case MAP_ITER_FLAT: sv_rc_deinit(&it->flat.root, a); break;
        case MAP_ITER_DEPTH: sv_rc_deinit(&it->depth.root, a); break;
    }
}

sv_opt_t(kv_t) map_iter_next(map_iter_t* it)
{
    sv_opt_t(sparse_item_t) item = iter_next_item(it);
    if (!item.is_some)
        return sv_opt_none_t(kv_t);
    kv_t kv = { .key = item.value.key, .value = item.value.value.value };
    return sv_opt_some_t(kv_t, kv);
}
