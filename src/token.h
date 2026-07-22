#ifndef LONG_TOKEN_H
#define LONG_TOKEN_H

#include "std/allocator.h"

typedef struct {
    char c;
} token_t;


const char* token_format(token_t, sv_allocator_t*);

#endif
