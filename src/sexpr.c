#include "sexpr.h"
#include <stdlib.h>
#include <stdio.h>

void sexpr_free(sexpr_t* sexpr, const sv_allocator_t* gpa)
{
    switch (sexpr->tag) {
        case S_ATOM:
            break;
        case S_CONS:
            sv_vec_foreach(sexpr_t, s, &sexpr->cons) {
                sexpr_free(&s, gpa);
            }
            sv_vec_deinit(&sexpr->cons, gpa);
            break;
    }
}

const char* sexpr_format(sexpr_t sexpr, const sv_allocator_t* gpa)
{
    switch (sexpr.tag) {
        case S_ATOM:
            return token_format(sexpr.atom, gpa);
        case S_CONS:
            fprintf(stderr, "Not implemented");
            exit(0);
            return "";
    }
    return "";
}
