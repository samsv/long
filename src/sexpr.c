#include "sexpr.h"

sexpr_t atom_sexpr(token_t t)
{
    return (sexpr_t){ .tag = S_ATOM, .atom = t };
}

sexpr_t cons_sexpr(sv_vec_t(sexpr_t) list)
{
    return (sexpr_t){ .tag = S_CONS, .cons = list };
}

bool is_error_sexpr(sexpr_t e)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_ERROR;
}

void sexpr_free(sexpr_t* sexpr, const sv_allocator_t* a)
{
    switch (sexpr->tag) {
        case S_ATOM:
            break;
        case S_CONS:
            sv_vec_foreach(sexpr_t, s, &sexpr->cons) {
                sexpr_free(&s, a);
            }
            sv_vec_deinit(&sexpr->cons, a);
            break;
    }
}

bool sexpr_clone(sexpr_t* out, sexpr_t src, const sv_allocator_t* a)
{
    if (src.tag == S_ATOM) {
        *out = src;
        return true;
    }

    sv_vec_t(sexpr_t) list = sv_vec_init_capacity(sexpr_t, src.cons.size, a);
    if (list.arr == NULL)
        return false;

    for (int64_t i = 0; i < src.cons.size; i++) {
        sexpr_t child = { 0 };
        if (!sexpr_clone(&child, src.cons.arr[i], a)) {
            sexpr_t partial = cons_sexpr(list);
            sexpr_free(&partial, a);
            return false;
        }
        list.arr[list.size++] = child;
    }

    *out = cons_sexpr(list);
    return true;
}

static bool sexpr_format_builder(sexpr_t sexpr, sv_str_builder* b, const sv_allocator_t* a)
{
#define CHECK(...) if (!(__VA_ARGS__)) return false
    switch (sexpr.tag) {
        case S_ATOM:
            return token_format_builder(sexpr.atom, b, a);
        case S_CONS:
            CHECK(sv_strb_add_char(b, '(', a) != -1);

            sv_vec_t(sexpr_t) cons = sexpr.cons;
            for (int64_t i = 0; i < cons.size; i++) {
                if (i != 0) {
                    CHECK(sv_strb_add_char(b, ' ', a) != -1);
                }
                sexpr_t s = cons.arr[i];
                CHECK(sexpr_format_builder(s, b, a));
            }

            return sv_strb_add_char(b, ')', a) != -1;
        default:
            // unreacheable
            return false;
    }
#undef CHECK
}

sv_str_t sexpr_format(sexpr_t sexpr, const sv_allocator_t* a)
{
    sv_str_builder b = sv_strb_init();
    if (!sexpr_format_builder(sexpr, &b, a)) {
        sv_strb_deinit(&b, a);
        return sv_str_err();
    }

    return sv_strb_to_str(&b);
}
