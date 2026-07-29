#ifndef LONG_STD_NATIVE_H
#define LONG_STD_NATIVE_H

#include <stdint.h>
#include "common.h"
#include "std/allocator.h"

/**
 * Lóng lang standard library native functions.
 */

/**
 * Prints one value to stdout.
 */
value_t ntv_print_value(const value_t*, uint8_t, const sv_allocator_t*);
/**
 * Prints every value in the array to stdout.
 */
value_t ntv_print_value_arr(const value_t*, uint8_t, const sv_allocator_t*);

#endif
