#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        case VALUE_UNDEFINED:
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

static bool map_eql(hashmap_t x, hashmap_t y)
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
        case VALUE_UNDEFINED: return false;
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
                case OBJ_RECORD: return record_eql(ox->record, oy->record);
                case OBJ_TUPLE: return tuple_eql(ox->tuple, oy->tuple);
                case OBJ_NATIVE_FN:
                case OBJ_ITER:
                case OBJ_CLOSURE:
                case OBJ_CLOSURE_MEMBER:
                case OBJ_ERR: // maybe errors should have structural equality
                    return ox == oy;
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
        case OBJ_RECORD: record_deinit(&o->record, a); break;
        case OBJ_TUPLE: tuple_deinit(&o->tuple, a); break;
        case OBJ_CLOSURE_MEMBER: clsm_deinit(&o->closure_member, a); break;
        case OBJ_ERR: {
            error_t err = o->err;
            if (err.payload != NULL)
                sv_free(a, err.payload);
            break;
        }
        case OBJ_NATIVE_FN: break;
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

value_t value_wrap_list(list_t list, const sv_allocator_t* a)
{
    return obj_wrap((obj_t){ .kind = OBJ_LIST, .list = list }, a);
}

value_t value_init_err(error_t err, const sv_allocator_t* a)
{
    return obj_wrap((obj_t){ .kind = OBJ_ERR, .err = err }, a);
}

value_t value_init_native(native_fn_t fn, const sv_allocator_t* a)
{
    return obj_wrap((obj_t){ .kind = OBJ_NATIVE_FN, .fn = fn }, a);
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

value_t value_init_str_own(sv_str_t s, const sv_allocator_t* a)
{
    value_t v = obj_wrap((obj_t){ .kind = OBJ_STR, .str = s }, a);
    if (v.obj.cell == NULL)
        sv_str_deinit(&s, a);
    return v;
}

value_t value_init_str(sv_str_t s, const sv_allocator_t* a)
{
    sv_str_t copy = sv_str_copy(s, a);
    if (copy.chars == NULL)
        return ERR_VALUE;
    return value_init_str_own(copy, a);
}

value_t value_init_map(const value_t* vs, int64_t n_pairs, const sv_allocator_t* a)
{
    hashmap_t map = map_init(vs, n_pairs * 2, a);
    if (map.cell == NULL)
        return ERR_VALUE;

    value_t v = obj_wrap((obj_t){ .kind = OBJ_MAP, .map = map }, a);
    if (v.obj.cell == NULL)
        map_deinit(&map, a);
    return v;
}

value_t value_init_record(const value_t* vs, uint8_t n, const sv_allocator_t* a)
{
    value_t v = obj_wrap((obj_t){ .kind = OBJ_RECORD, .record = {0} }, a);
    if (v.obj.cell == NULL)
        return ERR_VALUE;

    record_t record = record_init(vs, n, a);
    if (record.items == NULL) {
        value_free(&v, a);
        return ERR_VALUE;
    }

    v.obj.cell->value.record = record;
    return v;
}

value_t value_init_tuple(const value_t* vs, uint8_t n, const sv_allocator_t* a)
{
    value_t v = obj_wrap((obj_t){ .kind = OBJ_TUPLE, .tuple = {0} }, a);
    if (v.obj.cell == NULL)
        return ERR_VALUE;

    tuple_t tuple = tuple_init(vs, n, a);
    if (tuple.items == NULL) {
        value_free(&v, a);
        return ERR_VALUE;
    }

    v.obj.cell->value.tuple = tuple;
    return v;
}

const char* value_kind_str(value_kind v_kind, obj_kind o_kind)
{
    switch (v_kind) {
        case VALUE_UNDEFINED: return "undefined";
        case VALUE_BOOL: return "bool";
        case VALUE_NIL: return "nil";
        case VALUE_NUMBER: return "number";
        case VALUE_OBJ: switch (o_kind) {
            case OBJ_ITER: return "iter";
            case OBJ_ERR: return "error";
            case OBJ_STR: return "string";
            case OBJ_LIST: return "list";
            case OBJ_MAP: return "hashmap";
            case OBJ_RECORD: return "record";
            case OBJ_TUPLE: return "tuple";
            case OBJ_NATIVE_FN:
            case OBJ_CLOSURE:
            case OBJ_CLOSURE_MEMBER:
                return "function";
        }
    }
    return "";
}

static bool value_write(value_t v, sv_str_builder* b, const vm_ctx_t* ctx)
{
    const sv_allocator_t* a = ctx->alloc;
#define CHECK(expr) if (!(expr)) return false
    switch (v.kind) {
        case VALUE_NIL: return sv_strb_add(b, "nil", 3, a) >= 0;
        case VALUE_UNDEFINED: return sv_strb_add(b, "undefined", 3, a) >= 0;
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
            CHECK(sv_strb_add_char(b, '[', a) >= 0);
            ll_iter_t iter = ll_iter_init_no_borrow(v.obj.cell->value.list);
            bool first = true;
            for (sv_opt_t(value_t) e = ll_iter_next(&iter); e.is_some; e = ll_iter_next(&iter)) {
                if ((!first && sv_strb_add(b, ", ", 2, a) < 0) || !value_write(e.value, b, ctx)) {
                    return false;
                }
                first = false;
            }
            ll_iter_deinit(&iter, a);
            return sv_strb_add_char(b, ']', a) >= 0;
        }
        case OBJ_MAP: {
            CHECK(sv_strb_add(b, "%{", 2, a) >= 0);
            map_iter_t it = map_iter_init_no_borrow(v.obj.cell->value.map);
            bool first = true;
            for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
                if ((!first && sv_strb_add(b, ", ", 2, a) < 0)
                    || !value_write(kv.value.key, b, ctx)
                    || sv_strb_add(b, ": ", 2, a) < 0
                    || !value_write(kv.value.value, b, ctx)
                ) {
                    return false;
                }
                first = false;
            }
            map_iter_deinit(&it, a);
            return sv_strb_add_char(b, '}', a) >= 0;
        }
        case OBJ_TUPLE: {
            CHECK(sv_strb_add(b, "(", 1, a) >= 0);

            tuple_t tuple = AS_TUPLE(v);
            for (uint8_t i = 0; i < tuple.size; i++) {
                if (i > 0)
                    CHECK(sv_strb_add(b, ", ", 2, a) >= 0);
                CHECK(value_write(tuple.items[i], b, ctx));
            }

            if (tuple.size == 1)
                CHECK(sv_strb_add(b, ",", 1, a) >= 0);
            return sv_strb_add(b, ")", 1, a) >= 0;
        }
        case OBJ_ITER:
            switch (AS_ITER(v).kind) {
                case ITER_LIST: return sv_strb_add(b, "list iterator", 13, a) >= 0;
                case ITER_STR: return sv_strb_add(b, "string iterator", 15, a) >= 0;
            }
            return false;
        case OBJ_CLOSURE: {
            sv_str_t name = cls_get_vm(v.obj.cell->value.closure).name;
            return sv_strb_add(b, name.chars, name.size, a) >= 0;
        }
        case OBJ_CLOSURE_MEMBER: {
            sv_str_t name = clsm_get_vm(v.obj.cell->value.closure_member).name;
            return sv_strb_add(b, name.chars, name.size, a) >= 0;
        }
        case OBJ_NATIVE_FN: {
            const char* name = v.obj.cell->value.fn.name;
            return sv_strb_add(b, name, strlen(name), a) >= 0;
        }
        case OBJ_RECORD: {
            CHECK(sv_strb_add(b, "{", 1, a) >= 0);

            record_t tuple = AS_RECORD(v);
            for (uint8_t i = 0; i < tuple.size; i++) {
                record_item_t item = tuple.items[i];
                if (i > 0)
                    CHECK(sv_strb_add(b, ", ", 2, a) >= 0);

                const char* name = ctx->record_key_names[item.id];
                CHECK(sv_strb_add(b, name, (int64_t)strlen(name), a) >= 0);
                CHECK(sv_strb_add(b, ": ", 2, a) >= 0);
                CHECK(value_write(item.value, b, ctx));
            }

            return sv_strb_add(b, "}", 1, a) >= 0;
        }
        case OBJ_ERR: {
            error_t e = AS_ERR(v);
            CHECK(sv_strb_add(b, e.msg.chars, e.msg.size, a) >= 0);

            vm_err_t* vm_err = e.payload;
            char buffer[64];
            int writen = sprintf(buffer, " at line %ld", vm_err->line);
            CHECK(sv_strb_add(b, buffer, writen, a) >= 0);

            switch (e.error_code) {
                case VM_ERR_WRONG_TYPE: {
                    // TODO: Make this a vtable method
                    vm_wrong_type_err* payload = e.payload;
                    CHECK(sv_strb_add(b, "got ", strlen("got "), a) >= 0);
                    CHECK(value_write(payload->got, b, ctx));
                    const char* rcv = value_kind_str(payload->expected_v, payload->expected_o);
                    return sv_strb_add(b, rcv, strlen(rcv), a) >= 0;
                }
            }
            return true;
#undef CHECK
        }
    }
    return false;
}

sv_str_t value_to_str(value_t v, const vm_ctx_t* ctx)
{
    sv_str_builder b = sv_strb_init();
    if (!value_write(v, &b, ctx)) {
        sv_strb_deinit(&b, ctx->alloc);
        return sv_str_err();
    }
    return sv_strb_to_str(&b);
}

