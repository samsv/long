#include "error.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

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
    char buf[256];
    int at = snprintf(buf, sizeof(buf), ERROR_LINE_FMT, line);

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + at, sizeof(buf) - at, fmt, args);
    va_end(args);
    if (n < 0)
        return (error_t){ .error_code = code, .msg = sv_str_init("") };
    if (n >= (int)sizeof(buf) - at)
        n = sizeof(buf) - at - 1;

    sv_str_t msg = sv_str_copy((sv_str_t){ .chars = buf, .size = at + n }, a);
    return (error_t){ .error_code = code, .msg = msg.chars != NULL ? msg : sv_str_init("") };
}

void error_free(error_t* e, const sv_allocator_t* a)
{
    sv_str_deinit(&e->msg, a);
    e->msg = sv_str_init("");
}
