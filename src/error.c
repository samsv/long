#include "error.h"
#include <inttypes.h>
#include <stdio.h>

error_t error_init(void)
{
    return (error_t){
        .error_code = 0,
        .msg = sv_str_init(""),
        .payload = NULL,
    };
}

void error_reset(error_t* e)
{
    e->error_code = 0;
    e->msg = sv_str_init("");
    e->payload = NULL;
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
