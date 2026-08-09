#include "test_framework.h"
#include "src/lib/parser.h"
#include "include/taguchi.h"
#include <string.h>

TEST(parse_simple_factor_definition) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];

    const char *valid_input =
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "array: L9\n";

    int result = parse_experiment_def_from_string(valid_input, &def, error);

    if (result != 0) {
        printf("Parse error: %s\n", error);
    }

    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 1);
    ASSERT_STR_EQ(def.factors[0].name, "cache_size");
    ASSERT_EQ(def.factors[0].level_count, 3);
    ASSERT_STR_EQ(def.factors[0].values[0], "64M");
    ASSERT_STR_EQ(def.factors[0].values[1], "128M");
    ASSERT_STR_EQ(def.factors[0].values[2], "256M");
    ASSERT_STR_EQ(def.array_type, "L9");
}

TEST(parse_multiple_factors) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    
    const char *input = 
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4, 8\n"
        "  timeout: 30, 60, 120\n"
        "array: L9\n";
    
    int result = parse_experiment_def_from_string(input, &def, error);
    
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 3);
    ASSERT_STR_EQ(def.factors[0].name, "cache_size");
    ASSERT_STR_EQ(def.factors[1].name, "threads");
    ASSERT_STR_EQ(def.factors[2].name, "timeout");
    ASSERT_STR_EQ(def.array_type, "L9");
}

TEST(parse_with_whitespace) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    
    const char *input = 
        "factors:\n"
        "  cache_size : 64M , 128M , 256M\n"
        "  threads: 2,4,8\n"
        "array: L9\n";
    
    int result = parse_experiment_def_from_string(input, &def, error);
    
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 2);
    ASSERT_STR_EQ(def.factors[0].name, "cache_size");
    ASSERT_STR_EQ(def.factors[1].name, "threads");
    ASSERT_EQ(def.factors[0].level_count, 3);
    ASSERT_EQ(def.factors[1].level_count, 3);
    ASSERT_STR_EQ(def.array_type, "L9");
}

TEST(parse_invalid_no_factors) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    
    const char *input = 
        "array: L9\n";
    
    int result = parse_experiment_def_from_string(input, &def, error);
    
    ASSERT_EQ(result, -1);
    // Should fail because no factors defined
}

TEST(parse_invalid_no_array) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];

    const char *input =
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n";

    int result = parse_experiment_def_from_string(input, &def, error);

    // Should now succeed as array specification is optional for auto-selection
    ASSERT_EQ(result, 0);
    ASSERT_GT(def.factor_count, 0);  // Should have factors
    ASSERT_EQ(strlen(def.array_type), 0);  // Array type should be empty
}

TEST(validate_correct_definition) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    
    const char *input = 
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4\n"
        "array: L9\n";
    
    int result = parse_experiment_def_from_string(input, &def, error);
    ASSERT_EQ(result, 0);
    
    bool valid = validate_experiment_def(&def, error);
    ASSERT_TRUE(valid);
}

TEST(validate_empty_factor_name) {
    ExperimentDef def;
    // Manually create invalid def to test validation
    memset(&def, 0, sizeof(def));
    def.factor_count = 1;
    // Leave factor name empty
    strcpy(def.array_type, "L9");
    
    char error[TAGUCHI_ERROR_SIZE];
    bool valid = validate_experiment_def(&def, error);
    ASSERT_FALSE(valid);
}

/*
 * Parse errors name the line they came from, and the number is the FILE's.
 *
 * This is the test the previous line counter could never have passed. It was
 * driven by strtok, which collapses runs of its delimiter, so blank lines
 * never became tokens and were never counted -- the factor below would have
 * been called line 4 instead of line 7. That is why it was removed rather
 * than wired up (see the PR that deleted it): a confidently wrong line number
 * is worse than none.
 */
TEST(parse_error_reports_the_file_line) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];

    const char *input =
        "array: L4\n"        /* 1 */
        "\n"                 /* 2 */
        "\n"                 /* 3 */
        "# a comment\n"      /* 4 */
        "\n"                 /* 5 */
        "factors:\n"         /* 6 */
        "  : 1, 2\n";        /* 7 <- empty factor name */

    int result = parse_experiment_def_from_string(input, &def, error);
    ASSERT_EQ(result, -1);
    ASSERT_TRUE(strstr(error, "line 7:") != NULL);

    /* the array-type error on line 1 */
    const char *long_array =
        "array: WAY_TOO_LONG_ARRAY_NAME_HERE\n"
        "factors:\n"
        "  a: 1, 2\n";
    result = parse_experiment_def_from_string(long_array, &def, error);
    ASSERT_EQ(result, -1);
    ASSERT_TRUE(strstr(error, "line 1:") != NULL);

    /* a whole-file problem is not about any line, so it carries no number */
    result = parse_experiment_def_from_string("array: L4\n", &def, error);
    ASSERT_EQ(result, -1);
    ASSERT_TRUE(strstr(error, "line ") == NULL);
}

/* Blank lines and comments must still parse correctly after the strtok
 * replacement -- the scanner change is the risky half of this. */
TEST(parse_survives_blank_lines_and_crlf) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];

    const char *spaced =
        "\n"
        "array: L4\n"
        "\n"
        "factors:\n"
        "\n"
        "  a: 1, 2\n"
        "\n"
        "  b: 3, 4\n"
        "\n";
    int result = parse_experiment_def_from_string(spaced, &def, error);
    if (result != 0) printf("Parse error: %s\n", error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 2);
    ASSERT_STR_EQ(def.factors[0].name, "a");
    ASSERT_STR_EQ(def.factors[1].name, "b");
    ASSERT_STR_EQ(def.array_type, "L4");

    /* CRLF line endings: trim_whitespace eats the \r */
    const char *crlf = "array: L4\r\nfactors:\r\n  a: 1, 2\r\n  b: 3, 4\r\n";
    result = parse_experiment_def_from_string(crlf, &def, error);
    if (result != 0) printf("Parse error: %s\n", error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 2);
    ASSERT_STR_EQ(def.array_type, "L4");

    /* no trailing newline on the last line */
    result = parse_experiment_def_from_string("array: L4\nfactors:\n  a: 1, 2", &def, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 1);
}
