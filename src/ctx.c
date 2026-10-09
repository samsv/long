#include "ctx.h"

_Noreturn void ctx_fail(ctx_t* ctx, int code, const char* msg)
{
    error_set(&ctx->err, code, msg, &ctx->alloc);
    longjmp(*ctx->on_error, 2);
}

_Noreturn void ctx_fail_oom(ctx_t* ctx, int code, int64_t line)
{
    error_set_oom(&ctx->err, code, line, &ctx->alloc);
    longjmp(*ctx->on_error, 2);
}
