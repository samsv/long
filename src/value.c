#include <stdio.h>
#include "value.h"
#include "obj/list.h"
#include "obj/map.h"
#include "obj/iterator.h"
#include "obj/closure.h"
#include "vm.h"

#define ERR_VALUE (value_t){ .kind = VALUE_OBJ }


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

static bool list_eql(list_t x, list_t y)
{
    if (ll_count(x) != ll_count(y))
        return false;
    ll_iter_t ix = ll_iter_init_no_borrow(x);
    ll_iter_t iy = ll_iter_init_no_borrow(y);
    for (;;) {
        sv_opt_t(value_t) ex = ll_iter_next(&ix);
        sv_opt_t(value_t) ey = ll_iter_next(&iy);
        if (!ex.is_some)
            return true;
        if (!value_eql(ex.value, ey.value))
            return false;
    }
}

static bool map_eql(map_t x, map_t y)
{
    if (map_count(x) != map_count(y))
        return false;
    map_iter_t it = map_iter_init_no_borrow(x);
    for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
        sv_opt_t(value_t) other = map_get(y, kv.value.key);
        if (!other.is_some || !value_eql(kv.value.value, other.value))
            return false;
    }
    return true;
}

bool value_eql(value_t x, value_t y)
{
    if (x.kind != y.kind)
        return false;
    switch (x.kind) {
        case VALUE_NUMBER: return x.number == y.number;
        case VALUE_NIL: return true;
        case VALUE_BOOL: return x.boolean == y.boolean;
        case VALUE_OBJ: {
            if (x.obj.cell == y.obj.cell)
                return true;
            const obj_t* ox = &x.obj.cell->value;
            const obj_t* oy = &y.obj.cell->value;
            if (ox->kind != oy->kind)
                return false;
            switch (ox->kind) {
                case OBJ_STR: return sv_str_comp(ox->str, oy->str);
                case OBJ_LIST: return list_eql(ox->list, oy->list);
                case OBJ_MAP: return map_eql(ox->map, oy->map);
                case OBJ_ITER:
                case OBJ_CLOSURE:
                case OBJ_CLOSURE_MEMBER: return false;
            }
            return false;
        }
    }
    return false;
}

static void obj_free(obj_t* o, const sv_allocator_t* a)
{
    switch (o->kind) {
        case OBJ_STR: sv_str_deinit(&o->str, a); break;
        case OBJ_LIST: ll_deinit(&o->list, a); break;
        case OBJ_MAP: map_deinit(&o->map, a); break;
        case OBJ_ITER: iter_deinit(&o->iter, a); break;
        case OBJ_CLOSURE: cls_deinit(&o->closure, a); break;
        case OBJ_CLOSURE_MEMBER: clsm_deinit(&o->closure_member, a); break;
    }
}

static value_t obj_wrap(obj_t o, const sv_allocator_t* a)
{
    sv_rc_t(obj_t) rc = sv_rc_init(obj_t, o, obj_free, a);
    if (rc.cell == NULL)
        return ERR_VALUE;
    return (value_t){ .kind = VALUE_OBJ, .obj = rc };
}

value_t value_init_list(const value_t* vs, int64_t len, const sv_allocator_t* a)
{
    list_t list = ll_init(vs, len, a);
    if (list.cell == NULL)
        return ERR_VALUE;

    value_t v = obj_wrap((obj_t){ .kind = OBJ_LIST, .list = list }, a);
    if (v.obj.cell == NULL)
        ll_deinit(&list, a);
    return v;
}

value_t value_init_iter(value_t from, const sv_allocator_t* a)
{
    iter_t iter = iter_init(from);
    value_t v = obj_wrap((obj_t){ .kind = OBJ_ITER, .iter = iter }, a);
    if (v.obj.cell == NULL)
        iter_deinit(&iter, a);
    return v;
}

value_t value_init_closure(vm_t* function, const value_t* ups, int64_t n, const sv_allocator_t* a)
{
    closure_t cls = { .function = function, .upvalues = sv_vec_init(value_t) };
    if (n > 0) {
        cls.upvalues = (sv_vec_t(value_t))sv_vec_init_capacity(value_t, n, a);
        if (cls.upvalues.arr == NULL)
            return ERR_VALUE;
        cls.upvalues.size = n;
        for (int64_t i = 0; i < n; i++)
            cls.upvalues.arr[i] = value_borrow(ups[i]);
    }

    value_t v = obj_wrap((obj_t){ .kind = OBJ_CLOSURE, .closure = cls }, a);
    if (v.obj.cell == NULL)
        cls_deinit(&cls, a);
    return v;
}

value_t value_init_closure_member(sv_rc_t(closure_group_t) group, int64_t index, const sv_allocator_t* a)
{
    value_t v = obj_wrap(
        (obj_t){ .kind = OBJ_CLOSURE_MEMBER, .closure_member = { .group = group, .index = (size_t)index } },
        a);
    if (v.obj.cell == NULL)
        sv_rc_deinit(&group, a);
    return v;
}

value_t value_init_str(sv_str_t s, const sv_allocator_t* a)
{
    sv_str_t copy = sv_str_copy(s, a);
    if (copy.chars == NULL)
        return ERR_VALUE;

    value_t v = obj_wrap((obj_t){ .kind = OBJ_STR, .str = copy }, a);
    if (v.obj.cell == NULL)
        sv_str_deinit(&copy, a);
    return v;
}

value_t value_init_map(const value_t* vs, int64_t n_pairs, const sv_allocator_t* a)
{
    kv_t* kvs = NULL;
    if (n_pairs > 0) {
        kvs = sv_malloc(a, (size_t)n_pairs * sizeof(kv_t));
        if (kvs == NULL)
            return ERR_VALUE;
        for (int64_t i = 0; i < n_pairs; i++) {
            const value_t* pair = &vs[2 * (n_pairs - 1 - i)];
            kvs[i] = (kv_t){ .key = pair[0], .value = pair[1] };
        }
    }

    map_t map = map_init(kvs, n_pairs, a);
    if (kvs != NULL)
        sv_free(a, kvs);
    if (map.cell == NULL)
        return ERR_VALUE;

    value_t v = obj_wrap((obj_t){ .kind = OBJ_MAP, .map = map }, a);
    if (v.obj.cell == NULL)
        map_deinit(&map, a);
    return v;
}

static bool value_write(value_t v, sv_str_builder* b, const sv_allocator_t* a)
{
    switch (v.kind) {
        case VALUE_NIL: return sv_strb_add(b, "nil", 3, a) >= 0;
        case VALUE_BOOL:
            return v.boolean ? sv_strb_add(b, "true", 4, a) >= 0 : sv_strb_add(b, "false", 5, a) >= 0;
        case VALUE_NUMBER: {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%g", v.number);
            return sv_strb_add(b, buf, n, a) >= 0;
        }
        case VALUE_OBJ: break;
    }

    switch (v.obj.cell->value.kind) {
        case OBJ_STR:
            return sv_strb_add(b, v.obj.cell->value.str.chars, v.obj.cell->value.str.size, a) >= 0;
        case OBJ_LIST: {
            if (sv_strb_add_char(b, '[', a) < 0)
                return false;
            ll_iter_t iter = ll_iter_init(v.obj.cell->value.list);
            bool first = true;
            for (sv_opt_t(value_t) e = ll_iter_next(&iter); e.is_some; e = ll_iter_next(&iter)) {
                if ((!first && sv_strb_add(b, ", ", 2, a) < 0) || !value_write(e.value, b, a)) {
                    ll_iter_deinit(&iter, a);
                    return false;
                }
                first = false;
            }
            ll_iter_deinit(&iter, a);
            return sv_strb_add_char(b, ']', a) >= 0;
        }
        case OBJ_MAP: {
            if (sv_strb_add(b, "%{", 2, a) < 0)
                return false;
            map_iter_t it = map_iter_init(v.obj.cell->value.map);
            bool first = true;
            for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
                if ((!first && sv_strb_add(b, ", ", 2, a) < 0)
                    || !value_write(kv.value.key, b, a)
                    || sv_strb_add(b, ": ", 2, a) < 0
                    || !value_write(kv.value.value, b, a)) {
                    map_iter_deinit(&it, a);
                    return false;
                }
                first = false;
            }
            map_iter_deinit(&it, a);
            return sv_strb_add_char(b, '}', a) >= 0;
        }
        case OBJ_ITER:
            return sv_strb_add(b, "list iterator", 13, a) >= 0;
        case OBJ_CLOSURE: {
            sv_str_t name = cls_get_vm(v.obj.cell->value.closure).name;
            return sv_strb_add(b, name.chars, name.size, a) >= 0;
        }
        case OBJ_CLOSURE_MEMBER: {
            sv_str_t name = clsm_get_vm(v.obj.cell->value.closure_member).name;
            return sv_strb_add(b, name.chars, name.size, a) >= 0;
        }
    }
    return false;
}

sv_str_t value_to_str(value_t v, const sv_allocator_t* a)
{
    sv_str_builder b = sv_strb_init();
    if (!value_write(v, &b, a)) {
        sv_strb_deinit(&b, a);
        return sv_str_err();
    }
    return sv_strb_to_str(&b);
}
