#include "error.h"

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
