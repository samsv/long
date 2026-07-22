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
 * Runs a test. On failure, prints the file, line and failing expression.
 * @param t: sv_testing_t struct;
 * @param expr: expression which must evaluate to true or false;
 */
#define sv_test_run(t, expr) do {\
    (t)->tests++;\
    if (expr) {\
        (t)->passed++;\
        break;\
    }\
    (t)->logger->error(NULL, "File %s, line %d: Test failed: %s", __FILE__, __LINE__, #expr);\
} while(0)

/**
 * Runs a test with a custom failure message.
 * @param t: sv_testing_t struct;
 * @param expr: expression which must evaluate to true or false;
 * @param msg: a format string to display if the test fails. C99 requires at
 *      least one format argument; use ("%s", "text") for a plain string;
 */
#define sv_test_run_msg(t, expr, msg, ...) do {\
    (t)->tests++;\
    if (expr) {\
        (t)->passed++;\
        break;\
    }\
    (t)->logger->error(NULL, "File %s, line %d: Test failed: %s", __FILE__, __LINE__, #expr);\
    (t)->logger->error(NULL, msg, __VA_ARGS__);\
} while(0)

/**
 * Prints the test summary.
 * @param t: the testing struct which holds all the data;
 * @param exit_on_error: if true, the program will exit with an error code of 1
 *      if any test failed;
 */
static inline void sv_test_summary(sv_testing_t t, int exit_on_error)
{
    if (t.passed == t.tests) {
        t.logger->success(NULL, "All %d tests passed!", t.tests);
        return;
    }

    t.logger->error(NULL, "%d out of %d tests failed!", t.tests - t.passed, t.tests);

    if (exit_on_error) {
        exit(1);
    }
}

#endif
