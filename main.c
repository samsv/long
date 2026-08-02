#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/compiler.h"
#include "src/debug.h"
#include "src/std/allocator_std.h"
#include "src/scanner.h"
#include "src/parser.h"

static const char* sample =
    "x = {x: 1, y: 2}\n"
    "y = {y: 4, x: 3}\n"
    "println(x)\n"
    "println(y)\n";

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

    // test match expr
    const char* match_code =
        "match (a, b)\n"
        "| (false, y) = y\n"
        "| (true, true) = false\n"
        "| [x, ..xs] = true\n"
        "| (x, x) = true\n"
        "| 0 = true\n"
        "| 1 = true\n"
        "| \"hello\" = true\n"
        "| {x: a, y: b} = true\n"
        "| {x: a, y: b, ..} = true\n"
        "| _ = true\n"
        "end"
        ;

    scanner_t s = scanner_init(sv_str_init(match_code));
    sexpr_t sexpr = parser_expr(&s, &ctx);
    sv_str_t str = sexpr_format(sexpr, &sv_gpa);
    printf("\n\nmatch expr: %.*s\n", (int)str.size, str.chars);

    return 0;
}
