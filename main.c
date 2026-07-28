#define SV_IMPLEMENTATION
#include <stdio.h>
#include "src/compiler.h"
#include "src/std/allocator_std.h"

static const char* sample =
    "fun add(x, y) =\n"
    "   x + y\n"
    "end\n"
    "\n"
    "add(1, 2.5)\n";

int main(void)
{
    ctx_t ctx = {
        .alloc = sv_gpa,
        .logger = sv_std_logger,
        .err = { 0 },
    };

    vm_t vm = compile(sample, &ctx);
    if (ctx.err.error_code != 0) {
        if (ctx.err.msg.size > 0)
            sv_log_error(&ctx.logger, "%.*s", (int)ctx.err.msg.size, ctx.err.msg.chars);
        sv_str_deinit(&ctx.err.msg, &ctx.alloc);
        return 1;
    }

    printf("compiled %lld bytes of bytecode\n", (long long)vm.chunk.bytecode.size);
    vm_deinit(&vm, &ctx.alloc);

    return 0;
}
