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