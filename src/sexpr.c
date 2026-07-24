#include "sexpr.h"

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
                    CHECK(sv_strb_add_char(b, ',', a) != -1);
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
