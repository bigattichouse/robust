/*
 * test_public_api.c — the programmatic C API in include/taguchi.h.
 *
 * `make coverage` put taguchi.c at 63%, and the reason turned out to matter
 * more than the number: an entire half of the public API had no test at any
 * level. Nothing called taguchi_create_definition/taguchi_add_factor (build a
 * design in code rather than parsing a file), and nothing called the whole
 * analysis chain -- create_result_set, add_result, calculate_main_effects,
 * recommend_optimal, effects_to_json.
 *
 * That is exactly the surface the Python and Node bindings drive. The existing
 * suites all go through taguchi_parse_definition, which is the file path, so
 * the API a binding consumer depends on was untested in C.
 *
 * These tests walk the full workflow the way a binding does: build a
 * definition in memory, generate runs, feed measured responses back, compute
 * effects, and read the recommendation out.
 */

#include "test_framework.h"
#include "taguchi.h"
#include <stdlib.h>
#include <string.h>

/* ---- definition building ---------------------------------------------- */

TEST(api_create_definition_and_add_factors) {
    char err[256];
    taguchi_experiment_def_t *def = taguchi_create_definition("L9");
    ASSERT_NOT_NULL(def);
    ASSERT_EQ(taguchi_def_get_factor_count(def), (size_t)0);

    const char *temp[] = {"cold", "warm", "hot"};
    ASSERT_EQ(taguchi_add_factor(def, "temp", temp, 3, err), 0);
    ASSERT_EQ(taguchi_def_get_factor_count(def), (size_t)1);

    const char *ph[] = {"low", "mid", "high"};
    ASSERT_EQ(taguchi_add_factor(def, "ph", ph, 3, err), 0);
    ASSERT_EQ(taguchi_def_get_factor_count(def), (size_t)2);

    ASSERT_TRUE(taguchi_validate_definition(def, err));
    taguchi_free_definition(def);
}

TEST(api_add_factor_rejects_bad_input) {
    char err[256];
    taguchi_experiment_def_t *def = taguchi_create_definition("L9");
    ASSERT_NOT_NULL(def);
    const char *lv[] = {"a", "b", "c"};

    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_add_factor(NULL, "f", lv, 3, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_add_factor(def, NULL, lv, 3, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_add_factor(def, "f", NULL, 3, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    /* Zero levels is not a factor. */
    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_add_factor(def, "f", lv, 0, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    taguchi_free_definition(def);
}

TEST(api_create_definition_null_array_is_auto) {
    /* NULL array type means "choose one for me" -- the binding-facing way to
     * request auto-selection. It must still produce a usable design. */
    char err[256];
    taguchi_experiment_def_t *def = taguchi_create_definition(NULL);
    ASSERT_NOT_NULL(def);

    const char *lv[] = {"a", "b", "c"};
    ASSERT_EQ(taguchi_add_factor(def, "f1", lv, 3, err), 0);
    ASSERT_EQ(taguchi_add_factor(def, "f2", lv, 3, err), 0);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;
    ASSERT_EQ(taguchi_generate_runs(def, &runs, &count, err), 0);
    ASSERT_GT(count, (size_t)0);
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

/* ---- the full workflow a binding performs ------------------------------ */

/*
 * Build in code, generate, measure, analyse, recommend. The response is
 * constructed so the answer is known: factor "A" drives it (a3 best), factor
 * "B" is inert. A correct analysis must rank A above B and recommend a3.
 */
TEST(api_full_workflow_effects_and_recommendation) {
    char err[256];
    taguchi_experiment_def_t *def = taguchi_create_definition("L9");
    ASSERT_NOT_NULL(def);

    const char *a[] = {"a1", "a2", "a3"};
    const char *b[] = {"b1", "b2", "b3"};
    ASSERT_EQ(taguchi_add_factor(def, "A", a, 3, err), 0);
    ASSERT_EQ(taguchi_add_factor(def, "B", b, 3, err), 0);

    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;
    ASSERT_EQ(taguchi_generate_runs(def, &runs, &count, err), 0);
    ASSERT_EQ(count, (size_t)9);

    taguchi_result_set_t *rs = taguchi_create_result_set(def, "yield");
    ASSERT_NOT_NULL(rs);

    for (size_t i = 0; i < count; i++) {
        size_t nf = taguchi_run_get_factor_count(runs[i]);
        ASSERT_EQ(nf, (size_t)2);

        double response = 0.0;
        for (size_t f = 0; f < nf; f++) {
            const char *name = taguchi_run_get_factor_name_at_index(runs[i], f);
            ASSERT_NOT_NULL(name);
            const char *val  = taguchi_run_get_value(runs[i], name);
            ASSERT_NOT_NULL(val);
            if (strcmp(name, "A") == 0) {
                if      (strcmp(val, "a1") == 0) response = 10.0;
                else if (strcmp(val, "a2") == 0) response = 20.0;
                else                              response = 30.0;
            }
        }
        ASSERT_EQ(taguchi_add_result(rs, taguchi_run_get_id(runs[i]), response, err), 0);
    }

    taguchi_main_effect_t **effects = NULL;
    size_t ecount = 0;
    ASSERT_EQ(taguchi_calculate_main_effects(rs, &effects, &ecount, err), 0);
    ASSERT_EQ(ecount, (size_t)2);

    /* A must show a real range; B, driving nothing, must show ~none. */
    double range_a = -1.0, range_b = -1.0;
    for (size_t i = 0; i < ecount; i++) {
        const char *fn = taguchi_effect_get_factor(effects[i]);
        ASSERT_NOT_NULL(fn);
        if (strcmp(fn, "A") == 0) range_a = taguchi_effect_get_range(effects[i]);
        if (strcmp(fn, "B") == 0) range_b = taguchi_effect_get_range(effects[i]);
    }
    ASSERT_DOUBLE_EQ(range_a, 20.0, 0.001);      /* 30 - 10 */
    ASSERT_DOUBLE_EQ(range_b, 0.0, 0.001);
    ASSERT_GT(range_a, range_b);

    /* Level means for A must be exactly the three response levels. */
    for (size_t i = 0; i < ecount; i++) {
        if (strcmp(taguchi_effect_get_factor(effects[i]), "A") != 0) continue;
        size_t nlev = 0;
        const double *means = taguchi_effect_get_level_means(effects[i], &nlev);
        ASSERT_NOT_NULL(means);
        ASSERT_EQ(nlev, (size_t)3);
        ASSERT_DOUBLE_EQ(means[0], 10.0, 0.001);
        ASSERT_DOUBLE_EQ(means[1], 20.0, 0.001);
        ASSERT_DOUBLE_EQ(means[2], 30.0, 0.001);
    }

    /*
     * The recommendation names LEVEL INDICES, not the level values: the format
     * is "A=level_3, B=level_1", 1-based. Worth knowing when writing a
     * binding -- the caller has to map the index back to its own value list,
     * which taguchi_effect_get_level_means's ordering makes possible.
     * A=level_3 is the a3 row (response 30), so maximising must pick it and
     * minimising must pick level_1.
     */
    char rec[1024];
    ASSERT_EQ(taguchi_recommend_optimal((const taguchi_main_effect_t **)effects,
                                        ecount, true, rec, sizeof rec), 0);
    ASSERT_NOT_NULL(strstr(rec, "A=level_3"));

    ASSERT_EQ(taguchi_recommend_optimal((const taguchi_main_effect_t **)effects,
                                        ecount, false, rec, sizeof rec), 0);
    ASSERT_NOT_NULL(strstr(rec, "A=level_1"));

    /* JSON serialisation -- what a binding actually returns to its caller. */
    char *json = taguchi_effects_to_json((const taguchi_main_effect_t **)effects, ecount);
    ASSERT_NOT_NULL(json);
    ASSERT_GT(strlen(json), (size_t)1);
    taguchi_free_string(json);

    char *rjson = taguchi_runs_to_json((const taguchi_experiment_run_t **)runs, count);
    ASSERT_NOT_NULL(rjson);
    ASSERT_EQ(rjson[0], '[');
    ASSERT_NOT_NULL(strstr(rjson, "\"A\""));
    taguchi_free_string(rjson);

    taguchi_free_effects(effects, ecount);
    taguchi_free_result_set(rs);
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
}

/* ---- guards on the analysis API ---------------------------------------- */

TEST(api_analysis_rejects_null) {
    char err[256];
    ASSERT_NULL(taguchi_create_result_set(NULL, "m"));

    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_add_result(NULL, 1, 1.0, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    taguchi_main_effect_t **effects = NULL;
    size_t ecount = 0;
    memset(err, 'A', sizeof err);
    ASSERT_EQ(taguchi_calculate_main_effects(NULL, &effects, &ecount, err), -1);
    ASSERT_NOT_NULL(memchr(err, '\0', sizeof err));

    /* Every free must tolerate NULL, as a binding's finaliser may run twice. */
    taguchi_free_result_set(NULL);
    taguchi_free_effects(NULL, 0);
    taguchi_free_definition(NULL);
    taguchi_free_string(NULL);
}

TEST(api_recommend_rejects_bad_buffer) {
    char rec[8];
    /* No effects to recommend from. */
    ASSERT_EQ(taguchi_recommend_optimal(NULL, 0, true, rec, sizeof rec), -1);
    /* A NULL output buffer must not be written through. */
    ASSERT_EQ(taguchi_recommend_optimal(NULL, 1, true, NULL, 0), -1);
}

TEST(api_json_of_nothing_is_empty_array) {
    char *j = taguchi_effects_to_json(NULL, 0);
    ASSERT_NOT_NULL(j);
    taguchi_free_string(j);

    j = taguchi_runs_to_json(NULL, 0);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(j, "[]");
    taguchi_free_string(j);
}
