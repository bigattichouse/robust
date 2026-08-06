#ifndef DOE_TEST_FRAMEWORK_H
#define DOE_TEST_FRAMEWORK_H

/*
 * Tiny self-contained test harness for the libdoe core. Each test is an
 * `int fn(void)` returning 1 (pass) or 0 (fail). One test translation unit
 * owns these counters and calls TEST_SUMMARY() from main.
 */

#include <stdio.h>
#include <math.h>

static int doe_tests_run = 0;
static int doe_tests_failed = 0;

#define RUN_TEST(fn) do {                              \
    printf("  %-36s", #fn);                            \
    doe_tests_run++;                                   \
    if (fn()) { printf("ok\n"); }                      \
    else      { doe_tests_failed++; printf("FAIL\n"); }\
} while (0)

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        printf("\n    CHECK failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 0;                                                          \
    }                                                                      \
} while (0)

#define CHECK_DBL(a, b, tol) do {                                          \
    double _a = (a), _b = (b);                                             \
    if (fabs(_a - _b) > (tol)) {                                           \
        printf("\n    CHECK_DBL failed: %s ~= %s  (%g vs %g)  (%s:%d)\n",  \
               #a, #b, _a, _b, __FILE__, __LINE__);                        \
        return 0;                                                          \
    }                                                                      \
} while (0)

#define TEST_SUMMARY() (                                                   \
    printf("\n%d/%d tests passed\n",                                       \
           doe_tests_run - doe_tests_failed, doe_tests_run),               \
    doe_tests_failed == 0 ? 0 : 1)

#endif /* DOE_TEST_FRAMEWORK_H */
