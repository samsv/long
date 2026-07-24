#ifndef SV_CTX_H
#define SV_CTX_H

#include "error.h"
#include "std/logger.h"
#include "std/allocator.h"

/**
 * A context to be passed around `lóng` lang functions. Defines the allocator, logger and carries error information.
 */
typedef struct {
    sv_allocator_t a;
    sv_logger_t logger;
    error_t err;
} ctx_t;

#endif
