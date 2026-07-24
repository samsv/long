#ifndef SV_TESTS_UNICODE_H
#define SV_TESTS_UNICODE_H

#include "../src/std/test.h"
#include "../src/unicode_alphabetic_table.h"

static inline void sv_test_unicode(sv_testing_t* t)
{
   sv_test_run(t, unicode_alphabetic('A'));
   sv_test_run(t, unicode_alphabetic('z'));
   sv_test_run(t, !unicode_alphabetic('1'));
   sv_test_run(t, !unicode_alphabetic('_'));
   sv_test_run(t, !unicode_alphabetic(' '));
   sv_test_run(t, unicode_alphabetic(0x00E9));
   sv_test_run(t, unicode_alphabetic(0x03A9));
   sv_test_run(t, unicode_alphabetic(0x4E2D));
   sv_test_run(t, unicode_alphabetic(0x05D0));
   sv_test_run(t, unicode_alphabetic(0x10000));
   sv_test_run(t, !unicode_alphabetic(0x0669));
   sv_test_run(t, unicode_alphabetic(0x00C0));
   sv_test_run(t, unicode_alphabetic(0x00D6));
   sv_test_run(t, !unicode_alphabetic(0x00D7));
   sv_test_run(t, unicode_alphabetic(0x323AF));
   sv_test_run(t, !unicode_alphabetic(0x323B0));
   sv_test_run(t, !unicode_alphabetic(0x110000));
}

#endif
