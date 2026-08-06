#include "test_framework.h"

/* Declare test functions from other files */
extern void test_set_error_basic(void);
extern void test_set_error_with_args(void);
extern void test_set_error_truncation(void);
extern void test_set_error_null_buffer(void);

/* Declare test functions from test_arrays.c */
extern void test_get_array_valid(void);
extern void test_get_array_invalid(void);
extern void test_get_array_null(void);
extern void test_list_arrays_valid(void);
extern void test_l4_is_orthogonal(void);
extern void test_l8_is_orthogonal(void);
extern void test_l9_is_orthogonal(void);
extern void test_l16_is_orthogonal(void);
extern void test_l27_is_orthogonal(void);
extern void test_get_array_l18(void);
extern void test_l18_col0_values_in_range(void);
extern void test_l18_col1to7_values_in_range(void);
extern void test_l18_col0_balanced(void);
extern void test_l18_3level_cols_balanced(void);
extern void test_l18_is_orthogonal(void);
extern void test_get_array_l81(void);
extern void test_l81_values_in_range(void);
extern void test_l81_is_orthogonal(void);
extern void test_get_array_l243(void);
extern void test_l243_values_in_range(void);
extern void test_l243_is_orthogonal(void);
extern void test_get_array_l32(void);
extern void test_l32_values_in_range(void);
extern void test_get_array_l64(void);
extern void test_get_array_l1024(void);
extern void test_get_array_l729(void);
extern void test_l729_values_in_range(void);
extern void test_get_array_l2187(void);
extern void test_l2187_values_in_range(void);
extern void test_get_array_l25(void);
extern void test_get_array_l125(void);
extern void test_get_array_l625(void);
extern void test_get_array_l3125(void);
extern void test_l25_values_in_range(void);
extern void test_l125_spot_check(void);
extern void test_l32_is_orthogonal(void);
extern void test_l64_is_orthogonal(void);
extern void test_l128_spot_check(void);
extern void test_l256_spot_check(void);
extern void test_l512_spot_check(void);
extern void test_l1024_spot_check(void);
extern void test_l729_is_orthogonal(void);
extern void test_l2187_spot_check(void);
extern void test_columns_needed_basic(void);

/* Declare test functions from test_parser.c */
extern void test_parse_simple_factor_definition(void);
extern void test_parse_multiple_factors(void);
extern void test_parse_with_whitespace(void);
extern void test_parse_invalid_no_factors(void);
extern void test_parse_invalid_no_array(void);
extern void test_validate_correct_definition(void);
extern void test_validate_empty_factor_name(void);

/* Declare test functions from test_auto_select.c */
extern void test_suggest_optimal_array_basic_2level(void);
extern void test_suggest_optimal_array_3level(void);
extern void test_suggest_optimal_array_mixed_levels(void);
extern void test_suggest_optimal_array_too_many_factors(void);
extern void test_suggest_optimal_array_with_3level_limit(void);
extern void test_suggest_optimal_array_single_factor(void);
extern void test_suggest_optimal_array_null_input(void);
extern void test_column_pairing_9level_factor(void);
extern void test_mixed_level_2_in_3level_array(void);
extern void test_peltier_style_experiment(void);
extern void test_auto_select_with_9level_factor(void);
extern void test_auto_select_vs_manual_specification(void);
extern void test_auto_select_l32_for_6_two_level_factors(void);
extern void test_auto_select_l64_for_many_two_level_factors(void);
extern void test_auto_select_l128_for_50_two_level_factors(void);
extern void test_auto_select_l729_for_many_three_level_factors(void);
extern void test_generation_with_l32(void);
extern void test_generation_with_l64(void);
extern void test_generation_with_l729(void);
extern void test_auto_select_l729_for_100_three_level_factors(void);

/* Declare test functions from test_analyzer.c */
extern void test_analyzer_create_result_set(void);
extern void test_analyzer_create_null_inputs(void);
extern void test_analyzer_add_result_null(void);
extern void test_analyzer_result_set_grows(void);
extern void test_analyzer_main_effects_l9(void);
extern void test_analyzer_main_effects_null(void);
extern void test_analyzer_recommend_higher_is_better(void);
extern void test_analyzer_recommend_lower_is_better(void);
extern void test_analyzer_main_effects_l27(void);
extern void test_analyzer_main_effects_paired(void);
extern void test_analyzer_duplicate_values_9level(void);
extern void test_analyzer_duplicate_values_5level(void);

/* Declare test functions from test_security.c */
extern void test_parse_oversized_factor_name(void);
extern void test_parse_max_valid_factor_name(void);
extern void test_parse_oversized_level_value(void);
extern void test_parse_max_valid_level_value(void);
extern void test_parse_max_factors_at_limit(void);
extern void test_parse_too_many_factors(void);
extern void test_parse_max_levels_at_limit(void);
extern void test_parse_excess_levels_capped_not_crashed(void);
extern void test_parse_null_content(void);
extern void test_parse_null_def(void);
extern void test_parse_empty_string(void);
extern void test_parse_no_factors_section(void);
extern void test_parse_factor_name_with_equals_sign(void);
extern void test_parse_factor_name_with_shell_metacharacters(void);
extern void test_parse_large_valid_input(void);
extern void test_error_buffer_never_overflows(void);

/* Declare test functions from test_generation.c */
extern void test_generate_l27_regression(void);
extern void test_column_pairing_4level_factor(void);
extern void test_column_pairing_5level_factor(void);
extern void test_column_pairing_7level_factor(void);
extern void test_triple_pairing_10level_factor(void);
extern void test_triple_pairing_27level_factor(void);
extern void test_two_9level_factors_paired(void);
extern void test_generate_with_l243(void);
extern void test_mixed_level_balance_counts(void);
extern void test_error_array_too_small_for_paired_columns(void);
extern void test_error_exceeds_all_arrays(void);
extern void test_exact_column_fill_l9(void);
extern void test_repeated_l81_generation_consistent(void);
extern void test_nine_level_balance_in_l81(void);
extern void test_auto_select_prefers_smallest(void);
extern void test_auto_select_l27_for_5_3level_factors(void);


int main(void) {
    printf("=== Taguchi Library Test Suite ===\\n\\n");

    printf("Utils Tests:\\n");
    RUN_TEST(set_error_basic);
    RUN_TEST(set_error_with_args);
    RUN_TEST(set_error_truncation);
    RUN_TEST(set_error_null_buffer);

    printf("\\nArrays Tests:\\n");
    RUN_TEST(get_array_valid);
    RUN_TEST(get_array_invalid);
    RUN_TEST(get_array_null);
    RUN_TEST(list_arrays_valid);
    RUN_TEST(l4_is_orthogonal);
    RUN_TEST(l8_is_orthogonal);
    RUN_TEST(l9_is_orthogonal);
    RUN_TEST(l16_is_orthogonal);
    RUN_TEST(l27_is_orthogonal);
    RUN_TEST(get_array_l18);
    RUN_TEST(l18_col0_values_in_range);
    RUN_TEST(l18_col1to7_values_in_range);
    RUN_TEST(l18_col0_balanced);
    RUN_TEST(l18_3level_cols_balanced);
    RUN_TEST(l18_is_orthogonal);
    RUN_TEST(get_array_l81);
    RUN_TEST(l81_values_in_range);
    RUN_TEST(l81_is_orthogonal);
    RUN_TEST(get_array_l243);
    RUN_TEST(l243_values_in_range);
    RUN_TEST(l243_is_orthogonal);
    RUN_TEST(get_array_l32);
    RUN_TEST(l32_values_in_range);
    RUN_TEST(get_array_l64);
    RUN_TEST(get_array_l1024);
    RUN_TEST(get_array_l729);
    RUN_TEST(l729_values_in_range);
    RUN_TEST(get_array_l2187);
    RUN_TEST(l2187_values_in_range);
    RUN_TEST(get_array_l25);
    RUN_TEST(get_array_l125);
    RUN_TEST(get_array_l625);
    RUN_TEST(get_array_l3125);
    RUN_TEST(l25_values_in_range);
    RUN_TEST(l125_spot_check);
    RUN_TEST(l32_is_orthogonal);
    RUN_TEST(l64_is_orthogonal);
    RUN_TEST(l128_spot_check);
    RUN_TEST(l256_spot_check);
    RUN_TEST(l512_spot_check);
    RUN_TEST(l1024_spot_check);
    RUN_TEST(l729_is_orthogonal);
    RUN_TEST(l2187_spot_check);
    RUN_TEST(columns_needed_basic);

    printf("\\nParser Tests:\\n");
    RUN_TEST(parse_simple_factor_definition);
    RUN_TEST(parse_multiple_factors);
    RUN_TEST(parse_with_whitespace);
    RUN_TEST(parse_invalid_no_factors);
    RUN_TEST(parse_invalid_no_array);
    RUN_TEST(validate_correct_definition);
    RUN_TEST(validate_empty_factor_name);

    printf("\\nAuto-Selection Tests:\\n");
    RUN_TEST(suggest_optimal_array_basic_2level);
    RUN_TEST(suggest_optimal_array_3level);
    RUN_TEST(suggest_optimal_array_mixed_levels);
    RUN_TEST(suggest_optimal_array_too_many_factors);
    RUN_TEST(suggest_optimal_array_with_3level_limit);
    RUN_TEST(suggest_optimal_array_single_factor);
    RUN_TEST(suggest_optimal_array_null_input);
    RUN_TEST(column_pairing_9level_factor);
    RUN_TEST(mixed_level_2_in_3level_array);
    RUN_TEST(peltier_style_experiment);
    RUN_TEST(auto_select_with_9level_factor);
    RUN_TEST(auto_select_vs_manual_specification);
    RUN_TEST(auto_select_l32_for_6_two_level_factors);
    RUN_TEST(auto_select_l64_for_many_two_level_factors);
    RUN_TEST(auto_select_l128_for_50_two_level_factors);
    RUN_TEST(auto_select_l729_for_many_three_level_factors);
    RUN_TEST(generation_with_l32);
    RUN_TEST(generation_with_l64);
    RUN_TEST(generation_with_l729);
    RUN_TEST(auto_select_l729_for_100_three_level_factors);

    printf("\\nAnalyzer Tests:\\n");
    RUN_TEST(analyzer_create_result_set);
    RUN_TEST(analyzer_create_null_inputs);
    RUN_TEST(analyzer_add_result_null);
    RUN_TEST(analyzer_result_set_grows);
    RUN_TEST(analyzer_main_effects_l9);
    RUN_TEST(analyzer_main_effects_null);
    RUN_TEST(analyzer_recommend_higher_is_better);
    RUN_TEST(analyzer_recommend_lower_is_better);
    RUN_TEST(analyzer_main_effects_l27);
    RUN_TEST(analyzer_main_effects_paired);
    RUN_TEST(analyzer_duplicate_values_9level);
    RUN_TEST(analyzer_duplicate_values_5level);

    printf("\\nGeneration & Column Pairing Tests:\\n");
    RUN_TEST(generate_l27_regression);
    RUN_TEST(column_pairing_4level_factor);
    RUN_TEST(column_pairing_5level_factor);
    RUN_TEST(column_pairing_7level_factor);
    RUN_TEST(triple_pairing_10level_factor);
    RUN_TEST(triple_pairing_27level_factor);
    RUN_TEST(two_9level_factors_paired);
    RUN_TEST(generate_with_l243);
    RUN_TEST(mixed_level_balance_counts);
    RUN_TEST(error_array_too_small_for_paired_columns);
    RUN_TEST(error_exceeds_all_arrays);
    RUN_TEST(exact_column_fill_l9);
    RUN_TEST(repeated_l81_generation_consistent);
    RUN_TEST(nine_level_balance_in_l81);
    RUN_TEST(auto_select_prefers_smallest);
    RUN_TEST(auto_select_l27_for_5_3level_factors);

    printf("\\nSecurity Tests:\\n");
    RUN_TEST(parse_oversized_factor_name);
    RUN_TEST(parse_max_valid_factor_name);
    RUN_TEST(parse_oversized_level_value);
    RUN_TEST(parse_max_valid_level_value);
    RUN_TEST(parse_max_factors_at_limit);
    RUN_TEST(parse_too_many_factors);
    RUN_TEST(parse_max_levels_at_limit);
    RUN_TEST(parse_excess_levels_capped_not_crashed);
    RUN_TEST(parse_null_content);
    RUN_TEST(parse_null_def);
    RUN_TEST(parse_empty_string);
    RUN_TEST(parse_no_factors_section);
    RUN_TEST(parse_factor_name_with_equals_sign);
    RUN_TEST(parse_factor_name_with_shell_metacharacters);
    RUN_TEST(parse_large_valid_input);
    RUN_TEST(error_buffer_never_overflows);

    printf("\\n=== All Tests Passed ===\\n");
    return 0;
}
