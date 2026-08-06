#ifndef GENERATOR_H
#define GENERATOR_H

#include <stddef.h>
#include <stdbool.h>
#include "parser.h"  // For ExperimentDef
#include "arrays.h"  // For OrthogonalArray

/* Internal structure for generated experiment run */
typedef struct {
    size_t run_id;
    char values[MAX_FACTORS][MAX_LEVEL_VALUE];
    size_t level_indices[MAX_FACTORS]; /* post-modulo OA level index for each factor */
    size_t factor_count;
    char factor_names[MAX_FACTORS][MAX_FACTOR_NAME];
} ExperimentRun;

/* Generate experiments from definition */
int generate_experiments(
    const ExperimentDef *def,
    ExperimentRun **runs_out,
    size_t *count_out,
    char *error_buf
);

/* Free generated runs */
void free_experiments(ExperimentRun *runs, size_t count);

/* Check if factors fit in specified array */
bool check_array_compatibility(
    const ExperimentDef *def,
    const OrthogonalArray *array,
    char *error_buf
);

#endif /* GENERATOR_H */