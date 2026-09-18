/**
 * Lóng lang standard library native functions.
 */

#ifndef LONG_STD_NATIVE_H
#define LONG_STD_NATIVE_H

#include <stdint.h>
#include "common.h"

/**
 * Prints one value to stdout.
 */
value_t ntv_print(const value_t*, uint8_t, const vm_ctx_t*);
/**
 * Prints one value to stdout and adds a new line.
 */
value_t ntv_println(const value_t*, uint8_t, const vm_ctx_t*);
/**
 * Prints every value in the array to stdout.
 */
value_t ntv_print_arr(const value_t*, uint8_t, const vm_ctx_t*);
/**
 * Returns a random floating point number between min (the first value, inclusive) and max (the second value, inclusive).
 */
value_t ntv_randf(const value_t*, uint8_t, const vm_ctx_t*);
/**
 * Returns a random integer between min (the first value, inclusive) and max (the second value, inclusive).
 * This seeds srand with time(NULL). Define RAND_SEED_DEFINED to skip srand initialization.
 */
value_t ntv_randi(const value_t*, uint8_t, const vm_ctx_t*);

#endif
