#ifndef LONG_CTX_H
#define LONG_CTX_H

#include "error.h"
#include "std/logger.h"
#include "std/allocator.h"
#include <setjmp.h>

/**
 * A context to be passed around `lóng` lang functions. Defines the allocator, logger and carries error information.
 */
typedef struct {
    sv_allocator_t alloc;
    sv_logger_t logger;
    error_t err;
    jmp_buf* on_error;  // where a compilation leaves on failure: 1 for out of memory, 2 when err is set
} ctx_t;

/**
 * Stores the error and leaves the compilation through on_error.
 */
_Noreturn void ctx_fail(ctx_t*, int code, const char* msg);
/**
 * As ctx_fail, with an out of memory message naming the line.
 */
_Noreturn void ctx_fail_oom(ctx_t*, int code, int64_t line);

#endif
