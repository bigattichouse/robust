/*
 * test_generator.c — the parts of src/lib/generator.c nothing reached.
 *
 * `make coverage` put generator.c at 39%. The existing suites drive the public
 * API, which always names an array, so three regions had no coverage at all:
 *
 *   - get_suggested_array_for_factors(), the private auto-selection helper
 *     used when a definition names no array. This is NOT the same code as the
 *     public taguchi_suggest_optimal_array() the other tests exercise; the two
 *     are separate implementations of the same idea, and only the public one
 *     was tested.
 *   - the mixed-level (L18) column-assignment branch.
 *   - the NULL-parameter and unknown-array guards.
 *
 * These tests use the internal headers directly, as test_analyzer.c does.
 */

#include "test_framework.h"
#include "src/lib/generator.h"
#include "src/lib/arrays.h"
#include <stdlib.h>
#include <string.h>

/* A definition with `nfactors` factors of `levels` levels each, and no array
 * named -- which is what forces the auto-selection path. */
static void make_def(ExperimentDef *def, size_t nfactors, size_t levels,
                     const char *array_type) {
    memset(def, 0, sizeof(ExperimentDef));
    /* array_type is char[8] -- bound the copy, do not strcpy. */
    if (array_type)
        snprintf(def->array_type, sizeof def->array_type, "%s", array_type);
    def->factor_count = nfactors;
    for (size_t i = 0; i < nfactors; i++) {
        snprintf(def->factors[i].name, MAX_FACTOR_NAME, "F%zu", i);
        def->factors[i].level_count = levels;
        for (size_t j = 0; j < levels; j++)
            snprintf(def->factors[i].values[j], MAX_LEVEL_VALUE, "f%zul%zu", i, j);
    }
}

/* ---- auto-selection (no array named) --------------------------------- */

/* The headline gap: generating with no array_type must pick one and work. */
TEST(generator_auto_selects_for_3level) {
    ExperimentDef def;
    make_def(&def, 3, 3, NULL);          /* no array named */

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);
    ASSERT_GT(count, (size_t)0);

    /* Every run must carry every factor, with a value drawn from that
     * factor's own level list -- the mapping must not go out of range. */
    for (size_t r = 0; r < count; r++) {
        ASSERT_EQ(runs[r].factor_count, (size_t)3);
        ASSERT_EQ(runs[r].run_id, r + 1);
        for (size_t f = 0; f < 3; f++) {
            ASSERT_LT(runs[r].level_indices[f], (size_t)3);
            ASSERT_STR_EQ(runs[r].values[f],
                          def.factors[f].values[runs[r].level_indices[f]]);
        }
    }
    free_experiments(runs, count);
}

TEST(generator_auto_selects_for_2level) {
    ExperimentDef def;
    make_def(&def, 3, 2, NULL);

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);
    ASSERT_GT(count, (size_t)0);
    for (size_t r = 0; r < count; r++)
        for (size_t f = 0; f < 3; f++)
            ASSERT_LT(runs[r].level_indices[f], (size_t)2);
    free_experiments(runs, count);
}

/* Auto-selection must scale: more factors, still a usable design. */
TEST(generator_auto_selects_for_many_factors) {
    ExperimentDef def;
    make_def(&def, 10, 3, NULL);

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);
    ASSERT_GT(count, (size_t)0);
    ASSERT_EQ(runs[0].factor_count, (size_t)10);
    free_experiments(runs, count);
}

/* Every level of every factor must actually appear -- an auto-selected array
 * that silently dropped a level would still "work" but be useless. */
TEST(generator_auto_selection_covers_all_levels) {
    ExperimentDef def;
    make_def(&def, 4, 3, NULL);

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);

    for (size_t f = 0; f < 4; f++) {
        int seen[3] = {0, 0, 0};
        for (size_t r = 0; r < count; r++) seen[runs[r].level_indices[f]] = 1;
        ASSERT_TRUE(seen[0] && seen[1] && seen[2]);
    }
    free_experiments(runs, count);
}

/* ---- mixed-level arrays ---------------------------------------------- */

/* L18 has one 2-level column and seven 3-level columns, taking the branch
 * that assigns each factor to a column of exactly matching level count. */
TEST(generator_mixed_level_l18) {
    ExperimentDef def;
    memset(&def, 0, sizeof def);
    strcpy(def.array_type, "L18");
    def.factor_count = 3;

    strcpy(def.factors[0].name, "two");
    def.factors[0].level_count = 2;
    strcpy(def.factors[0].values[0], "lo");
    strcpy(def.factors[0].values[1], "hi");

    for (size_t i = 1; i < 3; i++) {
        snprintf(def.factors[i].name, MAX_FACTOR_NAME, "three%zu", i);
        def.factors[i].level_count = 3;
        for (size_t j = 0; j < 3; j++)
            snprintf(def.factors[i].values[j], MAX_LEVEL_VALUE, "v%zu%zu", i, j);
    }

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);
    ASSERT_EQ(count, (size_t)18);

    /* The 2-level factor must only ever take its two levels, and each of the
     * 3-level factors all three. */
    int seen2[2] = {0, 0};
    for (size_t r = 0; r < count; r++) {
        ASSERT_LT(runs[r].level_indices[0], (size_t)2);
        seen2[runs[r].level_indices[0]] = 1;
        for (size_t f = 1; f < 3; f++) ASSERT_LT(runs[r].level_indices[f], (size_t)3);
    }
    ASSERT_TRUE(seen2[0] && seen2[1]);
    free_experiments(runs, count);
}

/* A factor whose level count matches no column in a mixed-level array must be
 * refused, with the array named. */
TEST(generator_mixed_level_rejects_unmatchable_factor) {
    ExperimentDef def;
    memset(&def, 0, sizeof def);
    strcpy(def.array_type, "L18");
    def.factor_count = 1;
    strcpy(def.factors[0].name, "five");
    def.factors[0].level_count = 5;          /* no 5-level column exists */
    for (size_t j = 0; j < 5; j++)
        snprintf(def.factors[0].values[j], MAX_LEVEL_VALUE, "v%zu", j);

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    memset(err, 'A', sizeof err);
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));
    ASSERT_NOT_NULL(strstr(err, "L18"));
}

/* ---- check_array_compatibility --------------------------------------- */

TEST(generator_compat_null_parameters) {
    ExperimentDef def;
    make_def(&def, 2, 3, "L9");
    const OrthogonalArray *l9 = get_array("L9");
    ASSERT_NOT_NULL(l9);

    char err[256];
    ASSERT_TRUE(check_array_compatibility(&def, l9, err));

    memset(err, 'A', sizeof err);
    ASSERT_FALSE(check_array_compatibility(NULL, l9, err));
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    memset(err, 'A', sizeof err);
    ASSERT_FALSE(check_array_compatibility(&def, NULL, err));
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    /* A NULL error buffer must not crash. */
    ASSERT_FALSE(check_array_compatibility(NULL, NULL, NULL));
}

/* Too many factors for the named array: refused, and the message states both
 * the available and the needed column counts. */
TEST(generator_compat_array_too_small) {
    ExperimentDef def;
    make_def(&def, 12, 3, "L9");     /* L9 has 4 columns */
    const OrthogonalArray *l9 = get_array("L9");
    ASSERT_NOT_NULL(l9);

    char err[256];
    memset(err, 'A', sizeof err);
    ASSERT_FALSE(check_array_compatibility(&def, l9, err));
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));
    ASSERT_NOT_NULL(strstr(err, "L9"));
}

/* ---- generate_experiments guards -------------------------------------- */

TEST(generator_rejects_null_parameters) {
    ExperimentDef def;
    make_def(&def, 2, 3, "L9");
    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];

    memset(err, 'A', sizeof err);
    ASSERT_EQ(generate_experiments(NULL, &runs, &count, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    memset(err, 'A', sizeof err);
    ASSERT_EQ(generate_experiments(&def, NULL, &count, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    memset(err, 'A', sizeof err);
    ASSERT_EQ(generate_experiments(&def, &runs, NULL, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    /* NULL error buffer must not crash either. */
    ASSERT_EQ(generate_experiments(NULL, NULL, NULL, NULL), -1);
}

TEST(generator_rejects_unknown_array) {
    ExperimentDef def;
    make_def(&def, 2, 3, "L999");
    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    memset(err, 'A', sizeof err);
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));
    ASSERT_NOT_NULL(strstr(err, "L999"));
}

/* free_experiments must tolerate NULL, as every free-like function should. */
TEST(generator_free_null_is_safe) {
    free_experiments(NULL, 0);
    free_experiments(NULL, 99);
}

/* ---- column pairing ---------------------------------------------------- */

/* A factor with more levels than the array's base needs several columns
 * combined. Whatever the arithmetic, the resulting index must stay inside the
 * factor's level list -- that is the property a user depends on. */
TEST(generator_column_pairing_stays_in_range) {
    ExperimentDef def;
    memset(&def, 0, sizeof def);
    strcpy(def.array_type, "L27");     /* 3-level base */
    def.factor_count = 1;
    strcpy(def.factors[0].name, "wide");
    def.factors[0].level_count = 9;    /* needs 2 paired columns */
    for (size_t j = 0; j < 9; j++)
        snprintf(def.factors[0].values[j], MAX_LEVEL_VALUE, "w%zu", j);

    ExperimentRun *runs = NULL;
    size_t count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &count, err), 0);
    ASSERT_EQ(count, (size_t)27);

    int seen[9];
    memset(seen, 0, sizeof seen);
    for (size_t r = 0; r < count; r++) {
        ASSERT_LT(runs[r].level_indices[0], (size_t)9);
        ASSERT_STR_EQ(runs[r].values[0],
                      def.factors[0].values[runs[r].level_indices[0]]);
        seen[runs[r].level_indices[0]] = 1;
    }
    for (size_t i = 0; i < 9; i++) ASSERT_TRUE(seen[i]);
    free_experiments(runs, count);
}
