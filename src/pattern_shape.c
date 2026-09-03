#include "pattern_shape.h"
#include "token.h"

typedef sv_vec_t(sexpr_t) cons_t;

bool list_has_tail(sexpr_t list)
{
    sexpr_t last = list.cons.arr[list.cons.size - 1];
    return last.tag == S_CONS && last.cons.arr[0].atom.kind == TOKEN_DOT_DOT;
}

int64_t list_n_fixed(sexpr_t list)
{
    return list.cons.size - 1 - (list_has_tail(list) ? 1 : 0);
}

static bool is_dot_dot(cons_t cons)
{
    sexpr_t last = cons.arr[cons.size - 1];
    return last.tag == S_ATOM && last.atom.kind == TOKEN_DOT_DOT;
}

bool record_is_open(sexpr_t rec)
{
    return is_dot_dot(rec.cons);
}

bool hashmap_is_open(sexpr_t map)
{
    return is_dot_dot(map.cons);
}

int64_t hashmap_n_keys(sexpr_t map)
{
    return (map.cons.size - 1 - (hashmap_is_open(map) ? 1 : 0)) / 2;
}

int64_t record_n_fields(sexpr_t rec)
{
    int64_t n = rec.cons.size - 1 - (record_is_open(rec) ? 1 : 0);
    return n / 2;
}

bool pattern_is_name(sexpr_t e)
{
    return e.tag == S_ATOM
        && e.atom.kind == TOKEN_LITERAL
        && e.atom.literal.kind == LITERAL_IDENTIFIER;
}

bool pattern_is_alias(sexpr_t e)
{
    return e.tag == S_CONS && e.cons.size == 3
        && e.cons.arr[0].tag == S_ATOM
        && e.cons.arr[0].atom.kind == TOKEN_OPERATOR
        && e.cons.arr[0].atom.operator == OPERATOR_EQUAL;
}

sexpr_t alias_name(sexpr_t alias)
{
    return pattern_is_name(alias.cons.arr[2]) ? alias.cons.arr[2] : alias.cons.arr[1];
}

sexpr_t alias_pattern(sexpr_t alias)
{
    return pattern_is_name(alias.cons.arr[2]) ? alias.cons.arr[1] : alias.cons.arr[2];
}
