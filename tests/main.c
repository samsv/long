#define SV_IMPLEMENTATION
#include "../src/std/test.h"
#include "vector.h"
#include "string.h"

int main(void)
{
    sv_testing_t t = sv_test_default;

    sv_test_vector(&t);
    sv_test_string(&t);

    sv_test_summary(t, 1);
    return 0;
}
