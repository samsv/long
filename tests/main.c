#define SV_IMPLEMENTATION
#include "../src/std/test.h"
#include "vector.h"
#include "string.h"
#include "unicode.h"
#include "scanner.h"

int main(void)
{
    sv_testing_t t = sv_test_default;

    sv_test_vector(&t);
    sv_test_string(&t);
    sv_test_unicode(&t);
    sv_test_scanner(&t);

    sv_test_summary(t, 1);
    return 0;
}
