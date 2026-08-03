#ifndef SV_ERROR_H
#define SV_ERROR_H

#include "std/allocator.h"
#include "std/string.h"
#include "std/option.h"

/**
 * A custom default error type to store error information.
 */
typedef struct {
    int error_code;
    sv_str_t msg;
    void* payload; // Optional payload to give more information about the error.
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
 */
bool error_set_oom(error_t*, int code, int64_t line, const sv_allocator_t*);

#endif
