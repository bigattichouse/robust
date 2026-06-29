#include "serializer.h"
#include "utils.h"
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function to escape JSON strings */
char *escape_json_string(const char *input) {
    if (!input) return NULL;
    
    size_t len = strlen(input);
    // In worst case, we might need to escape every character
    char *escaped = xmalloc(len * 2 + 1); // Give extra space for escapes
    size_t pos = 0;
    
    for (size_t i = 0; i < len; i++) {
        switch (input[i]) {
            case '"':
                escaped[pos++] = '\\';
                escaped[pos++] = '"';
                break;
            case '\\':
                escaped[pos++] = '\\';
                escaped[pos++] = '\\';
                break;
            case '\b':
                escaped[pos++] = '\\';
                escaped[pos++] = 'b';
                break;
            case '\f':
                escaped[pos++] = '\\';
                escaped[pos++] = 'f';
                break;
            case '\n':
                escaped[pos++] = '\\';
                escaped[pos++] = 'n';
                break;
            case '\r':
                escaped[pos++] = '\\';
                escaped[pos++] = 'r';
                break;
            case '\t':
                escaped[pos++] = '\\';
                escaped[pos++] = 't';
                break;
            default:
                escaped[pos++] = input[i];
                break;
        }
    }
    escaped[pos] = '\0';
    return escaped;
}

/* Serialize runs to JSON format */
char *serialize_runs_to_json(const ExperimentRun *runs, size_t count) {
    if (!runs || count == 0) {
        char *result = xmalloc(3); // Just "[]"
        strcpy(result, "[]");
        return result;
    }
    
    // Rough estimate of buffer size needed - we'll realloc as necessary
    size_t estimated_size = count * BUFFER_SIZE; // Start with estimated size
    char *json = xmalloc(estimated_size);
    size_t pos = 0;
    
    // Start array
    pos += snprintf(json + pos, estimated_size - pos, "[\n");
    
    for (size_t i = 0; i < count; i++) {
        const ExperimentRun *run = &runs[i];
        
        // Start object
        pos += snprintf(json + pos, estimated_size - pos, "  {\"run_id\": %zu", run->run_id);
        
        // Add all factor values
        for (size_t j = 0; j < run->factor_count; j++) {
            // Escape the factor name and value
            char *escaped_name = escape_json_string(run->factor_names[j]);
            char *escaped_value = escape_json_string(run->values[j]);
            
            size_t needed = strlen(escaped_name) + strlen(escaped_value) + 10; // Extra for formatting
            if (pos + needed >= estimated_size) {
                estimated_size *= 2;
                json = xrealloc(json, estimated_size);
            }
            
            pos += snprintf(json + pos, estimated_size - pos, ", \"%s\": \"%s\"", 
                          escaped_name, escaped_value);
            
            free(escaped_name);
            free(escaped_value);
        }
        
        // Close object
        if (i == count - 1) {
            pos += snprintf(json + pos, estimated_size - pos, "}\n");
        } else {
            pos += snprintf(json + pos, estimated_size - pos, "},\n");
        }
        
        // Resize if needed
        if (pos > estimated_size * 3 / 4) { // Resize if we're 75% full
            estimated_size *= 2;
            json = xrealloc(json, estimated_size);
        }
    }
    
    pos += snprintf(json + pos, estimated_size - pos, "]");
    
    // Resize to exact size needed
    json = xrealloc(json, pos + 1);
    return json;
}

/* Serialize effects to JSON format (stub implementation - will need MainEffect structure) */
struct MainEffect;  // Forward declaration

char *serialize_effects_to_json(const struct MainEffect *effects, size_t count) {
    // For now, implement a simple placeholder - this will be expanded when the analyzer module is implemented
    (void)effects; // Suppress unused parameter warning
    if (count == 0) {
        char *result = xmalloc(3);
        strcpy(result, "[]");
        return result;
    }

    char *json = xmalloc(100); // Small initial allocation
    snprintf(json, 100, "[/* %zu effects */]", count);
    return json;
}

/* Free serialized string */
void free_serialized_string(char *str) {
    if (str) {
        free(str);
    }
}