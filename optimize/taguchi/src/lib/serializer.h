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

/* NOTE: there is deliberately no serialize_effects_to_json here.
 *
 * One existed and returned a bracketed C-comment placeholder rather than JSON
 * -- not parseable by anything -- from a function named for producing JSON,
 * with a test asserting that output. Nothing called it; taguchi_effects_to_json
 * in taguchi.c is the real implementation. Removed rather than left as a trap
 * for whoever reaches for the obvious name.
 */

/* Free serialized string */
void free_serialized_string(char *str);

#endif /* SERIALIZER_H */
