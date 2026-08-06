#include "generator.h"
#include "utils.h"
#include "arrays.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function to find the best array for the given factors */
static const OrthogonalArray *get_suggested_array_for_factors(const ExperimentDef *def, char *error_buf) {
    size_t array_count;
    const OrthogonalArray *all_arrays = get_all_arrays(&array_count);

    /* Determine the dominant level count in the factors */
    size_t dominant_levels = 2;
    size_t max_levels = 0;
    for (size_t i = 0; i < def->factor_count; i++) {
        if (def->factors[i].level_count > max_levels) {
            max_levels = def->factors[i].level_count;
        }
    }
    /* Prefer arrays with base level >= max factor levels */
    dominant_levels = max_levels;

    /* Find all arrays that can accommodate the factors.
     * Priority: 1) Exact level match with any margin, 2) Good margin (50-200%),
     * but avoid excessively large arrays (> 4x minimum runs) unless necessary. */
    const OrthogonalArray *best_fit = NULL;
    const OrthogonalArray *smallest_fit = NULL;
    const OrthogonalArray *best_exact_match = NULL;  /* Best exact level match */
    size_t smallest_fit_rows = 0;
    size_t best_margin_pct = 0;

    for (size_t i = 0; i < array_count; i++) {
        const OrthogonalArray *array = &all_arrays[i];
        size_t needed = total_columns_needed(def, array->levels);
        
        if (needed > array->cols) {
            continue;
        }

        /* Track smallest fit as fallback */
        if (smallest_fit == NULL || array->rows < smallest_fit_rows) {
            smallest_fit = array;
            smallest_fit_rows = array->rows;
        }

        /* Check if array base level matches factor levels (exact match) */
        int is_exact_match = (array->levels == dominant_levels) ? 1 : 0;

        /* Calculate capacity margin percentage */
        size_t margin_pct = (array->cols >= needed) ? ((array->cols - needed) * 100 / needed) : 0;

        /* Track best exact level match: prefer larger ones with good margin (50-200%) */
        if (is_exact_match) {
            int margin_good = (margin_pct >= 50 && margin_pct <= 200) ? 1 : 0;
            int current_margin_good = 0;
            if (best_exact_match != NULL) {
                size_t current_needed = total_columns_needed(def, best_exact_match->levels);
                size_t current_margin = (best_exact_match->cols >= current_needed) ? 
                    ((best_exact_match->cols - current_needed) * 100 / current_needed) : 0;
                current_margin_good = (current_margin >= 50 && current_margin <= 200) ? 1 : 0;
            }
            
            if (best_exact_match == NULL) {
                best_exact_match = array;
            } else if (margin_good && !current_margin_good) {
                best_exact_match = array;  /* New has good margin, old doesn't */
            } else if (margin_good && current_margin_good && array->rows > best_exact_match->rows) {
                best_exact_match = array;  /* Both have good margin, prefer larger */
            } else if (!margin_good && !current_margin_good && array->rows < best_exact_match->rows) {
                best_exact_match = array;  /* Neither has good margin, prefer smaller */
            }
        }

        /* Skip if this array is more than 4x the smallest (too expensive) */
        if (smallest_fit_rows > 0 && array->rows > smallest_fit_rows * 4) {
            continue;
        }

        /* Track best margin fit */
        if (margin_pct >= 50 && margin_pct <= 200) {
            if (best_fit == NULL || margin_pct > best_margin_pct) {
                best_fit = array;
                best_margin_pct = margin_pct;
            }
        }
    }

    /* Prefer exact level match if available, otherwise use best margin fit,
     * or fall back to smallest fit */
    if (best_exact_match != NULL) {
        return best_exact_match;
    }
    if (best_fit != NULL) {
        return best_fit;
    }
    if (smallest_fit != NULL) {
        return smallest_fit;
    }

    if (error_buf) {
        set_error(error_buf, "No suitable array found for %zu factors",
                 def->factor_count);
    }
    return NULL;
}

/* Check if factors fit in specified array (with column pairing support) */
bool check_array_compatibility(const ExperimentDef *def, const OrthogonalArray *array, char *error_buf) {
    if (!def || !array) {
        if (error_buf) {
            strcpy(error_buf, "Invalid parameters to check_array_compatibility");
        }
        return false;
    }

    /* Mixed-level array: each factor must match a column by exact level count */
    if (array->col_levels != NULL) {
        if (!mixed_array_can_fit(array, def)) {
            if (error_buf) {
                set_error(error_buf, "Array %s cannot accommodate the given factors. "
                         "It supports columns: col0=2-level, cols1-7=3-level. "
                         "Each factor must have a matching-level column available.",
                         array->name);
            }
            return false;
        }
        return true;
    }

    /* Homogeneous array: check total columns needed (with column pairing) */
    size_t needed = total_columns_needed(def, array->levels);
    if (needed > array->cols) {
        if (error_buf) {
            set_error(error_buf, "Array %s has %zu columns, but %zu columns needed "
                     "(factors require column pairing for multi-level support)",
                     array->name, array->cols, needed);
        }
        return false;
    }

    return true;
}

/* Generate experiments from definition (with column pairing and mixed-level support) */
int generate_experiments(const ExperimentDef *def, ExperimentRun **runs_out, size_t *count_out, char *error_buf) {
    if (!def || !runs_out || !count_out) {
        if (error_buf) {
            strcpy(error_buf, "Invalid parameters to generate_experiments");
        }
        return -1;
    }

    /* Find the corresponding array */
    const OrthogonalArray *array = NULL;

    if (strlen(def->array_type) == 0) {
        array = get_suggested_array_for_factors(def, error_buf);
        if (!array) {
            if (error_buf) {
                set_error(error_buf, "No suitable array found for %zu factors. "
                         "Either specify an array or reduce factor/level count.", def->factor_count);
            }
            return -1;
        }
    } else {
        array = get_array(def->array_type);
        if (!array) {
            if (error_buf) {
                set_error(error_buf, "Unknown array type: %s", def->array_type);
            }
            return -1;
        }
    }

    /* Check compatibility */
    if (!check_array_compatibility(def, array, error_buf)) {
        return -1;
    }

    /*
     * Build column assignment map: for each factor, record which OA columns
     * it uses and how many.
     */
    size_t col_start[MAX_FACTORS];  /* first OA column for each factor */
    size_t col_count[MAX_FACTORS];  /* number of OA columns for each factor */

    if (array->col_levels != NULL) {
        /*
         * Mixed-level array (e.g. L18): assign each factor to the first
         * available column whose level count exactly matches the factor's
         * level count.  Each factor always occupies exactly 1 column.
         */
        bool col_used[64];
        memset(col_used, 0, sizeof(col_used));

        for (size_t i = 0; i < def->factor_count; i++) {
            size_t needed = def->factors[i].level_count;
            col_count[i] = 1;
            col_start[i] = array->cols; /* sentinel */

            for (size_t c = 0; c < array->cols && c < 64; c++) {
                if (!col_used[c] && (size_t)array->col_levels[c] == needed) {
                    col_start[i] = c;
                    col_used[c] = true;
                    break;
                }
            }
            /* check_array_compatibility already verified every factor can be placed */
        }
    } else {
        /* Homogeneous array: sequential assignment with column pairing */
        size_t next_col = 0;
        for (size_t i = 0; i < def->factor_count; i++) {
            col_count[i] = columns_needed_for_factor(def->factors[i].level_count, array->levels);
            col_start[i] = next_col;
            next_col += col_count[i];
        }
    }

    /* Allocate runs array */
    ExperimentRun *runs = xcalloc(array->rows, sizeof(ExperimentRun));

    /* Generate each run */
    for (size_t run_idx = 0; run_idx < array->rows; run_idx++) {
        ExperimentRun *run = &runs[run_idx];
        run->run_id = run_idx + 1;
        run->factor_count = def->factor_count;

        /* Copy factor names */
        for (size_t factor_idx = 0; factor_idx < def->factor_count; factor_idx++) {
            strcpy(run->factor_names[factor_idx], def->factors[factor_idx].name);
        }

        /* Map array values to factor levels (with column pairing) */
        for (size_t factor_idx = 0; factor_idx < def->factor_count; factor_idx++) {
            const Factor *factor = &def->factors[factor_idx];
            int level_index = 0;

            if (col_count[factor_idx] == 1) {
                /* Single column: direct mapping */
                level_index = array->data[run_idx * array->cols + col_start[factor_idx]];
            } else {
                /* Multiple columns (column pairing): combine values */
                /* level_index = col_a * base^(n-1) + col_b * base^(n-2) + ... */
                size_t base = array->levels;
                level_index = 0;
                for (size_t c = 0; c < col_count[factor_idx]; c++) {
                    int col_val = array->data[run_idx * array->cols + col_start[factor_idx] + c];
                    /* Multiply by base^(remaining positions) */
                    size_t multiplier = 1;
                    for (size_t p = c + 1; p < col_count[factor_idx]; p++) {
                        multiplier *= base;
                    }
                    level_index += col_val * (int)multiplier;
                }
            }

            /*
             * Mixed-level support: if the computed index exceeds the factor's
             * level count, wrap using modulo. This handles cases like:
             * - 2-level factor in 3-level array (index 0,1,2 -> level 0,1,0)
             * - 5-level factor using 2 paired columns (index 0-8 -> level 0-4,0-3)
             */
            if (level_index < 0) level_index = 0;
            level_index = level_index % (int)factor->level_count;

            run->level_indices[factor_idx] = (size_t)level_index;
            strcpy(run->values[factor_idx], factor->values[level_index]);
        }
    }

    *runs_out = runs;
    *count_out = array->rows;
    return 0;
}

/* Free generated runs */
void free_experiments(ExperimentRun *runs, size_t count) {
    if (runs) {
        (void)count;
        free(runs);
    }
}
