#include "taguchi.h"
#include "parser.h"
#include "arrays.h"
#include "generator.h"
#include "serializer.h"
#include "analyzer.h"
#include "utils.h"
#include "../config.h"  // Include config for constants
#include <stdlib.h>     // For malloc, free
#include <stdio.h>      // For snprintf
#include <string.h>

/*
 * ============================================================================
 * Internal structure definitions (for opaque handles)
 * ============================================================================
 */

// Define internal structures that match the opaque handles declared in the public header
struct taguchi_experiment_def {
    ExperimentDef internal_def;
};

struct taguchi_experiment_run {
    ExperimentRun internal_run;
};

struct taguchi_result_set {
    ResultSet internal_results;
};

struct taguchi_main_effect {
    MainEffect internal_effect;
};

/*
 * ============================================================================
 * Experiment Definition API Implementation
 * ============================================================================
 */

taguchi_experiment_def_t *taguchi_parse_definition(const char *content, char *error_buf) {
    taguchi_experiment_def_t *def = xmalloc(sizeof(taguchi_experiment_def_t));
    
    if (parse_experiment_def_from_string(content, &def->internal_def, error_buf) != 0) {
        free(def);
        return NULL;
    }
    
    return def;
}

taguchi_experiment_def_t *taguchi_create_definition(const char *array_type) {
    taguchi_experiment_def_t *def = xmalloc(sizeof(taguchi_experiment_def_t));
    memset(&def->internal_def, 0, sizeof(def->internal_def));
    
    if (strlen(array_type) >= sizeof(def->internal_def.array_type)) {
        return NULL;
    }
    
    strcpy(def->internal_def.array_type, array_type);
    return def;
}

int taguchi_add_factor(taguchi_experiment_def_t *def, const char *name, const char **levels, size_t level_count, char *error_buf) {
    if (!def || !name || !levels || level_count == 0 || level_count > MAX_LEVELS) {
        set_error(error_buf, "Invalid parameters to taguchi_add_factor");
        return -1;
    }

    if (def->internal_def.factor_count >= MAX_FACTORS) {
        set_error(error_buf, "Maximum number of factors (%d) exceeded", MAX_FACTORS);
        return -1;
    }

    Factor *new_factor = &def->internal_def.factors[def->internal_def.factor_count];
    memset(new_factor, 0, sizeof(Factor));

    if (strlen(name) >= MAX_FACTOR_NAME) {
        set_error(error_buf, "Factor name too long: %s", name);
        return -1;
    }
    strcpy(new_factor->name, name);
    new_factor->level_count = level_count;

    for (size_t i = 0; i < level_count && i < MAX_LEVELS; i++) {
        if (strlen(levels[i]) >= MAX_LEVEL_VALUE) {
            set_error(error_buf, "Level value too long: %s", levels[i]);
            return -1;
        }
        strcpy(new_factor->values[i], levels[i]);
    }

    def->internal_def.factor_count++;
    return 0;
}

bool taguchi_validate_definition(const taguchi_experiment_def_t *def, char *error_buf) {
    if (!def) return false;
    return validate_experiment_def(&def->internal_def, error_buf);
}

size_t taguchi_def_get_factor_count(const taguchi_experiment_def_t *def) {
    if (!def) return 0;
    return def->internal_def.factor_count;
}

const char *taguchi_def_get_factor_name(const taguchi_experiment_def_t *def, size_t index) {
    if (!def) return NULL;
    if (index >= def->internal_def.factor_count) return NULL;

    return def->internal_def.factors[index].name;
}

void taguchi_free_definition(taguchi_experiment_def_t *def) {
    if (def) {
        free_experiment_def(&def->internal_def);
        free(def);
    }
}

/*
 * ============================================================================
 * Array Information API Implementation
 * ============================================================================
 */

const char **taguchi_list_arrays(void) {
    return list_array_names();
}

const char *taguchi_suggest_optimal_array(const taguchi_experiment_def_t *def, char *error_buf) {
    if (!def) {
        if (error_buf) {
            strcpy(error_buf, "Invalid definition parameter to taguchi_suggest_optimal_array");
        }
        return NULL;
    }

    // Call internal function
    return suggest_optimal_array(&def->internal_def, error_buf);
}

int taguchi_get_array_info(const char *name, size_t *rows_out, size_t *cols_out, size_t *levels_out) {
    if (!name || !rows_out || !cols_out || !levels_out) {
        return -1;
    }
    
    const OrthogonalArray *array = get_array(name);
    if (!array) {
        return -1;
    }
    
    *rows_out = array->rows;
    *cols_out = array->cols;
    *levels_out = array->levels;
    return 0;
}

/*
 * ============================================================================
 * Generation API Implementation
 * ============================================================================
 */

int taguchi_generate_runs(const taguchi_experiment_def_t *def, taguchi_experiment_run_t ***runs_out, size_t *count_out, char *error_buf) {
    if (!def || !runs_out || !count_out) {
        set_error(error_buf, "Invalid parameters to taguchi_generate_runs");
        return -1;
    }
    
    ExperimentRun *internal_runs = NULL;
    size_t internal_count = 0;
    
    int result = generate_experiments(&def->internal_def, &internal_runs, &internal_count, error_buf);
    if (result != 0) {
        return -1;
    }
    
    // Convert internal runs to opaque handles
    taguchi_experiment_run_t **external_runs = xmalloc(internal_count * sizeof(taguchi_experiment_run_t*));
    for (size_t i = 0; i < internal_count; i++) {
        external_runs[i] = xmalloc(sizeof(taguchi_experiment_run_t));
        memcpy(&external_runs[i]->internal_run, &internal_runs[i], sizeof(ExperimentRun));
    }
    
    *runs_out = external_runs;
    *count_out = internal_count;
    
    // Free the temporary internal structure array (but not the data itself)
    free(internal_runs);
    
    return 0;
}

const char *taguchi_run_get_value(const taguchi_experiment_run_t *run, const char *factor_name) {
    if (!run || !factor_name) return NULL;

    // Search for the factor by name and return its value
    for (size_t i = 0; i < run->internal_run.factor_count; i++) {
        if (strcmp(run->internal_run.factor_names[i], factor_name) == 0) {
            return run->internal_run.values[i];
        }
    }
    return NULL;
}

size_t taguchi_run_get_factor_count(const taguchi_experiment_run_t *run) {
    if (!run) return 0;
    return run->internal_run.factor_count;
}

const char *taguchi_run_get_factor_name_at_index(const taguchi_experiment_run_t *run, size_t index) {
    if (!run) return NULL;

    if (index >= run->internal_run.factor_count) {
        return NULL;
    }

    return run->internal_run.factor_names[index];
}

size_t taguchi_run_get_id(const taguchi_experiment_run_t *run) {
    if (!run) return 0;
    return run->internal_run.run_id;
}

const char **taguchi_run_get_factor_names(const taguchi_experiment_run_t *run) {
    if (!run) return NULL;

    // The internal structure uses char factor_names[MAX_FACTORS][MAX_FACTOR_NAME]
    // When we return &factor_names[0], it's type is char(*)[MAX_FACTOR_NAME]
    // which is different from char**, but we can cast it.
    // The correct approach is to return the address of the first element
    // of the 2D array, which allows accessing as array[i] for i in [0,factor_count)
    // We return the pointer to the first factor name, which can be accessed as an array
    return (const char **)&run->internal_run.factor_names[0];
}

void taguchi_free_runs(taguchi_experiment_run_t **runs, size_t count) {
    if (runs) {
        for (size_t i = 0; i < count; i++) {
            if (runs[i]) {
                free(runs[i]);
            }
        }
        free(runs);
    }
}

/*
 * ============================================================================
 * Results API Implementation
 * ============================================================================
 */

taguchi_result_set_t *taguchi_create_result_set(const taguchi_experiment_def_t *def, const char *metric_name) {
    if (!def || !metric_name) return NULL;

    if (strlen(metric_name) >= MAX_FACTOR_NAME) return NULL;

    taguchi_result_set_t *results = xmalloc(sizeof(taguchi_result_set_t));
    memset(&results->internal_results, 0, sizeof(ResultSet));

    results->internal_results.experiment_def = (ExperimentDef *)&def->internal_def;
    results->internal_results.capacity = 16;
    results->internal_results.responses = xmalloc(results->internal_results.capacity * sizeof(double));
    results->internal_results.run_ids = xmalloc(results->internal_results.capacity * sizeof(size_t));
    results->internal_results.count = 0;
    strcpy(results->internal_results.metric_name, metric_name);

    return results;
}

int taguchi_add_result(taguchi_result_set_t *results, size_t run_id, double response_value, char *error_buf) {
    if (!results) {
        if (error_buf) set_error(error_buf, "Invalid results pointer");
        return -1;
    }

    int result = add_result(&results->internal_results, run_id, response_value);
    return result;
}

void taguchi_free_result_set(taguchi_result_set_t *results) {
    if (results) {
        // Use analyzer module's free function
        // Since we created the internal structure specially above, we need to handle memory properly
        if (results->internal_results.responses) {
            free(results->internal_results.responses);
        }
        if (results->internal_results.run_ids) {
            free(results->internal_results.run_ids);
        }
        // Don't free experiment_def as it's just a pointer to the original
        results->internal_results.responses = NULL;
        results->internal_results.run_ids = NULL;
        results->internal_results.experiment_def = NULL;
        free(results);
    }
}

/*
 * ============================================================================
 * Analysis API Implementation
 * ============================================================================
 */

int taguchi_calculate_main_effects(const taguchi_result_set_t *results, taguchi_main_effect_t ***effects_out, size_t *count_out, char *error_buf) {
    if (!results || !effects_out || !count_out) {
        set_error(error_buf, "Invalid parameters to taguchi_calculate_main_effects");
        return -1;
    }

    MainEffect *internal_effects = NULL;
    size_t internal_count = 0;

    int rc = calculate_main_effects(&results->internal_results, &internal_effects, &internal_count);
    if (rc != 0) {
        set_error(error_buf, "Failed to calculate main effects");
        return -1;
    }

    /* Wrap internal effects in opaque handles */
    taguchi_main_effect_t **external_effects = xmalloc(internal_count * sizeof(taguchi_main_effect_t *));
    for (size_t i = 0; i < internal_count; i++) {
        external_effects[i] = xmalloc(sizeof(taguchi_main_effect_t));
        memcpy(&external_effects[i]->internal_effect, &internal_effects[i], sizeof(MainEffect));
        /* Null out level_means in the source so free_main_effects won't double-free */
        internal_effects[i].level_means = NULL;
    }

    /* Free the internal array shell (level_means ownership transferred above) */
    free(internal_effects);

    *effects_out = external_effects;
    *count_out = internal_count;
    return 0;
}

const char *taguchi_effect_get_factor(const taguchi_main_effect_t *effect) {
    if (!effect) return NULL;
    return effect->internal_effect.factor_name;
}

const double *taguchi_effect_get_level_means(const taguchi_main_effect_t *effect, size_t *level_count_out) {
    if (!effect) {
        if (level_count_out) *level_count_out = 0;
        return NULL;
    }
    if (level_count_out) *level_count_out = effect->internal_effect.level_count;
    return effect->internal_effect.level_means;
}

double taguchi_effect_get_range(const taguchi_main_effect_t *effect) {
    if (!effect) return 0.0;
    return effect->internal_effect.range;
}

void taguchi_free_effects(taguchi_main_effect_t **effects, size_t count) {
    if (effects) {
        for (size_t i = 0; i < count; i++) {
            if (effects[i]) {
                free(effects[i]->internal_effect.level_means);
                free(effects[i]);
            }
        }
        free(effects);
    }
}

int taguchi_recommend_optimal(const taguchi_main_effect_t **effects, size_t effect_count, bool higher_is_better, char *recommendation_buf, size_t buf_size) {
    if (!effects || !recommendation_buf || buf_size == 0 || effect_count == 0) {
        return -1;
    }

    /* Build an array of internal MainEffect structs for the analyzer */
    MainEffect *internal_effects = xmalloc(effect_count * sizeof(MainEffect));
    for (size_t i = 0; i < effect_count; i++) {
        memcpy(&internal_effects[i], &effects[i]->internal_effect, sizeof(MainEffect));
    }

    int rc = recommend_optimal_levels(internal_effects, effect_count, higher_is_better,
                                       recommendation_buf, buf_size);
    free(internal_effects);
    return rc;
}

/*
 * ============================================================================
 * Serialization API Implementation
 * ============================================================================
 */

char *taguchi_runs_to_json(const taguchi_experiment_run_t **runs, size_t count) {
    if (!runs || count == 0) {
        char *result = xmalloc(3);
        strcpy(result, "[]");
        return result;
    }

    // Convert taguchi_experiment_run_t array to internal ExperimentRun array for serialization
    // For now, we'll assume the internal structure is compatible
    ExperimentRun *internal_runs = xmalloc(count * sizeof(ExperimentRun));
    for (size_t i = 0; i < count; i++) {
        memcpy(&internal_runs[i], &runs[i]->internal_run, sizeof(ExperimentRun));
    }

    char *json_result = serialize_runs_to_json(internal_runs, count);

    free(internal_runs);
    return json_result;
}

char *taguchi_effects_to_json(const taguchi_main_effect_t **effects, size_t count) {
    if (!effects || count == 0) {
        char *result = xmalloc(3);
        strcpy(result, "[]");
        return result;
    }

    /* Build JSON manually for effects */
    size_t buf_size = 1024 + count * 512;
    char *json = xmalloc(buf_size);
    size_t pos = 0;

    pos += (size_t)snprintf(json + pos, buf_size - pos, "[\n");
    for (size_t i = 0; i < count; i++) {
        const MainEffect *e = &effects[i]->internal_effect;
        pos += (size_t)snprintf(json + pos, buf_size - pos,
            "  {\"factor\": \"%s\", \"range\": %.6f, \"level_means\": [",
            e->factor_name, e->range);
        for (size_t lv = 0; lv < e->level_count; lv++) {
            if (lv > 0) pos += (size_t)snprintf(json + pos, buf_size - pos, ", ");
            pos += (size_t)snprintf(json + pos, buf_size - pos, "%.6f", e->level_means[lv]);
        }
        pos += (size_t)snprintf(json + pos, buf_size - pos, "]}%s\n",
            (i < count - 1) ? "," : "");
    }
    pos += (size_t)snprintf(json + pos, buf_size - pos, "]");

    return json;
}

void taguchi_free_string(char *str) {
    if (str) {
        free_serialized_string(str);
    }
}