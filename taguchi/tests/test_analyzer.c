#include "test_framework.h"
#include "src/lib/analyzer.h"
#include "src/lib/parser.h"
#include "src/lib/generator.h"
#include "src/lib/arrays.h"
#include <stdlib.h>
#include <string.h>

/* Helper: build a simple 2-factor, 3-level experiment definition on L9 */
static void make_simple_def(ExperimentDef *def) {
    memset(def, 0, sizeof(ExperimentDef));
    strcpy(def->array_type, "L9");
    def->factor_count = 2;

    strcpy(def->factors[0].name, "A");
    def->factors[0].level_count = 3;
    strcpy(def->factors[0].values[0], "a1");
    strcpy(def->factors[0].values[1], "a2");
    strcpy(def->factors[0].values[2], "a3");

    strcpy(def->factors[1].name, "B");
    def->factors[1].level_count = 3;
    strcpy(def->factors[1].values[0], "b1");
    strcpy(def->factors[1].values[1], "b2");
    strcpy(def->factors[1].values[2], "b3");
}

/* Test: create_result_set and basic add */
TEST(analyzer_create_result_set) {
    ExperimentDef def;
    make_simple_def(&def);

    ResultSet *rs = create_result_set(&def, "throughput");
    ASSERT_NOT_NULL(rs);
    ASSERT_STR_EQ(rs->metric_name, "throughput");
    ASSERT_EQ(rs->count, (size_t)0);
    ASSERT_NOT_NULL(rs->responses);
    ASSERT_NOT_NULL(rs->run_ids);

    /* Add some results */
    ASSERT_EQ(add_result(rs, 1, 10.0), 0);
    ASSERT_EQ(add_result(rs, 2, 20.0), 0);
    ASSERT_EQ(rs->count, (size_t)2);

    /* Verify retrieval */
    ASSERT_DOUBLE_EQ(get_response_for_run(rs, 1), 10.0, 0.001);
    ASSERT_DOUBLE_EQ(get_response_for_run(rs, 2), 20.0, 0.001);
    ASSERT_DOUBLE_EQ(get_response_for_run(rs, 99), 0.0, 0.001); /* not found */

    free_result_set(rs);
}

/* Test: null inputs to create_result_set */
TEST(analyzer_create_null_inputs) {
    ExperimentDef def;
    make_simple_def(&def);

    ASSERT_NULL(create_result_set(NULL, "metric"));
    ASSERT_NULL(create_result_set(&def, NULL));
}

/* Test: add_result with null */
TEST(analyzer_add_result_null) {
    ASSERT_EQ(add_result(NULL, 1, 5.0), -1);
}

/* Test: result set grows beyond initial capacity */
TEST(analyzer_result_set_grows) {
    ExperimentDef def;
    make_simple_def(&def);

    ResultSet *rs = create_result_set(&def, "metric");
    ASSERT_NOT_NULL(rs);

    /* Add more than the initial capacity (16) */
    for (size_t i = 1; i <= 50; i++) {
        ASSERT_EQ(add_result(rs, i, (double)i * 1.5), 0);
    }
    ASSERT_EQ(rs->count, (size_t)50);

    /* Verify first and last */
    ASSERT_DOUBLE_EQ(get_response_for_run(rs, 1), 1.5, 0.001);
    ASSERT_DOUBLE_EQ(get_response_for_run(rs, 50), 75.0, 0.001);

    free_result_set(rs);
}

/* Test: calculate main effects for a simple L9 experiment */
TEST(analyzer_main_effects_l9) {
    ExperimentDef def;
    make_simple_def(&def);

    /* Generate runs to know the actual design */
    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);
    ASSERT_EQ(run_count, (size_t)9);

    /* Create result set and add responses.
     * Assign responses based on factor A level:
     *   a1 -> 10, a2 -> 20, a3 -> 30
     * This means factor A should show clear effect, factor B should show none.
     */
    ResultSet *rs = create_result_set(&def, "output");
    ASSERT_NOT_NULL(rs);

    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        /* Find factor A's value */
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "A") == 0) {
                if (strcmp(runs[i].values[f], "a1") == 0) response = 10.0;
                else if (strcmp(runs[i].values[f], "a2") == 0) response = 20.0;
                else if (strcmp(runs[i].values[f], "a3") == 0) response = 30.0;
                break;
            }
        }
        ASSERT_EQ(add_result(rs, runs[i].run_id, response), 0);
    }

    /* Calculate effects */
    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);
    ASSERT_EQ(effect_count, (size_t)2);

    /* Find effect for factor A */
    MainEffect *effect_a = NULL;
    MainEffect *effect_b = NULL;
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "A") == 0) effect_a = &effects[i];
        if (strcmp(effects[i].factor_name, "B") == 0) effect_b = &effects[i];
    }
    ASSERT_NOT_NULL(effect_a);
    ASSERT_NOT_NULL(effect_b);

    /* Factor A: means should be 10, 20, 30 */
    ASSERT_EQ(effect_a->level_count, (size_t)3);
    ASSERT_DOUBLE_EQ(effect_a->level_means[0], 10.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_a->level_means[1], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_a->level_means[2], 30.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_a->range, 20.0, 0.001);

    /* Factor B: response depends only on A, so all B levels should average to ~20 */
    ASSERT_EQ(effect_b->level_count, (size_t)3);
    ASSERT_DOUBLE_EQ(effect_b->level_means[0], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_b->level_means[1], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_b->level_means[2], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(effect_b->range, 0.0, 0.001);

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/* Test: calculate main effects with null inputs */
TEST(analyzer_main_effects_null) {
    MainEffect *effects = NULL;
    size_t count = 0;
    ASSERT_EQ(calculate_main_effects(NULL, &effects, &count), -1);
}

/* Test: recommend optimal levels (higher is better) */
TEST(analyzer_recommend_higher_is_better) {
    ExperimentDef def;
    make_simple_def(&def);

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);

    ResultSet *rs = create_result_set(&def, "output");
    ASSERT_NOT_NULL(rs);

    /* Assign: A=a3 is best (30), B=b1 is best (100 bonus) */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "A") == 0) {
                if (strcmp(runs[i].values[f], "a1") == 0) response += 10.0;
                else if (strcmp(runs[i].values[f], "a2") == 0) response += 20.0;
                else response += 30.0;
            }
            if (strcmp(runs[i].factor_names[f], "B") == 0) {
                if (strcmp(runs[i].values[f], "b1") == 0) response += 100.0;
                else if (strcmp(runs[i].values[f], "b2") == 0) response += 50.0;
                else response += 25.0;
            }
        }
        add_result(rs, runs[i].run_id, response);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);

    char rec[512];
    ASSERT_EQ(recommend_optimal_levels(effects, effect_count, true, rec, sizeof(rec)), 0);

    /* Should recommend A=level_3 (a3=30 highest) and B=level_1 (b1=100 highest) */
    ASSERT_NOT_NULL(strstr(rec, "A=level_3"));
    ASSERT_NOT_NULL(strstr(rec, "B=level_1"));

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/* Test: recommend optimal levels (lower is better) */
TEST(analyzer_recommend_lower_is_better) {
    ExperimentDef def;
    make_simple_def(&def);

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);

    ResultSet *rs = create_result_set(&def, "latency");
    ASSERT_NOT_NULL(rs);

    /* A=a1 is best (lowest=10), B=b3 is best (lowest=5) */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "A") == 0) {
                if (strcmp(runs[i].values[f], "a1") == 0) response += 10.0;
                else if (strcmp(runs[i].values[f], "a2") == 0) response += 20.0;
                else response += 30.0;
            }
            if (strcmp(runs[i].factor_names[f], "B") == 0) {
                if (strcmp(runs[i].values[f], "b1") == 0) response += 100.0;
                else if (strcmp(runs[i].values[f], "b2") == 0) response += 50.0;
                else response += 5.0;
            }
        }
        add_result(rs, runs[i].run_id, response);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);

    char rec[512];
    ASSERT_EQ(recommend_optimal_levels(effects, effect_count, false, rec, sizeof(rec)), 0);

    /* Should recommend A=level_1 (a1=10 lowest) and B=level_3 (b3=5 lowest) */
    ASSERT_NOT_NULL(strstr(rec, "A=level_1"));
    ASSERT_NOT_NULL(strstr(rec, "B=level_3"));

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/* Test: main effects with L27 and more factors */
TEST(analyzer_main_effects_l27) {
    ExperimentDef def;
    memset(&def, 0, sizeof(def));
    strcpy(def.array_type, "L27");
    def.factor_count = 4;

    for (size_t f = 0; f < 4; f++) {
        char name[8];
        snprintf(name, sizeof(name), "F%zu", f);
        strcpy(def.factors[f].name, name);
        def.factors[f].level_count = 3;
        for (size_t lv = 0; lv < 3; lv++) {
            char val[8];
            snprintf(val, sizeof(val), "v%zu", lv);
            strcpy(def.factors[f].values[lv], val);
        }
    }

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);
    ASSERT_EQ(run_count, (size_t)27);

    ResultSet *rs = create_result_set(&def, "perf");
    ASSERT_NOT_NULL(rs);

    /* Response depends only on F0: v0=1, v1=2, v2=3 */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "F0") == 0) {
                if (strcmp(runs[i].values[f], "v0") == 0) response = 1.0;
                else if (strcmp(runs[i].values[f], "v1") == 0) response = 2.0;
                else response = 3.0;
            }
        }
        add_result(rs, runs[i].run_id, response);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);
    ASSERT_EQ(effect_count, (size_t)4);

    /* F0 should show clear effect */
    MainEffect *f0 = NULL;
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "F0") == 0) f0 = &effects[i];
    }
    ASSERT_NOT_NULL(f0);
    ASSERT_DOUBLE_EQ(f0->level_means[0], 1.0, 0.001);
    ASSERT_DOUBLE_EQ(f0->level_means[1], 2.0, 0.001);
    ASSERT_DOUBLE_EQ(f0->level_means[2], 3.0, 0.001);
    ASSERT_DOUBLE_EQ(f0->range, 2.0, 0.001);

    /* Other factors should show no effect (all means ~2.0) */
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "F0") != 0) {
            ASSERT_DOUBLE_EQ(effects[i].range, 0.0, 0.001);
        }
    }

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/*
 * Test: 9-level factor with duplicate value strings in L81.
 *
 * The factor definition has 9 entries where three distinct strings each appear
 * three times: ["lo","lo","lo","med","med","med","hi","hi","hi"].
 * Column pairing maps OA levels 0-8 to these values, so runs 0-2 store "lo",
 * runs 3-5 store "med", and runs 6-8 store "hi".
 *
 * Bug (before fix): string matching always finds the FIRST occurrence of each
 * string: "lo"->bucket 0, "med"->bucket 3, "hi"->bucket 6.
 * Buckets 1,2,4,5,7,8 are never populated, so their means = 0.0 and the
 * computed range is inflated (30-0=30 instead of the correct 30-10=20).
 *
 * Fix: use run->level_indices[factor_idx] (the post-modulo OA index) directly.
 * Each of the 9 buckets receives data and the range is 20.0.
 */
TEST(analyzer_duplicate_values_9level) {
    ExperimentDef def;
    memset(&def, 0, sizeof(def));
    strcpy(def.array_type, "L81");
    def.factor_count = 2;

    /* Factor N: 9 entries, 3 unique strings (each repeated 3×) */
    strcpy(def.factors[0].name, "N");
    def.factors[0].level_count = 9;
    strcpy(def.factors[0].values[0], "lo");
    strcpy(def.factors[0].values[1], "lo");
    strcpy(def.factors[0].values[2], "lo");
    strcpy(def.factors[0].values[3], "med");
    strcpy(def.factors[0].values[4], "med");
    strcpy(def.factors[0].values[5], "med");
    strcpy(def.factors[0].values[6], "hi");
    strcpy(def.factors[0].values[7], "hi");
    strcpy(def.factors[0].values[8], "hi");

    /* Factor M: 3 distinct levels (filler) */
    strcpy(def.factors[1].name, "M");
    def.factors[1].level_count = 3;
    strcpy(def.factors[1].values[0], "m0");
    strcpy(def.factors[1].values[1], "m1");
    strcpy(def.factors[1].values[2], "m2");

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);
    ASSERT_EQ(run_count, (size_t)81);

    ResultSet *rs = create_result_set(&def, "perf");
    ASSERT_NOT_NULL(rs);

    /* Response = 10 for "lo", 20 for "med", 30 for "hi" */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "N") == 0) {
                if (strcmp(runs[i].values[f], "lo") == 0)       response = 10.0;
                else if (strcmp(runs[i].values[f], "med") == 0) response = 20.0;
                else                                             response = 30.0;
            }
        }
        ASSERT_EQ(add_result(rs, runs[i].run_id, response), 0);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);

    /* Locate factor N effect */
    MainEffect *en = NULL;
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "N") == 0) en = &effects[i];
    }
    ASSERT_NOT_NULL(en);
    ASSERT_EQ(en->level_count, (size_t)9);

    /* All 9 buckets must be non-zero and consistent with their value group.
     * Levels 0-2 map to "lo" (10), 3-5 to "med" (20), 6-8 to "hi" (30).
     * Before the fix, buckets 1,2,4,5,7,8 would be 0.0. */
    ASSERT_DOUBLE_EQ(en->level_means[0], 10.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[1], 10.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[2], 10.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[3], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[4], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[5], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[6], 30.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[7], 30.0, 0.001);
    ASSERT_DOUBLE_EQ(en->level_means[8], 30.0, 0.001);

    /* Range must be 20 (hi - lo), not 30 (which the bug would produce
     * because empty buckets have mean 0 and 30 - 0 = 30). */
    ASSERT_DOUBLE_EQ(en->range, 20.0, 0.001);

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/*
 * Test: 5-level factor with duplicate value strings in L25.
 *
 * Factor definition: ["a","b","a","b","c"] — 5 entries, 3 unique, with "a" at
 * positions 0 and 2, and "b" at positions 1 and 3.  L25 is a 5-level base array
 * so the factor uses a single column.  OA levels 0-4 map directly to the 5 entries.
 *
 * Bug (before fix): "a" matched at lv=0 (first hit), buckets 2 and 3 always empty.
 * Fix: OA level index routes each run to the correct bucket (0-4).
 */
TEST(analyzer_duplicate_values_5level) {
    ExperimentDef def;
    memset(&def, 0, sizeof(def));
    strcpy(def.array_type, "L25");
    def.factor_count = 2;

    /* Factor P: 5 entries, ["a","b","a","b","c"] */
    strcpy(def.factors[0].name, "P");
    def.factors[0].level_count = 5;
    strcpy(def.factors[0].values[0], "a");
    strcpy(def.factors[0].values[1], "b");
    strcpy(def.factors[0].values[2], "a");  /* duplicate "a" at index 2 */
    strcpy(def.factors[0].values[3], "b");  /* duplicate "b" at index 3 */
    strcpy(def.factors[0].values[4], "c");

    /* Factor Q: 5 distinct levels */
    strcpy(def.factors[1].name, "Q");
    def.factors[1].level_count = 5;
    strcpy(def.factors[1].values[0], "q0");
    strcpy(def.factors[1].values[1], "q1");
    strcpy(def.factors[1].values[2], "q2");
    strcpy(def.factors[1].values[3], "q3");
    strcpy(def.factors[1].values[4], "q4");

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);
    ASSERT_EQ(run_count, (size_t)25);

    ResultSet *rs = create_result_set(&def, "score");
    ASSERT_NOT_NULL(rs);

    /* Response = 10 if "a", 20 if "b", 30 if "c" */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "P") == 0) {
                if (strcmp(runs[i].values[f], "a") == 0)      response = 10.0;
                else if (strcmp(runs[i].values[f], "b") == 0) response = 20.0;
                else                                           response = 30.0;
            }
        }
        ASSERT_EQ(add_result(rs, runs[i].run_id, response), 0);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);

    /* Locate factor P effect */
    MainEffect *ep = NULL;
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "P") == 0) ep = &effects[i];
    }
    ASSERT_NOT_NULL(ep);
    ASSERT_EQ(ep->level_count, (size_t)5);

    /* Buckets 0 and 2 receive "a" runs (response 10).
     * Buckets 1 and 3 receive "b" runs (response 20).
     * Bucket 4 receives "c" runs (response 30).
     * Before the fix, buckets 2 and 3 would be 0.0. */
    ASSERT_DOUBLE_EQ(ep->level_means[0], 10.0, 0.001);
    ASSERT_DOUBLE_EQ(ep->level_means[1], 20.0, 0.001);
    ASSERT_DOUBLE_EQ(ep->level_means[2], 10.0, 0.001);  /* would be 0 before fix */
    ASSERT_DOUBLE_EQ(ep->level_means[3], 20.0, 0.001);  /* would be 0 before fix */
    ASSERT_DOUBLE_EQ(ep->level_means[4], 30.0, 0.001);

    /* Range = 30 - 10 = 20.  The bug inflates this to 30 - 0 = 30. */
    ASSERT_DOUBLE_EQ(ep->range, 20.0, 0.001);

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}

/* Test: main effects with paired columns (9-level factor in L81) */
TEST(analyzer_main_effects_paired) {
    ExperimentDef def;
    memset(&def, 0, sizeof(def));
    strcpy(def.array_type, "L81");
    def.factor_count = 2;

    /* 9-level factor (needs column pairing: 2 cols in base-3) */
    strcpy(def.factors[0].name, "X");
    def.factors[0].level_count = 9;
    for (size_t lv = 0; lv < 9; lv++) {
        char val[8];
        snprintf(val, sizeof(val), "x%zu", lv);
        strcpy(def.factors[0].values[lv], val);
    }

    /* 3-level factor */
    strcpy(def.factors[1].name, "Y");
    def.factors[1].level_count = 3;
    strcpy(def.factors[1].values[0], "y0");
    strcpy(def.factors[1].values[1], "y1");
    strcpy(def.factors[1].values[2], "y2");

    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char err[256];
    ASSERT_EQ(generate_experiments(&def, &runs, &run_count, err), 0);
    ASSERT_EQ(run_count, (size_t)81);

    ResultSet *rs = create_result_set(&def, "temp");
    ASSERT_NOT_NULL(rs);

    /* Response = X level index (0-8), so X shows clear graduated effect */
    for (size_t i = 0; i < run_count; i++) {
        double response = 0.0;
        for (size_t f = 0; f < runs[i].factor_count; f++) {
            if (strcmp(runs[i].factor_names[f], "X") == 0) {
                /* Parse the level index from "xN" */
                response = (double)(runs[i].values[f][1] - '0');
            }
        }
        add_result(rs, runs[i].run_id, response);
    }

    MainEffect *effects = NULL;
    size_t effect_count = 0;
    ASSERT_EQ(calculate_main_effects(rs, &effects, &effect_count), 0);
    ASSERT_EQ(effect_count, (size_t)2);

    /* Find factor X */
    MainEffect *ex = NULL;
    for (size_t i = 0; i < effect_count; i++) {
        if (strcmp(effects[i].factor_name, "X") == 0) ex = &effects[i];
    }
    ASSERT_NOT_NULL(ex);
    ASSERT_EQ(ex->level_count, (size_t)9);

    /* Each level mean should equal the level index (0.0, 1.0, ..., 8.0) */
    for (size_t lv = 0; lv < 9; lv++) {
        ASSERT_DOUBLE_EQ(ex->level_means[lv], (double)lv, 0.001);
    }
    ASSERT_DOUBLE_EQ(ex->range, 8.0, 0.001);

    free_main_effects(effects, effect_count);
    free_result_set(rs);
    free_experiments(runs, run_count);
}
