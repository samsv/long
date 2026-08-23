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

    sv_opt_t(error_t) err = vm_run(&vm);
    print_vm(vm);
    if (err.is_some) {
        sv_log_error(&ctx.logger, "%.*s", (int)err.value.msg.size, err.value.msg.chars);
        vm_err_deinit(&err.value, &ctx.alloc);
    }

    vm_deinit(&vm, &ctx.alloc);

    const char* psample =
        "match x\n"
        "| %{ 0: x, \"y\": 0 } do x\n"
        "| %{ \"x\": 0, \"y\":y } do y\n"
        "| %{ \"x\": x, \"y\": y } do x * y\n"
        "end";

    {
        sv_arena_t arena = sv_arena_init(1 << 16);
        scanner_t s = scanner_init(sv_str_init(psample));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sv_str_t og_fmt = sexpr_format(sexpr, &sv_gpa);
        printf("%.*s\n", (int)og_fmt.size, og_fmt.chars);

        sexpr_t ms = match_compile(sexpr, &ctx, &arena);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n", (int)fmt.size, fmt.chars);
        sv_arena_deinit(&arena);
    }

    return 0;
}
