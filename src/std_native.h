#ifndef LONG_STD_NATIVE_H
#define LONG_STD_NATIVE_H

#include <stdint.h>
#include "common.h"

/**
 * Lóng lang standard library native functions.
 */

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

#endif
