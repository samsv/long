#define SV_IMPLEMENTATION
#include "../src/std/test.h"
#include "../src/std/vector.h"
#include "../src/std/allocator_std.h"

sv_vec_def(int);

int main(void)
{
    sv_testing_t t = sv_test_default;

    sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);
    int success = 0;
    sv_vec_push(&vs, 41, &success, &sv_gpa);
    sv_test_run(&t, success == 1);
    sv_test_run(&t, vs.size == 1);
    sv_test_run_msg(&t, sv_vec_at(vs, 0) == 41, "got %d", sv_vec_at(vs, 0));
    sv_vec_deinit(&vs, &sv_gpa);

    sv_test_summary(t, 1);
    return 0;
}
