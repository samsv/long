#define SV_IMPLEMENTATION
#include "../src/std/test.h"
#include "vector.h"
#include "string.h"
#include "unicode.h"
#include "rc.h"
#include "list.h"
#include "map.h"
#include "str_iter.h"
#include "compiler.h"
#include "parser.h"

int main(void)
{
    sv_testing_t t = sv_test_default;

    sv_test_vector(&t);
    sv_test_string(&t);
    sv_test_unicode(&t);
    sv_test_rc(&t);
    sv_test_list(&t);
    sv_test_map(&t);
    sv_test_str_iter(&t);
    sv_test_compiler(&t);
    sv_test_parser(&t);

    sv_test_summary(t, 1);
    return 0;
}
