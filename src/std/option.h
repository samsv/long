#ifndef SV_OPTION_H
#define SV_OPTION_H

#include <stdbool.h>

#define opt_tt(type) type ## _opt_t
#define opt_t(type) opt_tt(type)

#define opt_def(type) typedef struct opt_t(type) {                                                            \
   type value;                                                                                                \
   bool is_some;                                                                                              \
} opt_t(type)

#define opt_some(v) { .value = (v), .is_some = true }
#define opt_none { .is_some = false }

#define opt_unwrap(opt) (opt).value
#endif
