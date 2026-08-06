#include "analyzer.h"
#include "utils.h"
#include "arrays.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

/* Create result set for collecting experimental data */
ResultSet *create_result_set(const ExperimentDef *def, const char *metric_name) {
    if (!def || !metric_name) return NULL;

    ResultSet *results = xmalloc(sizeof(ResultSet));
    memset(results, 0, sizeof(ResultSet));

    results->capacity = 16;
    results->responses = xmalloc(results->capacity * sizeof(double));
    results->run_ids = xmalloc(results->capacity * sizeof(size_t));
    results->count = 0;

    if (strlen(metric_name) >= MAX_FACTOR_NAME) {
        free(results->responses);
        free(results->run_ids);
        free(results);
        return NULL;
    }

    strcpy(results->metric_name, metric_name);
    results->experiment_def = (ExperimentDef *)def;

    return results;
}

/* Add result to set */
int add_result(ResultSet *results, size_t run_id, double response_value) {
    if (!results) return -1;

    if (results->count >= results->capacity) {
        results->capacity *= 2;
        results->responses = xrealloc(results->responses, results->capacity * sizeof(double));
        results->run_ids = xrealloc(results->run_ids, results->capacity * sizeof(size_t));
    }

    results->run_ids[results->count] = run_id;
    results->responses[results->count] = response_value;
    results->count++;

    return 0;
}

/* Get the response value for a particular run ID */
double get_response_for_run(const ResultSet *results, size_t run_id) {
    if (!results) return 0.0;

    for (size_t i = 0; i < results->count; i++) {
        if (results->run_ids[i] == run_id) {
            return results->responses[i];
        }
    }

    return 0.0;
}

/* Free result set */
void free_result_set(ResultSet *results) {
    if (results) {
        free(results->responses);
        free(results->run_ids);
        results->experiment_def = NULL;
        free(results);
    }
}

/*
 * Calculate main effects from results and experiment design.
 *
 * This regenerates the experiment runs from the stored definition to
 * determine which level of each factor was used in each run, then
 * groups responses by factor level and computes means.
 */
int calculate_main_effects(const ResultSet *results, MainEffect **effects_out, size_t *count_out) {
    if (!results || !effects_out || !count_out || !results->experiment_def) {
        return -1;
    }

    const ExperimentDef *def = results->experiment_def;

    /* Regenerate the runs to get the factor-level mapping */
    ExperimentRun *runs = NULL;
    size_t run_count = 0;
    char error_buf[256];
    if (generate_experiments(def, &runs, &run_count, error_buf) != 0) {
        return -1;
    }

    /* Create effects array - one per factor */
    MainEffect *effects = xmalloc(def->factor_count * sizeof(MainEffect));

    for (size_t factor_idx = 0; factor_idx < def->factor_count; factor_idx++) {
        MainEffect *effect = &effects[factor_idx];
        memset(effect, 0, sizeof(MainEffect));

        const Factor *factor = &def->factors[factor_idx];
        strcpy(effect->factor_name, factor->name);
        effect->level_count = factor->level_count;
        effect->level_means = xmalloc(factor->level_count * sizeof(double));

        double *level_sums = xcalloc(factor->level_count, sizeof(double));
        size_t *level_counts = xcalloc(factor->level_count, sizeof(size_t));

        /* For each result, find the corresponding run and determine
           which level of this factor was used */
        for (size_t result_idx = 0; result_idx < results->count; result_idx++) {
            size_t run_id = results->run_ids[result_idx];
            double response = results->responses[result_idx];

            /* Find the run with this ID (runs are 1-indexed) */
            if (run_id < 1 || run_id > run_count) continue;
            const ExperimentRun *run = &runs[run_id - 1];

            /* Use the stored OA level index directly.
             * String matching would pick the first occurrence of a duplicate
             * value string, leaving other buckets at 0.  The level index is
             * the authoritative bucket regardless of repeated value strings. */
            size_t lv = run->level_indices[factor_idx];
            if (lv < factor->level_count) {
                level_sums[lv] += response;
                level_counts[lv]++;
            }
        }

        /* Calculate means for each level */
        for (size_t lv = 0; lv < factor->level_count; lv++) {
            if (level_counts[lv] > 0) {
                effect->level_means[lv] = level_sums[lv] / (double)level_counts[lv];
            } else {
                effect->level_means[lv] = 0.0;
            }
        }

        /* Calculate range (max - min) */
        if (factor->level_count > 0) {
            double min_val = effect->level_means[0];
            double max_val = effect->level_means[0];
            for (size_t lv = 1; lv < factor->level_count; lv++) {
                if (effect->level_means[lv] < min_val) min_val = effect->level_means[lv];
                if (effect->level_means[lv] > max_val) max_val = effect->level_means[lv];
            }
            effect->range = max_val - min_val;
        }

        free(level_sums);
        free(level_counts);
    }

    free_experiments(runs, run_count);

    *effects_out = effects;
    *count_out = def->factor_count;
    return 0;
}

/* Free main effects */
void free_main_effects(MainEffect *effects, size_t count) {
    if (effects) {
        for (size_t i = 0; i < count; i++) {
            free(effects[i].level_means);
        }
        free(effects);
    }
}

/* Recommend optimal levels based on effects */
int recommend_optimal_levels(const MainEffect *effects, size_t effect_count,
                            bool higher_is_better,
                            char *recommendation_buf, size_t buf_size) {
    if (!effects || !recommendation_buf || buf_size == 0) {
        return -1;
    }

    size_t pos = 0;
    for (size_t i = 0; i < effect_count; i++) {
        const MainEffect *effect = &effects[i];
        if (effect->level_count == 0) continue;

        size_t best_idx = 0;
        double best_val = effect->level_means[0];
        for (size_t lv = 1; lv < effect->level_count; lv++) {
            bool is_better = higher_is_better
                ? (effect->level_means[lv] > best_val)
                : (effect->level_means[lv] < best_val);
            if (is_better) {
                best_val = effect->level_means[lv];
                best_idx = lv;
            }
        }

        if (i > 0 && pos < buf_size) {
            pos += (size_t)snprintf(recommendation_buf + pos, buf_size - pos, ", ");
        }
        if (pos < buf_size) {
            pos += (size_t)snprintf(recommendation_buf + pos, buf_size - pos,
                "%s=level_%zu", effect->factor_name, best_idx + 1);
        }
    }

    return 0;
}
