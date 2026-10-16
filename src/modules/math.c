#include "math.h"
#include "../value.h"
#include "../std_native.h"
#include <assert.h>
#include <math.h>

value_t ntvm_sin(const value_t* args, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    if (!IS_NUMBER(args[0]))
        return bad_arg_type_error(args[0], VALUE_NUMBER, OBJ_LIST, ctx);

    return (value_t){ .kind = VALUE_NUMBER, .number = sin(AS_NUMBER(args[0])) };
}

value_t ntvm_cos(const value_t* args, uint8_t n, const vm_ctx_t* ctx)
{
    assert(n == 1);

    if (!IS_NUMBER(args[0]))
        return bad_arg_type_error(args[0], VALUE_NUMBER, OBJ_LIST, ctx);

    return (value_t){ .kind = VALUE_NUMBER, .number = cos(AS_NUMBER(args[0])) };
}

