#include "std_native.h"

#include "value.h"
#include "std/allocator_std.h"
#include "obj/list.h"
#include "vm.h"
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#ifndef RAND_SEED_DEFINED
    static int initialized = false;
#else
    static int initialized = true;
#endif

value_t bad_arg_type_error(value_t v, value_kind expected_k, obj_kind expected_o, const vm_ctx_t* ctx)
{
    vm_wrong_type_err* paylod = sv_malloc(ctx->alloc, sizeof(vm_wrong_type_err));
    *paylod = (vm_wrong_type_err){
        .expected_o = expected_o,
            .expected_v = expected_k,
            .got = v,
    };

    error_t e = {
        .error_code = VM_ERR_WRONG_TYPE,
        .msg = sv_str_init("Wrong type of argument"),
        .payload = paylod,
    };
    return value_init_err(e, ctx->alloc);
}

value_t ntv_print(const value_t* values, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    value_t v = values[0];
    sv_str_t str = !IS_STR(v) ? value_to_str(v, ctx) : AS_STR(v);
    printf("%.*s", (int)str.size, str.chars);

    if (!IS_STR(v))
        sv_str_deinit(&str, &sv_gpa);

    return value_nil;
}

value_t ntv_println(const value_t* values, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    value_t ret = ntv_print(values, n, ctx);
    printf("\n");
    return ret;
}

value_t ntv_print_arr(const value_t* values, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    value_t maybe_list = values[0];
    if (!IS_LIST(maybe_list))
        return bad_arg_type_error(maybe_list, VALUE_OBJ, OBJ_LIST, ctx);

    ll_iter_t iter = ll_iter_init_no_borrow(AS_LIST(maybe_list));
    for (sv_opt_t(value_t) v = ll_iter_next(&iter); v.is_some; v = ll_iter_next(&iter)) {
        ntv_print(&v.value, 1, ctx);
    }

    return value_nil;
}

#define RAND(formula) do {                                                     \
    if (!initialized) {                                                        \
        srand(time(NULL));                                                     \
        initialized = true;                                                    \
    }                                                                          \
    assert(n == 2);                                                            \
    value_t min = args[0];                                                     \
    value_t max = args[1];                                                     \
    if (!IS_NUMBER(min))                                                       \
        return bad_arg_type_error(min, VALUE_NUMBER, OBJ_LIST, ctx);           \
    if (!IS_NUMBER(max))                                                       \
        return bad_arg_type_error(max, VALUE_NUMBER, OBJ_LIST, ctx);           \
    if (min.number > max.number)                                               \
        return value_nil;                                                      \
    double range = fabs(max.number - min.number);                              \
    double res = (formula);                                                    \
    return (value_t){ .number = res, .kind = VALUE_NUMBER };                   \
} while (0)

value_t ntv_randi(const value_t* args, uint8_t n, const vm_ctx_t* ctx)
{ RAND(rand() % (int)range + (int)min.number); }

value_t ntv_randf(const value_t* args, uint8_t n, const vm_ctx_t* ctx)
{ RAND((double)rand()/(double)(RAND_MAX/range) + min.number); }

#undef RAND
