#include "test_framework.h"
#include "include/taguchi.h"
#include "src/lib/utils.h"
#include <string.h>

TEST(set_error_basic) {
    char error[TAGUCHI_ERROR_SIZE];
    set_error(error, "This is a test error");
    ASSERT_STR_EQ("This is a test error", error);
}

TEST(set_error_with_args) {
    char error[TAGUCHI_ERROR_SIZE];
    set_error(error, "Error code: %d, message: %s", 404, "Not Found");
    ASSERT_STR_EQ("Error code: 404, message: Not Found", error);
}

TEST(set_error_truncation) {
    char error[TAGUCHI_ERROR_SIZE];
    const char *long_string = "This is a very long string that is designed to be much longer than the allocated error buffer of 256 characters to ensure that the set_error function correctly truncates the input to prevent buffer overflows, which are a serious security risk in C programming. We need to be very careful about this.";
    set_error(error, "Message: %s", long_string);
    ASSERT_EQ(strlen(error), TAGUCHI_ERROR_SIZE - 1);
    ASSERT_NE(strcmp(error, long_string), 0);
}

TEST(set_error_null_buffer) {
    // This test just ensures that calling set_error with a NULL buffer doesn't crash.
    set_error(NULL, "This should not crash");
    // No assertion needed, the test passes if it doesn't segfault.
}
