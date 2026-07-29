#ifndef LONG_NATIVE_H
#define LONG_NATIVE_H

#include <stdint.h>
#include "../common.h"
#include "../std/allocator.h"

typedef struct {
    value_t(*fn)(const value_t*, uint8_t, const sv_allocator_t*);
    uint8_t arity;
    const char* name;
} native_fn_t;

#endif
