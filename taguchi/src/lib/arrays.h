#ifndef ARRAYS_H
#define ARRAYS_H

#include <stddef.h>
#include <stdbool.h>  // for bool type
#include "../../src/config.h"  // Include config for constants
#include "parser.h"   // for ExperimentDef structure

/* Orthogonal array structure (internal) */
typedef struct {
    const char *name;
    size_t rows;           /* Number of experiments */
    size_t cols;           /* Number of factors */
    size_t levels;         /* Levels per factor (0 = mixed) */
    const int *data;       /* Row-major array */
    const int *col_levels; /* Per-column level count; NULL = use `levels` for all */
} OrthogonalArray;

/* Lookup array by name */
const OrthogonalArray *get_array(const char *name);

/* List all available arrays (for public API) */
const char **list_array_names(void);

/* Suggest most appropriate orthogonal array based on factor requirements */
const char *suggest_optimal_array(const ExperimentDef *def, char *error_buf);

/* List all array structures (for internal use) */
const OrthogonalArray *get_all_arrays(size_t *count_out);

/* Calculate how many OA columns a factor needs (column pairing) */
size_t columns_needed_for_factor(size_t level_count, size_t base_levels);

/* Calculate total OA columns needed for all factors */
size_t total_columns_needed(const ExperimentDef *def, size_t base_levels);

/* Check if a mixed-level array can accommodate the given factors */
bool mixed_array_can_fit(const OrthogonalArray *array, const ExperimentDef *def);

#endif /* ARRAYS_H */
