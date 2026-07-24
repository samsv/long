#include <stdio.h>
#define SV_IMPLEMENTATION
#include "src/std/vector.h"
#include "src/std/allocator_std.h"

// define the vector type before using it.
sv_vec_def(int);

int main(void) {
    /// append ///
    sv_vec_t(int) vs = sv_vec_init(int, &sv_gpa);

    for (int i = 0; i < 10; i++) {
        int success; // pass NULL if you do not wish to check for the error code.
        sv_vec_push(&vs, i, &success, &sv_gpa);
        if (!success) {
            printf("Could not insert data into vector\n");
            exit(1);
        }
    }

    int xs[] = {1, 2, 3, 4, 16, 15, 1021, 415};
    sv_vec_push_many(&vs, xs, 8, NULL, &sv_gpa);

    /// loops ///
    // normal loop
    for (int64_t i = 0; i < vs.size; i++) {
        printf("%d\n", vs.arr[i]);
    }
    // foreach
    sv_vec_foreach(int, el, &vs) {
        printf("%d\n", el);
    }


    /// free vector ///
    sv_vec_deinit(&vs, &sv_gpa);
}
