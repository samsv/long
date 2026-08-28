#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/compiler.h"
#include "src/debug.h"
#include "src/std/allocator_std.h"
#include "sexpr.h"
#include "parser.h"
#include "pattern_match.h"

static const char* sample = "1";
    /**
    "fun \n"
    "| is_even(x)\n"
        "| 0 do true\n"
        "| _ do is_odd(x - 1)\n"
    "| is_odd(x)\n"
        "| 0 do false\n"
        "| _ do \n"
            "new_x = x - 1\n"
            "is_even(new_x)\n"
    "end\n"
    "is_even(2)";
    */

static int run(ctx_t ctx, const char* code)
{
    vm_t vm = compile(code, &ctx);
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
        return 1;
    }
    print_vm(vm);

    vm_deinit(&vm, &ctx.alloc);
    return 0;
}

int main(void)
{
    (void)sample;

    ctx_t ctx = {
        .alloc = sv_gpa,
        .logger = sv_std_logger,
        .err = { 0 },
    };

    /**
    if (run(ctx, sample) > 1) {
        return 1;
    }
    */

    const char* psample =
        "match {a: 1, c:2}\n"
        "| {b: z, d: y} do 3\n"
        "| {a: z, ..} do z\n"
        "end";

    {
        scanner_t s = scanner_init(sv_str_init(psample));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sv_str_t og_fmt = sexpr_format(sexpr, &sv_gpa);
        printf("%.*s\n", (int)og_fmt.size, og_fmt.chars);

        sv_arena_t arena = sv_arena_init(1 << 16);
        sexpr_t ms = match_compile(sexpr, &ctx, &arena);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n", (int)fmt.size, fmt.chars);
        sv_arena_deinit(&arena);

        if (run(ctx, psample) > 1) {
            return 1;
        }
    }

    return 0;
}
