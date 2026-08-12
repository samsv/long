#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/compiler.h"
#include "src/debug.h"
#include "src/std/allocator_std.h"
#include "sexpr.h"
#include "parser.h"
#include "pattern_match.h"
#include "pattern_match_2.h"

static const char* sample =
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
        "| [] do 0"
        "| [x, y] do x\n"
        "| [x, y, ..xs] do xs\n"
        "end";

    {
        scanner_t s = scanner_init(sv_str_init(psample));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sexpr_t ms = match_compile(sexpr, &ctx);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n\n", (int)fmt.size, fmt.chars);
    }

    {
        scanner_t s = scanner_init(sv_str_init(psample));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sexpr_t ms = match_compile_2(sexpr, &ctx);
        sv_str_t og_fmt = sexpr_format(sexpr, &sv_gpa);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n", (int)og_fmt.size, og_fmt.chars);
        printf("%.*s\n", (int)fmt.size, fmt.chars);
    }

    return 0;
}
