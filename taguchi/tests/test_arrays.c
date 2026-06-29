#include "test_framework.h"
#include "src/lib/arrays.h"

TEST(get_array_valid) {
    const OrthogonalArray *array = get_array("L4");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L4");
    ASSERT_EQ(array->rows, 4);
    ASSERT_EQ(array->cols, 3);
    ASSERT_EQ(array->levels, 2);
}

TEST(get_array_invalid) {
    const OrthogonalArray *array = get_array("L5");
    ASSERT_NULL(array);
}

TEST(get_array_null) {
    const OrthogonalArray *array = get_array(NULL);
    ASSERT_NULL(array);
}

TEST(list_arrays_valid) {
    const char **names = list_array_names();
    ASSERT_NOT_NULL(names);
    /* Static arrays */
    ASSERT_STR_EQ(names[0], "L4");
    ASSERT_STR_EQ(names[1], "L8");
    ASSERT_STR_EQ(names[2], "L9");
    ASSERT_STR_EQ(names[3], "L16");
    ASSERT_STR_EQ(names[4], "L18");
    /* GF(2) series */
    ASSERT_STR_EQ(names[5], "L32");
    ASSERT_STR_EQ(names[6], "L64");
    ASSERT_STR_EQ(names[7], "L128");
    ASSERT_STR_EQ(names[8], "L256");
    ASSERT_STR_EQ(names[9], "L512");
    ASSERT_STR_EQ(names[10], "L1024");
    /* GF(3) series */
    ASSERT_STR_EQ(names[11], "L27");
    ASSERT_STR_EQ(names[12], "L81");
    ASSERT_STR_EQ(names[13], "L243");
    ASSERT_STR_EQ(names[14], "L729");
    ASSERT_STR_EQ(names[15], "L2187");
    /* GF(5) series */
    ASSERT_STR_EQ(names[16], "L25");
    ASSERT_STR_EQ(names[17], "L125");
    ASSERT_STR_EQ(names[18], "L625");
    ASSERT_STR_EQ(names[19], "L3125");
    ASSERT_NULL(names[20]);
}

// Helper function to check the balance property of an orthogonal array
void check_orthogonality(const OrthogonalArray *array) {
    if (array->cols < 2) {
        return; // Nothing to check
    }
    if (array->col_levels != NULL) {
        return; // Mixed-level arrays are checked by check_l18_orthogonality
    }

    size_t levels = array->levels;
    size_t rows = array->rows;
    const int *data = array->data;

    // The number of times each pair of levels should appear
    size_t expected_count = rows / (levels * levels);

    // Check every pair of columns
    for (size_t c1 = 0; c1 < array->cols; c1++) {
        for (size_t c2 = c1 + 1; c2 < array->cols; c2++) {
            
            // Initialize a counter for each pair of levels
            size_t pair_counts[levels][levels];
            for (size_t i = 0; i < levels; i++) {
                for (size_t j = 0; j < levels; j++) {
                    pair_counts[i][j] = 0;
                }
            }

            // Count the occurrences of each pair of levels
            for (size_t r = 0; r < rows; r++) {
                int level1 = data[r * array->cols + c1];
                int level2 = data[r * array->cols + c2];
                pair_counts[level1][level2]++;
            }

            // Check if each pair count is correct
            for (size_t i = 0; i < levels; i++) {
                for (size_t j = 0; j < levels; j++) {
                    if (pair_counts[i][j] != expected_count) {
                        printf("Orthogonality check failed for array %s\n", array->name);
                        printf("Columns: %zu and %zu\n", c1, c2);
                        printf("Levels: %zu and %zu\n", i, j);
                        printf("Expected count: %zu, but got: %zu\n", expected_count, pair_counts[i][j]);
                    }
                    ASSERT_EQ(pair_counts[i][j], expected_count);
                }
            }
        }
    }
}


TEST(l4_is_orthogonal) {
    const OrthogonalArray *array = get_array("L4");
    check_orthogonality(array);
}

TEST(l8_is_orthogonal) {
    const OrthogonalArray *array = get_array("L8");
    check_orthogonality(array);
}

TEST(l9_is_orthogonal) {
    const OrthogonalArray *array = get_array("L9");
    check_orthogonality(array);
}

TEST(l16_is_orthogonal) {
    const OrthogonalArray *array = get_array("L16");
    check_orthogonality(array);
}

TEST(l27_is_orthogonal) {
    const OrthogonalArray *array = get_array("L27");
    check_orthogonality(array);
}

TEST(get_array_l18) {
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L18");
    ASSERT_EQ(array->rows, 18);
    ASSERT_EQ(array->cols, 8);
    ASSERT_EQ(array->levels, 0);        /* 0 = mixed */
    ASSERT_NOT_NULL(array->col_levels);
    ASSERT_EQ((size_t)array->col_levels[0], 2);  /* col 0: 2-level */
    ASSERT_EQ((size_t)array->col_levels[1], 3);  /* cols 1-7: 3-level */
    ASSERT_EQ((size_t)array->col_levels[7], 3);
}

TEST(l18_col0_values_in_range) {
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        int val = array->data[r * array->cols + 0];
        ASSERT(val >= 0 && val < 2);
    }
}

TEST(l18_col1to7_values_in_range) {
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 1; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 3);
        }
    }
}

TEST(l18_col0_balanced) {
    /* Col 0 is 2-level: each level appears 9 times */
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    size_t counts[2] = {0, 0};
    for (size_t r = 0; r < array->rows; r++) {
        int val = array->data[r * array->cols + 0];
        counts[val]++;
    }
    ASSERT_EQ(counts[0], 9);
    ASSERT_EQ(counts[1], 9);
}

TEST(l18_3level_cols_balanced) {
    /* Cols 1-7 are 3-level: each level appears 6 times per column */
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    for (size_t c = 1; c < array->cols; c++) {
        size_t counts[3] = {0, 0, 0};
        for (size_t r = 0; r < array->rows; r++) {
            int val = array->data[r * array->cols + c];
            counts[val]++;
        }
        ASSERT_EQ(counts[0], 6);
        ASSERT_EQ(counts[1], 6);
        ASSERT_EQ(counts[2], 6);
    }
}

TEST(l18_is_orthogonal) {
    /* Every pair of columns should be balanced:
     * (col0, colN): each (2-level, 3-level) combination appears 3 times.
     * (colM, colN) both 3-level: each combination appears 2 times. */
    const OrthogonalArray *array = get_array("L18");
    ASSERT_NOT_NULL(array);
    for (size_t c1 = 0; c1 < array->cols; c1++) {
        for (size_t c2 = c1 + 1; c2 < array->cols; c2++) {
            int lv1 = array->col_levels[c1];
            int lv2 = array->col_levels[c2];
            size_t expected = (size_t)array->rows / ((size_t)lv1 * (size_t)lv2);
            size_t pair_counts[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
            for (size_t r = 0; r < array->rows; r++) {
                int v1 = array->data[r * array->cols + c1];
                int v2 = array->data[r * array->cols + c2];
                pair_counts[v1][v2]++;
            }
            for (int i = 0; i < lv1; i++) {
                for (int j = 0; j < lv2; j++) {
                    ASSERT_EQ(pair_counts[i][j], expected);
                }
            }
        }
    }
}

TEST(get_array_l81) {
    const OrthogonalArray *array = get_array("L81");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L81");
    ASSERT_EQ(array->rows, 81);
    ASSERT_EQ(array->cols, 40);
    ASSERT_EQ(array->levels, 3);
}

TEST(l81_values_in_range) {
    const OrthogonalArray *array = get_array("L81");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 3);
        }
    }
}

TEST(l81_is_orthogonal) {
    const OrthogonalArray *array = get_array("L81");
    check_orthogonality(array);
}

TEST(get_array_l243) {
    const OrthogonalArray *array = get_array("L243");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L243");
    ASSERT_EQ(array->rows, 243);
    ASSERT_EQ(array->cols, 121);
    ASSERT_EQ(array->levels, 3);
}

TEST(l243_values_in_range) {
    const OrthogonalArray *array = get_array("L243");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 3);
        }
    }
}

TEST(l243_is_orthogonal) {
    const OrthogonalArray *array = get_array("L243");
    check_orthogonality(array);
}

/* Tests for extended GF(2) series */
TEST(get_array_l32) {
    const OrthogonalArray *array = get_array("L32");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L32");
    ASSERT_EQ(array->rows, 32);
    ASSERT_EQ(array->cols, 31);
    ASSERT_EQ(array->levels, 2);
}

TEST(l32_values_in_range) {
    const OrthogonalArray *array = get_array("L32");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 2);
        }
    }
}

TEST(get_array_l64) {
    const OrthogonalArray *array = get_array("L64");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L64");
    ASSERT_EQ(array->rows, 64);
    ASSERT_EQ(array->cols, 63);
    ASSERT_EQ(array->levels, 2);
}

TEST(get_array_l1024) {
    const OrthogonalArray *array = get_array("L1024");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L1024");
    ASSERT_EQ(array->rows, 1024);
    ASSERT_EQ(array->cols, 1023);
    ASSERT_EQ(array->levels, 2);
}

/* Tests for extended GF(3) series */
TEST(get_array_l729) {
    const OrthogonalArray *array = get_array("L729");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L729");
    ASSERT_EQ(array->rows, 729);
    ASSERT_EQ(array->cols, 364);
    ASSERT_EQ(array->levels, 3);
}

TEST(l729_values_in_range) {
    const OrthogonalArray *array = get_array("L729");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 3);
        }
    }
}

TEST(get_array_l2187) {
    const OrthogonalArray *array = get_array("L2187");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L2187");
    ASSERT_EQ(array->rows, 2187);
    ASSERT_EQ(array->cols, 1093);
    ASSERT_EQ(array->levels, 3);
}

TEST(l2187_values_in_range) {
    const OrthogonalArray *array = get_array("L2187");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 3);
        }
    }
}

/* Tests for GF(5) series */
TEST(get_array_l25) {
    const OrthogonalArray *array = get_array("L25");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L25");
    ASSERT_EQ(array->rows, 25);
    ASSERT_EQ(array->cols, 6);
    ASSERT_EQ(array->levels, 5);
}

TEST(get_array_l125) {
    const OrthogonalArray *array = get_array("L125");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L125");
    ASSERT_EQ(array->rows, 125);
    ASSERT_EQ(array->cols, 31);
    ASSERT_EQ(array->levels, 5);
}

TEST(get_array_l625) {
    const OrthogonalArray *array = get_array("L625");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L625");
    ASSERT_EQ(array->rows, 625);
    ASSERT_EQ(array->cols, 156);
    ASSERT_EQ(array->levels, 5);
}

TEST(get_array_l3125) {
    const OrthogonalArray *array = get_array("L3125");
    ASSERT_NOT_NULL(array);
    ASSERT_STR_EQ(array->name, "L3125");
    ASSERT_EQ(array->rows, 3125);
    ASSERT_EQ(array->cols, 781);
    ASSERT_EQ(array->levels, 5);
}

TEST(l25_values_in_range) {
    const OrthogonalArray *array = get_array("L25");
    ASSERT_NOT_NULL(array);
    for (size_t r = 0; r < array->rows; r++) {
        for (size_t c = 0; c < array->cols; c++) {
            int val = array->data[r * array->cols + c];
            ASSERT(val >= 0 && val < 5);
        }
    }
}

TEST(l125_spot_check) {
    const OrthogonalArray *array = get_array("L125");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 125);
    ASSERT_EQ(array->cols, 31);
    
    /* Spot check first column pair */
    size_t counts[5][5] = {{0}};
    for (size_t r = 0; r < array->rows; r++) {
        int v1 = array->data[r * array->cols];
        int v2 = array->data[r * array->cols + 1];
        counts[v1][v2]++;
    }
    /* Each combination should appear rows/25 = 5 times */
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            ASSERT_EQ(counts[i][j], 5);
        }
    }
}

/* Orthogonality tests for extended GF(2) series */
TEST(l32_is_orthogonal) {
    const OrthogonalArray *array = get_array("L32");
    check_orthogonality(array);
}

TEST(l64_is_orthogonal) {
    const OrthogonalArray *array = get_array("L64");
    check_orthogonality(array);
}

/* Spot-check orthogonality for larger GF(2) arrays (full check too slow) */
TEST(l128_spot_check) {
    const OrthogonalArray *array = get_array("L128");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 128);
    ASSERT_EQ(array->cols, 127);
    ASSERT_EQ(array->levels, 2);
    
    /* Spot check: verify first 3 column pairs have balanced level combinations */
    for (size_t c1 = 0; c1 < 3; c1++) {
        for (size_t c2 = c1 + 1; c2 < 3; c2++) {
            size_t count_00 = 0, count_01 = 0, count_10 = 0, count_11 = 0;
            for (size_t r = 0; r < array->rows; r++) {
                int v1 = array->data[r * array->cols + c1];
                int v2 = array->data[r * array->cols + c2];
                if (v1 == 0 && v2 == 0) count_00++;
                else if (v1 == 0 && v2 == 1) count_01++;
                else if (v1 == 1 && v2 == 0) count_10++;
                else if (v1 == 1 && v2 == 1) count_11++;
            }
            /* Each combination should appear rows/4 = 32 times */
            ASSERT_EQ(count_00, 32);
            ASSERT_EQ(count_01, 32);
            ASSERT_EQ(count_10, 32);
            ASSERT_EQ(count_11, 32);
        }
    }
}

TEST(l256_spot_check) {
    const OrthogonalArray *array = get_array("L256");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 256);
    ASSERT_EQ(array->cols, 255);
    
    /* Spot check first column pair */
    size_t count_00 = 0, count_01 = 0, count_10 = 0, count_11 = 0;
    for (size_t r = 0; r < array->rows; r++) {
        int v1 = array->data[r * array->cols];
        int v2 = array->data[r * array->cols + 1];
        if (v1 == 0 && v2 == 0) count_00++;
        else if (v1 == 0 && v2 == 1) count_01++;
        else if (v1 == 1 && v2 == 0) count_10++;
        else if (v1 == 1 && v2 == 1) count_11++;
    }
    ASSERT_EQ(count_00, 64);
    ASSERT_EQ(count_01, 64);
    ASSERT_EQ(count_10, 64);
    ASSERT_EQ(count_11, 64);
}

TEST(l512_spot_check) {
    const OrthogonalArray *array = get_array("L512");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 512);
    ASSERT_EQ(array->cols, 511);
    
    /* Spot check first column pair */
    size_t count_00 = 0, count_01 = 0, count_10 = 0, count_11 = 0;
    for (size_t r = 0; r < array->rows; r++) {
        int v1 = array->data[r * array->cols];
        int v2 = array->data[r * array->cols + 1];
        if (v1 == 0 && v2 == 0) count_00++;
        else if (v1 == 0 && v2 == 1) count_01++;
        else if (v1 == 1 && v2 == 0) count_10++;
        else if (v1 == 1 && v2 == 1) count_11++;
    }
    ASSERT_EQ(count_00, 128);
    ASSERT_EQ(count_01, 128);
    ASSERT_EQ(count_10, 128);
    ASSERT_EQ(count_11, 128);
}

TEST(l1024_spot_check) {
    const OrthogonalArray *array = get_array("L1024");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 1024);
    ASSERT_EQ(array->cols, 1023);
    
    /* Spot check first column pair */
    size_t count_00 = 0, count_01 = 0, count_10 = 0, count_11 = 0;
    for (size_t r = 0; r < array->rows; r++) {
        int v1 = array->data[r * array->cols];
        int v2 = array->data[r * array->cols + 1];
        if (v1 == 0 && v2 == 0) count_00++;
        else if (v1 == 0 && v2 == 1) count_01++;
        else if (v1 == 1 && v2 == 0) count_10++;
        else if (v1 == 1 && v2 == 1) count_11++;
    }
    ASSERT_EQ(count_00, 256);
    ASSERT_EQ(count_01, 256);
    ASSERT_EQ(count_10, 256);
    ASSERT_EQ(count_11, 256);
}

/* Orthogonality tests for extended GF(3) series */
TEST(l729_is_orthogonal) {
    const OrthogonalArray *array = get_array("L729");
    check_orthogonality(array);
}

/* Spot-check for L2187 (full orthogonality check too slow) */
TEST(l2187_spot_check) {
    const OrthogonalArray *array = get_array("L2187");
    ASSERT_NOT_NULL(array);
    ASSERT_EQ(array->rows, 2187);
    ASSERT_EQ(array->cols, 1093);
    ASSERT_EQ(array->levels, 3);
    
    /* Spot check: verify first 2 column pairs have balanced level combinations */
    for (size_t c1 = 0; c1 < 2; c1++) {
        for (size_t c2 = c1 + 1; c2 < 2; c2++) {
            size_t counts[3][3] = {{0}};
            for (size_t r = 0; r < array->rows; r++) {
                int v1 = array->data[r * array->cols + c1];
                int v2 = array->data[r * array->cols + c2];
                counts[v1][v2]++;
            }
            /* Each combination should appear rows/9 = 243 times */
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    ASSERT_EQ(counts[i][j], 243);
                }
            }
        }
    }
}

TEST(columns_needed_basic) {
    /* 3-level base: 1-3 levels need 1 col, 4-9 need 2, 10-27 need 3 */
    ASSERT_EQ(columns_needed_for_factor(2, 3), 1);
    ASSERT_EQ(columns_needed_for_factor(3, 3), 1);
    ASSERT_EQ(columns_needed_for_factor(4, 3), 2);
    ASSERT_EQ(columns_needed_for_factor(9, 3), 2);
    ASSERT_EQ(columns_needed_for_factor(10, 3), 3);
    ASSERT_EQ(columns_needed_for_factor(27, 3), 3);
    /* 2-level base */
    ASSERT_EQ(columns_needed_for_factor(2, 2), 1);
    ASSERT_EQ(columns_needed_for_factor(3, 2), 2);
    ASSERT_EQ(columns_needed_for_factor(4, 2), 2);
}
