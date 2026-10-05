#include "error.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

static error_t error_oom = {
    .error_code = -1,
    .msg = { .chars = "Out of memory", .size = 13 },
};

error_t error_init(void)
{
    return (error_t){
        .error_code = 0,
        .msg = sv_str_init(""),
    };
}

void error_reset(error_t* e)
{
    e->error_code = 0;
    e->msg = sv_str_init("");
}

bool error_set(error_t* e, int code, const char* msg, const sv_allocator_t* a)
{
    e->error_code = code;
    e->msg = sv_str_copy(sv_str_init(msg), a);
    return false;
}

bool error_set_oom(error_t* e, int code, int64_t line, const sv_allocator_t* a)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Out of memory at line %" PRId64, line);
    return error_set(e, code, msg, a);
}

error_t error_fmt(int code, int64_t line, const sv_allocator_t* a, const char* fmt, ...)
{
    int bufsize = sizeof(char) * 256;
    char* buf = sv_malloc(a, bufsize);
    if (buf == NULL)
        return error_oom;
    int at = snprintf(buf, bufsize, ERROR_LINE_FMT, line);

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + at, bufsize - at, fmt, args);
    va_end(args);
    if (n < 0)
        return (error_t){ .error_code = code, .msg = sv_str_init("") };

    sv_str_t msg = sv_str_init(buf);
    return (error_t){ .error_code = code, .msg = msg };
}

void error_free(error_t* e, const sv_allocator_t* a)
{
    if (e->error_code < 0)
        return;
    sv_str_deinit(&e->msg, a);
    e->msg = sv_str_init("");
}
