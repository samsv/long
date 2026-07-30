#include "std_native.h"

#include "value.h"
#include "std/allocator_std.h"
#include "obj/list.h"
#include "vm.h"
#include <stdio.h>
#include <assert.h>


value_t ntv_print_value(const value_t* values, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    value_t v = values[0];
    sv_str_t str = !IS_STR(v) ? value_to_str(v, ctx) : AS_STR(v);
    printf("%.*s", (int)str.size, str.chars);

    if (!IS_STR(v))
        sv_str_deinit(&str, &sv_gpa);

    return value_nil;
}

value_t ntv_print_value_arr(const value_t* values, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    value_t maybe_list = values[0];
    if (!IS_LIST(maybe_list)) {
        vm_wrong_type_err* paylod = sv_malloc(ctx->alloc, sizeof(vm_wrong_type_err));
        *paylod = (vm_wrong_type_err){
            .expected_o = OBJ_LIST,
            .expected_v = VALUE_OBJ,
            .got = maybe_list,
        };

        error_t e = {
            .error_code = VM_ERR_WRONG_TYPE,
            .msg = sv_str_init("Wrong type of argument"),
            .payload = paylod,
        };
        return value_init_err(e, ctx->alloc);
    }

    ll_iter_t iter = ll_iter_init_no_borrow(AS_LIST(maybe_list));
    for (sv_opt_t(value_t) v = ll_iter_next(&iter); v.is_some; v = ll_iter_next(&iter)) {
        ntv_print_value(&v.value, 1, ctx);
    }

    return value_nil;
}

