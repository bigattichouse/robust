#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <stddef.h>
#include "generator.h"  // For ExperimentRun
#include "../config.h"  // For constants

/* Escape a string for embedding in a JSON document. Caller frees.
 * Declared here so the CLI's --json emitters use this one implementation
 * rather than growing a second, subtly different one. */
char *escape_json_string(const char *input);

/* Serialize runs to JSON format */
char *serialize_runs_to_json(const ExperimentRun *runs, size_t count);

/* Serialize effects to JSON format (forward declaration - will be needed for analyzer) */
struct MainEffect;  // Forward declaration
char *serialize_effects_to_json(const struct MainEffect *effects, size_t count);

/* Free serialized string */
void free_serialized_string(char *str);

#endif /* SERIALIZER_H */
