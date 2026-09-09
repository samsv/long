#ifndef SV_OPTION_H
#define SV_OPTION_H

#include <stdbool.h>

#define sv_opt_tt(type) type ## _opt_t
#define sv_opt_t(type) sv_opt_tt(type)

#define sv_opt_def(type) typedef struct sv_opt_t(type) {                                                      \
   type value;                                                                                                \
   bool is_some;                                                                                              \
} sv_opt_t(type)

#define sv_opt_some(v) { .value = (v), .is_some = true }
#define sv_opt_none { .is_some = false }

#define sv_opt_unwrap(opt) (opt).value

#define sv_opt_some_t(type, v) ((sv_opt_t(type))sv_opt_some(v))
#define sv_opt_none_t(type) ((sv_opt_t(type))sv_opt_none)
#endif
