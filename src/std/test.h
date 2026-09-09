#ifndef SV_TEST_H
#define SV_TEST_H

#include <stdlib.h>
#include "logger.h"

typedef struct {
    int tests;
    int passed;
    const sv_logger_t* logger;
} sv_testing_t;

/**
 * Initializes a test struct with the standard logger.
 */
#define sv_test_default { .tests = 0, .passed = 0, .logger = &sv_std_logger }

/**
 * Runs a test. `expr` must evaluate to true or false; on failure, prints
 * the file, line and failing expression through `t`'s logger.
 */
#define sv_test_run(t, expr) do {\
    (t)->tests++;\
    if (expr) {\
        (t)->passed++;\
        break;\
    }\
    sv_log_error((t)->logger, "File %s, line %d: Test failed: %s", __FILE__, __LINE__, #expr);\
} while(0)

/**
 * Runs a test like sv_test_run, also printing the format string `msg` on
 * failure. At least one format argument is required; use ("%s", "text")
 * for a plain string.
 */
#define sv_test_run_msg(t, expr, msg, ...) do {\
    (t)->tests++;\
    if (expr) {\
        (t)->passed++;\
        break;\
    }\
    sv_log_error((t)->logger, "File %s, line %d: Test failed: %s", __FILE__, __LINE__, #expr);\
    sv_log_error((t)->logger, msg, __VA_ARGS__);\
} while(0)

/**
 * Prints the test summary. If `exit_on_error` is true and any test failed,
 * exits with code 1.
 */
static inline void sv_test_summary(sv_testing_t t, int exit_on_error)
{
    if (t.passed == t.tests) {
        sv_log_success(t.logger, "All %d tests passed!", t.tests);
        return;
    }

    sv_log_error(t.logger, "%d out of %d tests failed!", t.tests - t.passed, t.tests);

    if (exit_on_error) {
        exit(1);
    }
}

#endif
