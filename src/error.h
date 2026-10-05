#ifndef SV_ERROR_H
#define SV_ERROR_H

#include <inttypes.h>
#include "std/allocator.h"
#include "std/string.h"
#include "std/option.h"

/* How a message names its line; the formatted text follows. */
#define ERROR_LINE_FMT "Line %" PRId64 ": "

/**
 * A custom default error type to store error information.
 */
typedef struct {
    int error_code;
    sv_str_t msg; // always owned, see error_free
} error_t;

sv_opt_def(error_t);

/**
 * A 0 initilized error.
 */
error_t error_init(void);
/**
 * Resets the error to a default state. It does not free any allocations.
 */
void error_reset(error_t*);

/**
 * Stores the code and a copy of the message in the error. Always returns false
 * so callers can `return error_set(...)`.
 */
bool error_set(error_t*, int code, const char* msg, const sv_allocator_t*);
/**
 * Stores an out of memory error naming the line. Always returns false.
 *
 * Note: the fact that an OOM error allocates is quite odd. We should fix this in the future.
 */
bool error_set_oom(error_t*, int code, int64_t line, const sv_allocator_t*);
/**
 * Builds an error whose message is `Line N: ` followed by the formatted text, printf
 * style. Text past 255 bytes is cut. The message is empty when it cannot be allocated.
 */
error_t error_fmt(int code, int64_t line, const sv_allocator_t*, const char* fmt, ...);
/**
 * Frees the message and leaves the error empty, so freeing twice is harmless.
 */
void error_free(error_t*, const sv_allocator_t*);

#endif
