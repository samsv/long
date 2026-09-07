#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/compiler.h"
#include "src/debug.h"
#include "src/std/allocator_std.h"

static int run(vm_t vm, ctx_t ctx)
{
    if (ctx.err.error_code != 0) {
        if (ctx.err.msg.size > 0)
            sv_log_error(&ctx.logger, "%.*s", (int)ctx.err.msg.size, ctx.err.msg.chars);
        sv_str_deinit(&ctx.err.msg, &ctx.alloc);
        return 1;
    }

    sv_opt_t(error_t) err = vm_run(&vm);
    if (err.is_some) {
        sv_log_error(&ctx.logger, "%.*s", (int)err.value.msg.size, err.value.msg.chars);
        vm_err_deinit(&err.value, &ctx.alloc);
        vm_deinit(&vm, &ctx.alloc);
        return 1;
    }
    print_vm(vm);

    vm_deinit(&vm, &ctx.alloc);
    return 0;
}

static int run_file(const char* path)
{
    ctx_t ctx = {
        .alloc = sv_gpa,
        .logger = sv_std_logger,
        .err = { 0 },
    };

    const char* source = read_file(path, &ctx.alloc);
    if (source == NULL) {
        return 1;
    }

    vm_t vm = compile(path, source, &ctx);
    free((void*)source);

    int ret = run(vm, ctx);
    if (ret > 0) {
        return ret;
    }

    return 0;
}

int main(int argc, const char** argv)
{
    if (argc != 2) {
        printf("Usage: ./long $file-path\n");
        return 0;
    }

    return run_file(argv[1]);
}
