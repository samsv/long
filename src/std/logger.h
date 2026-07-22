#ifndef SV_LOGGER_H
#define SV_LOGGER_H

/**
 * A simple logger interface. The logger interface has the following functions:
 *
typedef struct {
    sv_log_fn info;
    sv_log_fn debug;
    sv_log_fn warning;
    sv_log_fn error;
    sv_log_fn success;
} sv_logger_t;

 * Where sv_log_fn has fprintf's signature and returns the total characters
 * written, or -1 on failure. Every message is terminated with a newline.
 * A default logger is provided in sv_std_logger; exactly one translation unit
 * must define SV_IMPLEMENTATION to define it. If you pass a NULL file to the
 * std_logger it will write to stdout (info, debug, warn and success) or to
 * stderr (error). std_logger.debug is a no op if NDEBUG is set. Colors are
 * disabled when the NO_COLOR environment variable is set.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define SV_LOG_NORMAL "\x1B[0m"
#define SV_LOG_RED "\x1B[31m"
#define SV_LOG_GREEN "\x1B[32m"
#define SV_LOG_YELLOW "\x1B[33m"

typedef int (*sv_log_fn)(FILE*, const char* format, ...);

typedef struct {
    sv_log_fn info;
    sv_log_fn debug;
    sv_log_fn warning;
    sv_log_fn error;
    sv_log_fn success;
} sv_logger_t;

static inline int sv_log_colors_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1) enabled = getenv("NO_COLOR") == NULL;
    return enabled;
}

static inline int sv_log_color(FILE* f, const char* color, const char* format, va_list args)
{
    const char* reset = SV_LOG_NORMAL;
    if (!sv_log_colors_enabled()) {
        color = "";
        reset = "";
    }

    int a = fprintf(f, "%s", color);
    int b = vfprintf(f, format, args);
    int c = fprintf(f, "%s\n", reset);
    if (a < 0 || b < 0 || c < 0) return -1;
    return a + b + c;
}

#define LOG_FN_NAME(fn_name) sv_log_ ##fn_name
#define CREATE_LOG_FN(fn_name, color, default_file)\
static inline int LOG_FN_NAME(fn_name) (FILE* f, const char* format, ...) {\
    va_list args;\
    va_start (args, format);\
    if (f == NULL) f = default_file;\
    int ret = sv_log_color(f, color, format, args);\
    va_end(args);\
    return ret;\
}

CREATE_LOG_FN(success, SV_LOG_GREEN, stdout)
CREATE_LOG_FN(warn, SV_LOG_YELLOW, stdout)
CREATE_LOG_FN(err, SV_LOG_RED, stderr)
CREATE_LOG_FN(info, SV_LOG_NORMAL, stdout)

#ifndef NDEBUG
CREATE_LOG_FN(debug, SV_LOG_NORMAL, stdout)
#else
static inline int sv_log_debug(FILE* f, const char* format, ...)
{
    (void)f;
    (void) format;
    return 0;
}
#endif

extern const sv_logger_t sv_std_logger;

#ifdef SV_IMPLEMENTATION
const sv_logger_t sv_std_logger = {
    .info = LOG_FN_NAME(info),
    .debug = LOG_FN_NAME(debug),
    .warning = LOG_FN_NAME(warn),
    .error = LOG_FN_NAME(err),
    .success = LOG_FN_NAME(success),
};
#endif

#undef CREATE_LOG_FN
#undef LOG_FN_NAME

#endif
