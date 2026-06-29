#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <stddef.h>
#include "generator.h"  // For ExperimentRun
#include "../config.h"  // For constants

/* Serialize runs to JSON format */
char *serialize_runs_to_json(const ExperimentRun *runs, size_t count);

/* Serialize effects to JSON format (forward declaration - will be needed for analyzer) */
struct MainEffect;  // Forward declaration
char *serialize_effects_to_json(const struct MainEffect *effects, size_t count);

/* Free serialized string */
void free_serialized_string(char *str);

#endif /* SERIALIZER_H */