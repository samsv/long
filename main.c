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

#define P_SIZE 5
char* psamples[P_SIZE];

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

psamples[0] =
    "match x\n"
    "| 1 do x\n"
    "| 2 do 2 * x\n"
    "| 3 do 9 * x\n"
    "end";

psamples[1] =
    "match x\n"
    "| 1 do 1\n"
    "| 2 do 2 * x\n"
    "| a do a\n"
    "end";

psamples[2] =
    "match x\n"
    "| 1 do 1\n"
    "| a do a\n"
    "| 2 do 2 * x\n"
    "end";

psamples[3] =
    "match x \n"
    "| 1 do 5\n"
    "|\"hello\" do 1\n"
    "| 2 do 2\n"
    "|\"world\" do 1\n"
    "end";

psamples[4] =
    "match x \n"
    "| (1, 2) do 1\n"
    "| (1, 2, 3) do 2\n"
    "| (1, 4) do 3\n"
    "end";

    for (int i = 0; i < P_SIZE; i++) {
        char* psample = psamples[i];
        scanner_t s = scanner_init(sv_str_init(psample));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sexpr_t ms = match_compile(sexpr, &ctx);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n", (int)fmt.size, fmt.chars);
    }

    {
        scanner_t s = scanner_init(sv_str_init(psamples[4]));
        sexpr_t sexpr = parser_expr(&s, &ctx);
        sexpr_t ms = match_compile_2(sexpr, &ctx);
        sv_str_t og_expr = sexpr_format(sexpr, &sv_gpa);
        sv_str_t fmt = sexpr_format(ms, &sv_gpa);
        printf("%.*s\n", (int)og_expr.size, og_expr.chars);
        printf("%.*s\n", (int)fmt.size, fmt.chars);
    }

    return 0;
}
