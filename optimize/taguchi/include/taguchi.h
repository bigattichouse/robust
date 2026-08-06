#ifndef TAGUCHI_H
#define TAGUCHI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/* Library version */
#define TAGUCHI_VERSION_MAJOR 1
#define TAGUCHI_VERSION_MINOR 5
#define TAGUCHI_VERSION_PATCH 0

/* Error buffer size */
#define TAGUCHI_ERROR_SIZE 256

/* Opaque handles - never exposed to user */
typedef struct taguchi_experiment_def taguchi_experiment_def_t;
typedef struct taguchi_experiment_run taguchi_experiment_run_t;
typedef struct taguchi_result_set taguchi_result_set_t;
typedef struct taguchi_main_effect taguchi_main_effect_t;

/*
 * ============================================================================
 * Experiment Definition API
 * ============================================================================
 */

/**
 * Create experiment definition from .tgu file content (string).
 * 
 * @param content  .tgu file content as string
 * @param error_buf Buffer for error message (size TAGUCHI_ERROR_SIZE)
 * @return Experiment definition handle, or NULL on error
 */
taguchi_experiment_def_t *taguchi_parse_definition(
    const char *content,
    char *error_buf
);

/**
 * Create experiment definition programmatically.
 * 
 * @param array_type Array type (e.g., "L4", "L9", "L16")
 * @return Experiment definition handle, or NULL on error
 */
taguchi_experiment_def_t *taguchi_create_definition(const char *array_type);

/**
 * Add factor to definition.
 * 
 * @param def Experiment definition
 * @param name Factor name
 * @param levels Array of level values (strings)
 * @param level_count Number of levels
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_add_factor(
    taguchi_experiment_def_t *def,
    const char *name,
    const char **levels,
    size_t level_count,
    char *error_buf
);

/**
 * Validate experiment definition.
 * 
 * @param def Experiment definition
 * @param error_buf Buffer for error message
 * @return true if valid, false otherwise
 */
bool taguchi_validate_definition(
    const taguchi_experiment_def_t *def,
    char *error_buf
);

/**
 * Free experiment definition.
 * 
 * @param def Experiment definition to free
 */
void taguchi_free_definition(taguchi_experiment_def_t *def);

/*
 * ============================================================================
 * Array Information API
 * ============================================================================
 */

/**
 * Get available array names.
 * 
 * @return NULL-terminated array of array names (do not free)
 */
const char **taguchi_list_arrays(void);

/**
 * Suggest optimal array for experiment definition.
 *
 * Automatically selects the most appropriate orthogonal array based on
 * factor count and level requirements.
 *
 * @param def Experiment definition
 * @param error_buf Buffer for error message (size TAGUCHI_ERROR_SIZE)
 * @return Recommended array name (do not free), or NULL on error
 */
const char *taguchi_suggest_optimal_array(
    const taguchi_experiment_def_t *def,
    char *error_buf
);

/**
 * Get array information.
 *
 * @param name Array name (e.g., "L9")
 * @param rows_out Output: number of runs
 * @param cols_out Output: number of factors
 * @param levels_out Output: number of levels
 * @return 0 on success, -1 if array not found
 */
int taguchi_get_array_info(
    const char *name,
    size_t *rows_out,
    size_t *cols_out,
    size_t *levels_out
);

/**
 * Get number of factors in experiment definition.
 *
 * @param def Experiment definition
 * @return Number of factors in the definition
 */
size_t taguchi_def_get_factor_count(const taguchi_experiment_def_t *def);

/**
 * Get factor name by index from experiment definition.
 *
 * @param def Experiment definition
 * @param index Factor index (0-based)
 * @return Factor name (do not free), or NULL if index out of range
 */
const char *taguchi_def_get_factor_name(const taguchi_experiment_def_t *def, size_t index);

/*
 * ============================================================================
 * Generation API
 * ============================================================================
 */

/**
 * Generate experiment runs from definition.
 * 
 * @param def Experiment definition
 * @param runs_out Output: array of run pointers (caller must free)
 * @param count_out Output: number of runs
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_generate_runs(
    const taguchi_experiment_def_t *def,
    taguchi_experiment_run_t ***runs_out,
    size_t *count_out,
    char *error_buf
);

/**
 * Get run configuration value by factor name.
 *
 * @param run Experiment run
 * @param factor_name Factor name
 * @return Factor value, or NULL if not found (do not free)
 */
const char *taguchi_run_get_value(
    const taguchi_experiment_run_t *run,
    const char *factor_name
);

/**
 * Get factor name by index in run.
 *
 * @param run Experiment run
 * @param index Factor index (0-based)
 * @return Factor name, or NULL if index out of range (do not free)
 */
const char *taguchi_run_get_factor_name_at_index(
    const taguchi_experiment_run_t *run,
    size_t index
);

/**
 * Get number of factors in run.
 *
 * @param run Experiment run
 * @return Number of factors in the run
 */
size_t taguchi_run_get_factor_count(
    const taguchi_experiment_run_t *run
);

/**
 * Get run ID.
 * 
 * @param run Experiment run
 * @return Run ID (1-indexed)
 */
size_t taguchi_run_get_id(const taguchi_experiment_run_t *run);

/**
 * Get all factor names in run.
 * 
 * @param run Experiment run
 * @return NULL-terminated array of factor names (do not free)
 */
const char **taguchi_run_get_factor_names(const taguchi_experiment_run_t *run);

/**
 * Free experiment runs.
 * 
 * @param runs Array of run pointers
 * @param count Number of runs
 */
void taguchi_free_runs(taguchi_experiment_run_t **runs, size_t count);

/*
 * ============================================================================
 * Results API
 * ============================================================================
 */

/**
 * Create result set for collecting experimental data.
 * 
 * @param def Experiment definition
 * @param metric_name Name of metric being measured
 * @return Result set handle, or NULL on error
 */
taguchi_result_set_t *taguchi_create_result_set(
    const taguchi_experiment_def_t *def,
    const char *metric_name
);

/**
 * Add result to set.
 * 
 * @param results Result set
 * @param run_id Run ID (from taguchi_run_get_id)
 * @param response_value Measured response value
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_add_result(
    taguchi_result_set_t *results,
    size_t run_id,
    double response_value,
    char *error_buf
);

/**
 * Free result set.
 * 
 * @param results Result set to free
 */
void taguchi_free_result_set(taguchi_result_set_t *results);

/*
 * ============================================================================
 * Analysis API
 * ============================================================================
 */

/**
 * Calculate main effects from results.
 * 
 * @param results Result set
 * @param effects_out Output: array of effect pointers (caller must free)
 * @param count_out Output: number of effects
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_calculate_main_effects(
    const taguchi_result_set_t *results,
    taguchi_main_effect_t ***effects_out,
    size_t *count_out,
    char *error_buf
);

/**
 * Get effect factor name.
 * 
 * @param effect Main effect
 * @return Factor name (do not free)
 */
const char *taguchi_effect_get_factor(const taguchi_main_effect_t *effect);

/**
 * Get level means from effect.
 * 
 * @param effect Main effect
 * @param level_count_out Output: number of levels
 * @return Array of level means (do not free)
 */
const double *taguchi_effect_get_level_means(
    const taguchi_main_effect_t *effect,
    size_t *level_count_out
);

/**
 * Get effect range (max - min).
 * 
 * @param effect Main effect
 * @return Range value
 */
double taguchi_effect_get_range(const taguchi_main_effect_t *effect);

/**
 * Free main effects.
 * 
 * @param effects Array of effect pointers
 * @param count Number of effects
 */
void taguchi_free_effects(taguchi_main_effect_t **effects, size_t count);

/**
 * Recommend optimal configuration based on effects.
 * 
 * @param effects Array of effect pointers
 * @param effect_count Number of effects
 * @param higher_is_better True if maximizing metric
 * @param recommendation_buf Output buffer for recommendation
 * @param buf_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int taguchi_recommend_optimal(
    const taguchi_main_effect_t **effects,
    size_t effect_count,
    bool higher_is_better,
    char *recommendation_buf,
    size_t buf_size
);

/*
 * ============================================================================
 * Serialization API (for language bindings)
 * ============================================================================
 */

/**
 * Serialize runs to JSON string.
 * 
 * @param runs Array of run pointers
 * @param count Number of runs
 * @return JSON string (caller must free with taguchi_free_string)
 */
char *taguchi_runs_to_json(
    const taguchi_experiment_run_t **runs,
    size_t count
);

/**
 * Serialize effects to JSON string.
 * 
 * @param effects Array of effect pointers
 * @param count Number of effects
 * @return JSON string (caller must free with taguchi_free_string)
 */
char *taguchi_effects_to_json(
    const taguchi_main_effect_t **effects,
    size_t count
);

/**
 * Free string allocated by library.
 * 
 * @param str String to free
 */
void taguchi_free_string(char *str);

#ifdef __cplusplus
}
#endif

#endif /* TAGUCHI_H */
