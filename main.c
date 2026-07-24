#define SV_IMPLEMENTATION
#include <inttypes.h>
#include <stdio.h>
#include "src/scanner.h"
#include "src/std/allocator_std.h"

static const char* sample =
    "fun add(x, y) do\n"
    "   x + y\n"
    "end\n"
    "\n"
    "add(1, 2.5) |> print\n"
    "nums = [1, 2, 3]\n"
    "msg = \"hello\nworld\"\n";

int main(void)
{
    ctx_t ctx = {
        .a = sv_gpa,
        .logger = sv_std_logger,
        .err = { 0 },
    };

    scanner_t s = scanner_init(sv_str_init(sample));
    for (;;) {
        token_t token = scanner_next(&s, &ctx);
        if (token.kind == TOKEN_EOF)
            break;

        if (token.kind == TOKEN_ERROR) {
            sv_log_error(&ctx.logger, "%.*s", (int)ctx.err.msg.size, ctx.err.msg.chars);
            sv_str_deinit(&ctx.err.msg, &ctx.a);
            return 1;
        }

        sv_str_t text = token_format(token, &ctx.a);
        if (text.size < 0)
            return 1;

        printf("line %" PRId64 ": %.*s\n", token.line, (int)text.size, text.chars);
        sv_str_deinit(&text, &ctx.a);
    }

    return 0;
}
