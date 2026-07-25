#define SV_IMPLEMENTATION
#include <stdio.h>
#include "src/parser.h"
#include "src/std/allocator_std.h"
#include "src/std/rc.h"

static const char* sample =
    "fun add(x, y) =\n"
    "   x + y\n"
    "end\n"
    "\n"
    "add(1, 2.5) |> print()\n"
    "nums = [1, 2, 3]\n"
    "msg = \"hello\nworld\"\n";

sv_rc_def(sv_str_t);

void f(void)
{
   char* c = sv_malloc(&sv_gpa, sizeof("hello"));
   memcpy(c, "hello", sizeof("hello"));
   sv_str_t s = sv_str_init(c);
   sv_rc_t(sv_str_t) rc = rc_init(sv_str_t, s, sv_str_deinit, &sv_gpa);
   sv_rc_t(sv_str_t) rc2 = rc_borrow(rc);

   sv_str_t* ms = rc_get(rc);
   printf("%.*s", (int)ms->size, ms->chars);

   rc_deinit(&rc, &sv_gpa);
   rc_deinit(&rc2, &sv_gpa);
}

int main(void)
{
    ctx_t ctx = {
        .a = sv_gpa,
        .logger = sv_std_logger,
        .err = { 0 },
    };

    scanner_t s = scanner_init(sv_str_init(sample));
    sexpr_t program = parser_program(&s, &ctx);
    if (ctx.err.error_code != 0) {
        if (ctx.err.msg.size > 0)
            sv_log_error(&ctx.logger, "%.*s", (int)ctx.err.msg.size, ctx.err.msg.chars);
        sv_str_deinit(&ctx.err.msg, &ctx.a);
        return 1;
    }

    sv_str_t text = sexpr_format(program, &ctx.a);
    sexpr_free(&program, &ctx.a);
    if (text.size < 0)
        return 1;

    printf("%.*s\n", (int)text.size, text.chars);
    sv_str_deinit(&text, &ctx.a);

    f();
    return 0;
}
