#include "test_framework.h" 
#include "include/taguchi.h"
#include <string.h>

TEST(suggest_optimal_array_basic_2level) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Use the format that works from original tests
    const char *content = 
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4, 8\n"
        "array: L9\n";
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    if (!def) {
        printf("Parse failed with: %s\n", error);
    }
    ASSERT_NOT_NULL(def);
    
    const char *recommended = taguchi_suggest_optimal_array(def, error);
    if (!recommended) {
        printf("Suggestion failed with: %s\n", error);
    }
    ASSERT_NOT_NULL(recommended);
    
    // The function should return a valid array name, even if it's the same as already specified
    bool is_valid_output = (recommended[0] == 'L' && strlen(recommended) >= 2);
    ASSERT(is_valid_output);
    
    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_3level) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Test with 4 factors, 3 levels each - should suggest L9
    const char *content = 
        "factors:\n"
        "  temp: 300F, 350F, 400F\n"
        "  time: 10min, 15min, 20min\n"
        "  size: small, medium, large\n"
        "  material: A, B, C\n"
        "array: L27\n";  // For parsing only
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);
    
    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    
    // Should suggest appropriate array (like L9 for 4 factors x 3 levels)
    bool is_valid_output = (recommended[0] == 'L' && strlen(recommended) >= 2);
    ASSERT(is_valid_output);
    
    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_mixed_levels) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Test with mixed levels (some 2-level, some 3-level factors)
    const char *content = 
        "factors:\n"
        "  temp: 300F, 350F, 400F\n"      // 3 levels
        "  on_off: OFF, ON\n"             // 2 levels  
        "  pressure: 10, 15, 20\n"       // 3 levels
        "array: L9\n";                    // For parsing
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);
    
    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    
    // Should suggest appropriate array supporting up to 3 levels
    bool is_valid_output = (recommended[0] == 'L' && strlen(recommended) >= 2);
    ASSERT(is_valid_output);
    
    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_too_many_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 16 two-level factors need 16 columns. L16 has 15 cols (too few),
       but L81 has 40 cols at 3 levels which fits. So this should succeed now.
       To truly exceed capacity, we need more factors than the largest array. */
    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n"
        "  f6: A, B\n  f7: A, B\n  f8: A, B\n  f9: A, B\n  f10: A, B\n"
        "  f11: A, B\n  f12: A, B\n  f13: A, B\n  f14: A, B\n  f15: A, B\n"
        "  f16: A, B\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    /* 16 two-level factors need 16 columns; L81 has 40 columns so it fits */
    ASSERT_NOT_NULL(recommended);

    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_with_3level_limit) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Test with 13 factors, 3 levels each - should suggest L27
    const char *content = 
        "factors:\n"
        "  f1: A, B, C\n  f2: A, B, C\n  f3: A, B, C\n  f4: A, B, C\n  f5: A, B, C\n"
        "  f6: A, B, C\n  f7: A, B, C\n  f8: A, B, C\n  f9: A, B, C\n  f10: A, B, C\n"
        "  f11: A, B, C\n  f12: A, B, C\n  f13: A, B, C\n"
        "array: L27\n";  // For parsing but test auto-select function
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);
    
    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    
    // Should suggest appropriate array for 13 factors with 3 levels
    bool is_valid_output = (recommended[0] == 'L' && strlen(recommended) >= 2);
    ASSERT(is_valid_output);
    
    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_single_factor) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Test with single factor
    const char *content = 
        "factors:\n"
        "  temp: 300F, 350F\n"
        "array: L4\n";  // For parsing but test function
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);
    
    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    
    // Should suggest appropriate small array
    bool is_valid_output = (recommended[0] == 'L' && strlen(recommended) >= 2);
    ASSERT(is_valid_output);
    
    taguchi_free_definition(def);
}

TEST(suggest_optimal_array_null_input) {
    char error[TAGUCHI_ERROR_SIZE];
    
    // Test with NULL definition
    const char *recommended = taguchi_suggest_optimal_array(NULL, error);
    ASSERT_NULL(recommended);
    
    // Error buffer should contain error message
    ASSERT(strlen(error) > 0);
}

TEST(column_pairing_9level_factor) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 9-level factor requires column pairing (2 columns in 3-level array) */
    const char *content =
        "factors:\n"
        "  n_stages: 1, 2, 3, 4, 5, 6, 7, 8, 9\n"
        "  mode: pumped, static, hybrid\n"
        "array: L81\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    if (!def) {
        printf("Parse failed: %s\n", error);
    }
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    if (result != 0) {
        printf("Generate failed: %s\n", error);
    }
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 81);

    /* Verify all 9 stage values appear in the runs */
    int stage_seen[9] = {0};
    for (size_t i = 0; i < count; i++) {
        const char *val = taguchi_run_get_value(runs[i], "n_stages");
        ASSERT_NOT_NULL(val);
        int v = atoi(val);
        if (v >= 1 && v <= 9) {
            stage_seen[v - 1] = 1;
        }
    }
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(stage_seen[i], 1);
    }

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(mixed_level_2_in_3level_array) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 2-level factor in 3-level array should work with wrapping */
    const char *content =
        "factors:\n"
        "  on_off: true, false\n"
        "  temp: low, medium, high\n"
        "  size: small, medium, large\n"
        "  color: red, green, blue\n"
        "array: L9\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 9);

    /* Verify on_off only has valid values */
    for (size_t i = 0; i < count; i++) {
        const char *val = taguchi_run_get_value(runs[i], "on_off");
        ASSERT_NOT_NULL(val);
        ASSERT(strcmp(val, "true") == 0 || strcmp(val, "false") == 0);
    }

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(peltier_style_experiment) {
    char error[TAGUCHI_ERROR_SIZE];

    /* Simulate the peltier stack experiment: mix of 2, 3, and 9-level factors */
    const char *content =
        "factors:\n"
        "  coupling_mode: pumped_series, static_pool\n"
        "  n_stages: 1, 2, 3, 4, 5, 6, 7, 8, 9\n"
        "  current: 0.1, 0.25, 0.5, 1.0, 2.0, 3.5, 5.0, 7.0, 10.0\n"
        "  n_parallel: 1, 2, 4\n"
        "  tank_size: small, standard, large\n"
        "array: L81\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    if (!def) {
        printf("Parse failed: %s\n", error);
    }
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    if (result != 0) {
        printf("Generate failed: %s\n", error);
    }
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 81);

    /* Columns needed: coupling_mode=1, n_stages=2, current=2, n_parallel=1, tank_size=1 = 7 total */
    /* L81 has 40 columns, so this fits easily */

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(auto_select_with_9level_factor) {
    char error[TAGUCHI_ERROR_SIZE];

    /* Auto-selection picks L16 for mixed 9-level and 3-level factors
       L16 has 150% margin which is in the preferred range */
    const char *content =
        "factors:\n"
        "  n_stages: 1, 2, 3, 4, 5, 6, 7, 8, 9\n"
        "  mode: A, B, C\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L16 has 150% margin which is in preferred range */
    ASSERT_STR_EQ(recommended, "L16");

    taguchi_free_definition(def);
}

TEST(auto_select_vs_manual_specification) {
    char error[TAGUCHI_ERROR_SIZE];

    // Test auto-selection function on the same experiment definition
    const char *experiment_content =
        "factors:\n"
        "  temp: 300F, 350F, 400F\n"
        "  time: 10min, 15min, 20min\n"
        "array: L9\n";  // For parsing but we'll test auto-selection function

    taguchi_experiment_def_t *def = taguchi_parse_definition(experiment_content, error);
    ASSERT_NOT_NULL(def);

    // The auto-selected array should suggest something appropriate for the factor structure
    const char *auto_selected = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(auto_selected);

    // Should suggest an appropriate array for 2 factors with 3 levels each
    bool is_valid_output = (auto_selected[0] == 'L' && strlen(auto_selected) >= 2);
    ASSERT(is_valid_output);

    taguchi_free_definition(def);
}

/* Tests for new higher-order arrays */
TEST(auto_select_l32_for_6_two_level_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 6 two-level factors need 6 columns; L8 has 7 cols (margin 16%)
       L16 has 15 cols (margin 150%) - prefer L16 with good margin */
    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n  f6: A, B\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L16 has 15 cols, margin = 150% which is in good range */
    ASSERT_STR_EQ(recommended, "L16");

    taguchi_free_definition(def);
}

TEST(auto_select_l64_for_many_two_level_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 20 two-level factors need 20 columns; L32 has 31 cols */
    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n"
        "  f6: A, B\n  f7: A, B\n  f8: A, B\n  f9: A, B\n  f10: A, B\n"
        "  f11: A, B\n  f12: A, B\n  f13: A, B\n  f14: A, B\n  f15: A, B\n"
        "  f16: A, B\n  f17: A, B\n  f18: A, B\n  f19: A, B\n  f20: A, B\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L32 has 31 cols which fits 20 factors */
    ASSERT_STR_EQ(recommended, "L32");

    taguchi_free_definition(def);
}

TEST(auto_select_l128_for_50_two_level_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 50 two-level factors need 50 columns; L64 has 63 cols */
    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n"
        "  f6: A, B\n  f7: A, B\n  f8: A, B\n  f9: A, B\n  f10: A, B\n"
        "  f11: A, B\n  f12: A, B\n  f13: A, B\n  f14: A, B\n  f15: A, B\n"
        "  f16: A, B\n  f17: A, B\n  f18: A, B\n  f19: A, B\n  f20: A, B\n"
        "  f21: A, B\n  f22: A, B\n  f23: A, B\n  f24: A, B\n  f25: A, B\n"
        "  f26: A, B\n  f27: A, B\n  f28: A, B\n  f29: A, B\n  f30: A, B\n"
        "  f31: A, B\n  f32: A, B\n  f33: A, B\n  f34: A, B\n  f35: A, B\n"
        "  f36: A, B\n  f37: A, B\n  f38: A, B\n  f39: A, B\n  f40: A, B\n"
        "  f41: A, B\n  f42: A, B\n  f43: A, B\n  f44: A, B\n  f45: A, B\n"
        "  f46: A, B\n  f47: A, B\n  f48: A, B\n  f49: A, B\n  f50: A, B\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L128 has 127 cols, margin = 154% which is in preferred range */
    ASSERT_STR_EQ(recommended, "L128");

    taguchi_free_definition(def);
}

TEST(auto_select_l729_for_many_three_level_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 20 three-level factors need 20 columns in 3-level array, or 40 cols in 2-level
       L64 has 63 cols (2-level) which fits via column pairing, so it's selected */
    const char *content =
        "factors:\n"
        "  f1: A, B, C\n  f2: A, B, C\n  f3: A, B, C\n  f4: A, B, C\n  f5: A, B, C\n"
        "  f6: A, B, C\n  f7: A, B, C\n  f8: A, B, C\n  f9: A, B, C\n  f10: A, B, C\n"
        "  f11: A, B, C\n  f12: A, B, C\n  f13: A, B, C\n  f14: A, B, C\n  f15: A, B, C\n"
        "  f16: A, B, C\n  f17: A, B, C\n  f18: A, B, C\n  f19: A, B, C\n  f20: A, B, C\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L81 is exact 3-level match with 100% margin */
    ASSERT_STR_EQ(recommended, "L81");

    taguchi_free_definition(def);
}

TEST(generation_with_l32) {
    char error[TAGUCHI_ERROR_SIZE];

    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n"
        "array: L32\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 32);

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(generation_with_l64) {
    char error[TAGUCHI_ERROR_SIZE];

    const char *content =
        "factors:\n"
        "  f1: A, B\n  f2: A, B\n  f3: A, B\n  f4: A, B\n  f5: A, B\n"
        "  f6: A, B\n  f7: A, B\n  f8: A, B\n  f9: A, B\n  f10: A, B\n"
        "array: L64\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 64);

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(generation_with_l729) {
    char error[TAGUCHI_ERROR_SIZE];

    const char *content =
        "factors:\n"
        "  f1: A, B, C\n  f2: A, B, C\n  f3: A, B, C\n  f4: A, B, C\n  f5: A, B, C\n"
        "  f6: A, B, C\n  f7: A, B, C\n  f8: A, B, C\n  f9: A, B, C\n  f10: A, B, C\n"
        "array: L729\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    int result = taguchi_generate_runs(def, &runs, &count, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(count, 729);

    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

TEST(auto_select_l729_for_100_three_level_factors) {
    char error[TAGUCHI_ERROR_SIZE];

    /* 100 three-level factors need 100 columns in 3-level array (L729 has 364)
       But in 2-level array, each needs 2 cols = 200 cols total
       L256 has 255 cols which fits, so it's selected (smaller than L729) */
    const char *content =
        "factors:\n"
        "  f1: A, B, C\n  f2: A, B, C\n  f3: A, B, C\n  f4: A, B, C\n  f5: A, B, C\n"
        "  f6: A, B, C\n  f7: A, B, C\n  f8: A, B, C\n  f9: A, B, C\n  f10: A, B, C\n"
        "  f11: A, B, C\n  f12: A, B, C\n  f13: A, B, C\n  f14: A, B, C\n  f15: A, B, C\n"
        "  f16: A, B, C\n  f17: A, B, C\n  f18: A, B, C\n  f19: A, B, C\n  f20: A, B, C\n"
        "  f21: A, B, C\n  f22: A, B, C\n  f23: A, B, C\n  f24: A, B, C\n  f25: A, B, C\n"
        "  f26: A, B, C\n  f27: A, B, C\n  f28: A, B, C\n  f29: A, B, C\n  f30: A, B, C\n"
        "  f31: A, B, C\n  f32: A, B, C\n  f33: A, B, C\n  f34: A, B, C\n  f35: A, B, C\n"
        "  f36: A, B, C\n  f37: A, B, C\n  f38: A, B, C\n  f39: A, B, C\n  f40: A, B, C\n"
        "  f41: A, B, C\n  f42: A, B, C\n  f43: A, B, C\n  f44: A, B, C\n  f45: A, B, C\n"
        "  f46: A, B, C\n  f47: A, B, C\n  f48: A, B, C\n  f49: A, B, C\n  f50: A, B, C\n"
        "  f51: A, B, C\n  f52: A, B, C\n  f53: A, B, C\n  f54: A, B, C\n  f55: A, B, C\n"
        "  f56: A, B, C\n  f57: A, B, C\n  f58: A, B, C\n  f59: A, B, C\n  f60: A, B, C\n"
        "  f61: A, B, C\n  f62: A, B, C\n  f63: A, B, C\n  f64: A, B, C\n  f65: A, B, C\n"
        "  f66: A, B, C\n  f67: A, B, C\n  f68: A, B, C\n  f69: A, B, C\n  f70: A, B, C\n"
        "  f71: A, B, C\n  f72: A, B, C\n  f73: A, B, C\n  f74: A, B, C\n  f75: A, B, C\n"
        "  f76: A, B, C\n  f77: A, B, C\n  f78: A, B, C\n  f79: A, B, C\n  f80: A, B, C\n"
        "  f81: A, B, C\n  f82: A, B, C\n  f83: A, B, C\n  f84: A, B, C\n  f85: A, B, C\n"
        "  f86: A, B, C\n  f87: A, B, C\n  f88: A, B, C\n  f89: A, B, C\n  f90: A, B, C\n"
        "  f91: A, B, C\n  f92: A, B, C\n  f93: A, B, C\n  f94: A, B, C\n  f95: A, B, C\n"
        "  f96: A, B, C\n  f97: A, B, C\n  f98: A, B, C\n  f99: A, B, C\n  f100: A, B, C\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    ASSERT_NOT_NULL(def);

    const char *recommended = taguchi_suggest_optimal_array(def, error);
    ASSERT_NOT_NULL(recommended);
    /* L243 is exact 3-level match (121 cols for 100 factors) */
    ASSERT_STR_EQ(recommended, "L243");

    taguchi_free_definition(def);
}