#include "sexpr.h"

sexpr_t atom_sexpr(token_t t)
{
    return (sexpr_t){ .tag = S_ATOM, .atom = t };
}

sexpr_t cons_sexpr(sv_vec_t(sexpr_t) list)
{
    return (sexpr_t){ .tag = S_CONS, .cons = list };
}

static bool sexpr_format_builder(sexpr_t sexpr, sv_str_builder* b, int ident, const sv_allocator_t* a)
{
#define CHECK(...) if (!(__VA_ARGS__)) return false
    switch (sexpr.tag) {
        case S_ATOM:
            return token_format_builder(sexpr.atom, b, a);
        case S_CONS:
            if (ident > 0)
                CHECK(sv_strb_add_char(b, '\n', a) != -1);
            for (int i = 0; i < ident; i++)
                CHECK(sv_strb_add(b, "  ", 2, a) != -1);
            CHECK(sv_strb_add_char(b, '(', a) != -1);

            sv_vec_t(sexpr_t) cons = sexpr.cons;
            for (int64_t i = 0; i < cons.size; i++) {
                if (i != 0) {
                    CHECK(sv_strb_add_char(b, ' ', a) != -1);
                }
                sexpr_t s = cons.arr[i];
                CHECK(sexpr_format_builder(s, b, ident + 1, a));
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
    if (!sexpr_format_builder(sexpr, &b, 0, a)) {
        sv_strb_deinit(&b, a);
        return sv_str_err();
    }

    return sv_strb_to_str(&b);
}
