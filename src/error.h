#ifndef SV_ERROR_H
#define SV_ERROR_H

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

#endif
