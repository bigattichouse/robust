#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include "../../src/config.h"  // Include config for constants

/* Internal structures for experiment definition (implementation details) */
typedef struct {
    char name[MAX_FACTOR_NAME];
    char values[MAX_LEVELS][MAX_LEVEL_VALUE];
    size_t level_count;
} Factor;

typedef struct {
    Factor factors[MAX_FACTORS];
    size_t factor_count;
    char array_type[8];  /* "L4", "L9", "L16", "L27", etc. */
} ExperimentDef;

/* Parse experiment definition from string content */
int parse_experiment_def_from_string(
    const char *content,
    ExperimentDef *def,
    char *error_buf
);

/* Validate parsed experiment definition */
bool validate_experiment_def(
    const ExperimentDef *def,
    char *error_buf
);

/* Free resources used by experiment definition */
void free_experiment_def(ExperimentDef *def);

#endif /* PARSER_H */