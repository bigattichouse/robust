#ifndef ANALYZER_H
#define ANALYZER_H

#include <stddef.h>
#include <stdbool.h>
#include "parser.h"      // For ExperimentDef
#include "generator.h"   // For ExperimentRun
#include "../config.h"   // For constants

/* Result set structure for holding experimental data */
typedef struct {
    double *responses;      /* Response values for each run */
    size_t *run_ids;        /* Run IDs corresponding to responses */
    size_t count;           /* Number of results entered */
    size_t capacity;        /* Total allocated capacity */
    char metric_name[MAX_FACTOR_NAME];  /* Name of measured metric */

    /* Store a reference to the experiment definition to tie results to design */
    ExperimentDef *experiment_def;
} ResultSet;

/* Main effect structure for statistical analysis */
typedef struct {
    char factor_name[MAX_FACTOR_NAME];  /* Name of the factor */
    double *level_means;               /* Mean response for each level */
    size_t level_count;                /* Number of levels */
    double range;                      /* Range (max - min) of level means */
} MainEffect;

/* Create result set for collecting experimental data */
ResultSet *create_result_set(const ExperimentDef *def, const char *metric_name);

/* Add result to set */
int add_result(ResultSet *results, size_t run_id, double response_value);

/* Get the response value for a particular run ID */
double get_response_for_run(const ResultSet *results, size_t run_id);

/* Free result set */
void free_result_set(ResultSet *results);

/* Calculate main effects from results and experiment design */
int calculate_main_effects(
    const ResultSet *results,
    MainEffect **effects_out,
    size_t *count_out
);

/* Free main effects */
void free_main_effects(MainEffect *effects, size_t count);

/* Recommend optimal levels based on effects */
int recommend_optimal_levels(
    const MainEffect *effects,
    size_t effect_count,
    bool higher_is_better,
    char *recommendation_buf,
    size_t buf_size
);

#endif /* ANALYZER_H */