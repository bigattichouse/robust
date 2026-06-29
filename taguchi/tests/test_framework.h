/* tests/test_framework.h */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TEST(name) void test_##name(void)

#define RUN_TEST(name) do { \
    printf("Running %s...\\n", #name); \
    test_##name(); \
    printf("  PASSED\\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERTION FAILED: %s:%d: %s\\n", \
                __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0)
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)
#define ASSERT_DOUBLE_EQ(a, b, epsilon) ASSERT(fabs((a) - (b)) < (epsilon))
#define ASSERT_TRUE(condition) ASSERT(condition)
#define ASSERT_FALSE(condition) ASSERT(!(condition))

#endif /* TEST_FRAMEWORK_H */

