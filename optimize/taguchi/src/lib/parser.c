#include "parser.h"
#include "utils.h"
#include "include/taguchi.h"   /* TAGUCHI_ERROR_SIZE, as utils.c does */
#include "arrays.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Prefix an already-formatted parse error with the line it came from.
 *
 * Used where the message is built by a helper that never learns which line it
 * was handed (parse_factor_line). Rewriting the finished string keeps the line
 * number in one place instead of threading it through every helper, which is
 * how one of them ends up reporting the wrong line.
 *
 * The copy back is bounded by the formatted length, which snprintf has already
 * clamped below TAGUCHI_ERROR_SIZE.
 */
static void err_prefix_line(char *error_buf, int line_num) {
    if (!error_buf) return;
    char tmp[TAGUCHI_ERROR_SIZE];
    if (snprintf(tmp, sizeof tmp, "line %d: %s", line_num, error_buf) < 0) return;
    memcpy(error_buf, tmp, strlen(tmp) + 1);
}

/* Helper function to trim whitespace */
char *trim_whitespace(char *str) {
    if (!str) return NULL;
    
    char *end;
    
    // Trim leading space
    while (*str && isspace(*str)) {
        str++;
    }
    
    if (*str == 0) {  // All spaces?
        return str;
    }
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end--;
    }
    
    *(end + 1) = '\0';
    
    return str;
}

/* Helper function to split string by delimiter */
int split_string(const char *str, char delim, char **tokens, size_t max_tokens) {
    if (!str || !tokens || max_tokens == 0) {
        return 0;
    }

    size_t count = 0;
    const char *start = str;
    const char *ptr = str;

    while (*ptr && count < max_tokens) {
        if (*ptr == delim) {
            size_t len = ptr - start;
            if (len > 0) {
                tokens[count] = xmalloc(len + 1);
                strncpy(tokens[count], start, len);
                tokens[count][len] = '\0';
                count++;
            }
            start = ptr + 1;
        }
        ptr++;
    }

    // Handle the last token
    if (start < ptr && count < max_tokens) {
        size_t len = ptr - start;
        tokens[count] = xmalloc(len + 1);
        strncpy(tokens[count], start, len);
        tokens[count][len] = '\0';
        count++;
    }

    return (int)count;
}

/* Parse a line containing factor levels (e.g., "cache_size: 64M, 128M, 256M") */
int parse_factor_line(const char *line, Factor *factor, char *error_buf) {
    if (!line || !factor) {
        return -1;
    }
    
    // Find the colon
    const char *colon_pos = strchr(line, ':');
    if (!colon_pos) {
        set_error(error_buf, "Expected ':' after factor name");
        return -1;
    }
    
    // Extract factor name
    size_t name_len = colon_pos - line;
    if (name_len >= MAX_FACTOR_NAME) {
        set_error(error_buf, "Factor name too long (max %d)", MAX_FACTOR_NAME - 1);
        return -1;
    }
    
    strncpy(factor->name, line, name_len);
    factor->name[name_len] = '\0';
    trim_whitespace(factor->name);
    
    // Check if name is empty after trimming
    if (strlen(factor->name) == 0) {
        set_error(error_buf, "Empty factor name");
        return -1;
    }

    /*
     * Reject control characters, as core/space.c's .space parser already does.
     *
     * They were accepted here, and a factor name reaches two places that
     * cannot represent them: the JSON emitters (where a raw control byte is a
     * character JSON forbids) and setenv("TAGUCHI_<name>") in the run path.
     * Bytes >= 0x80 stay legal so UTF-8 names keep working.
     */
    for (const unsigned char *p = (const unsigned char *)factor->name; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) {
            set_error(error_buf, "Factor name contains a control character");
            return -1;
        }
    }
    
    // Skip colon and any leading whitespace
    const char *values_start = colon_pos + 1;
    while (*values_start && isspace(*values_start)) {
        values_start++;
    }
    
    // Parse values using comma separator
    char *values_copy = xmalloc(strlen(values_start) + 1);
    strcpy(values_copy, values_start);
    
    char *tokens[MAX_LEVELS];
    int num_tokens = split_string(values_copy, ',', tokens, MAX_LEVELS);
    
    if (num_tokens <= 0) {
        set_error(error_buf, "No factor levels found after ':'");
        free(values_copy);
        return -1;
    }
    
    if (num_tokens > MAX_LEVELS) {
        set_error(error_buf, "Too many levels for factor '%s' (max %d)", factor->name, MAX_LEVELS);
        for (int i = 0; i < num_tokens; i++) {
            free(tokens[i]);
        }
        free(values_copy);
        return -1;
    }
    
    // Store the levels with trimming
    factor->level_count = 0;
    for (int i = 0; i < num_tokens; i++) {
        char *trimmed = trim_whitespace(tokens[i]);
        if (strlen(trimmed) == 0) {
            // Skip empty levels
            free(tokens[i]);
            continue;
        }
        
        if (strlen(trimmed) >= MAX_LEVEL_VALUE) {
            set_error(error_buf, "Level value '%s' too long (max %d)", trimmed, MAX_LEVEL_VALUE - 1);
            for (int j = i; j < num_tokens; j++) {
                free(tokens[j]);
            }
            free(values_copy);
            return -1;
        }
        
        strcpy(factor->values[factor->level_count], trimmed);
        factor->level_count++;
        free(tokens[i]);
    }
    
    free(values_copy);
    
    if (factor->level_count == 0) {
        set_error(error_buf, "No valid levels found for factor '%s'", factor->name);
        return -1;
    }
    
    return 0;
}

/* Parse experiment definition from string content */
int parse_experiment_def_from_string(const char *content, ExperimentDef *def, char *error_buf) {
    if (!content || !def) {
        if (error_buf) {
            strcpy(error_buf, "Invalid input parameters");
        }
        return -1;
    }

    // Initialize the structure
    memset(def, 0, sizeof(ExperimentDef));

    char *content_copy = xmalloc(strlen(content) + 1);
    strcpy(content_copy, content);

    /*
     * Walked with strchr rather than strtok, so that line numbers in errors are
     * the file's own.
     *
     * strtok collapses runs of its delimiter, so blank lines never become
     * tokens and a counter driven by it silently under-reports: given
     * "array: L4\n\n\nfactors:\n  a: 1, 2\n" it calls the factor line 3 when it
     * is line 5. A line number that is wrong is worse than none, which is why
     * the counter that used to sit here reported nothing -- it could not have
     * been right. This is the same scan core/src/space.c uses.
     */
    char *line = content_copy;
    int line_num = 0;
    /* 0 = outside any section, 1 = factors:, 2 = noise: */
    int in_factors_section = 0;

    while (line != NULL && *line != '\0') {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *next = nl ? nl + 1 : NULL;
        line_num++;                 /* incremented before anything can fail */

        // Check original line for leading whitespace before trimming
        char first_char_original = line[0];

        // Trim leading/trailing whitespace
        char *trimmed_line = trim_whitespace(line);

        // Skip empty lines and comments
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            line = next;
            continue;
        }

        // Check for factors section indicator
        if (strcmp(trimmed_line, "factors:") == 0) {
            in_factors_section = 1;
        }
        /* Noise factors (E5): the outer array. Same line grammar as factors. */
        else if (strcmp(trimmed_line, "noise:") == 0) {
            in_factors_section = 2;
        }
        // Check for array specification
        else if (strncmp(trimmed_line, "array:", 6) == 0) {
            in_factors_section = 0;  // No longer in a factor section

            // Parse the array type
            const char *array_start = trimmed_line + 6;
            while (*array_start && isspace(*array_start)) {
                array_start++;
            }

            if (strlen(array_start) >= sizeof(def->array_type)) {
                set_error(error_buf, "line %d: array type too long", line_num);
                free(content_copy);
                return -1;
            }

            strncpy(def->array_type, array_start, sizeof(def->array_type) - 1);
            def->array_type[sizeof(def->array_type) - 1] = '\0';
            trim_whitespace(def->array_type);
        }
        // If we're in the factors section and the original line started with space (indentation)
        else if (in_factors_section != 0) {
            // The original line (before trimming) should start with whitespace (indentation)
            // and after trimming it should contain a colon
            if ((first_char_original == ' ' || first_char_original == '\t') && strchr(trimmed_line, ':')) {
                // This is an indented factor line like "  cache_size: 64M, 128M, 256M"
                int is_noise = (in_factors_section == 2);
                size_t *count = is_noise ? &def->noise_count : &def->factor_count;
                Factor *table = is_noise ? def->noise : def->factors;

                if (*count >= MAX_FACTORS) {
                    set_error(error_buf, "line %d: too many %s (max %d)",
                              line_num, is_noise ? "noise factors" : "factors",
                              MAX_FACTORS);
                    free(content_copy);
                    return -1;
                }

                Factor *current_factor = &table[*count];
                if (parse_factor_line(trimmed_line, current_factor, error_buf) != 0) {  // Use trimmed line
                    /* parse_factor_line has no idea which line it was handed, so
                     * the location is attached here -- the one place that knows. */
                    err_prefix_line(error_buf, line_num);
                    free(content_copy);
                    return -1;
                }

                (*count)++;
            }
            /*
             * Anything else inside `factors:` used to be dropped in silence,
             * which is the worst outcome available: a mistyped factor line
             * removed a factor from the design and the tool still succeeded,
             * so the array was built and run over the wrong space with nothing
             * to indicate it. A missing colon is the likely typo, and it is
             * exactly what the old code could not tell you about.
             */
            else if (first_char_original == ' ' || first_char_original == '\t') {
                set_error(error_buf,
                          "line %d: factor line needs 'name: value1, value2, ...' "
                          "(no ':' found in '%s')", line_num, trimmed_line);
                free(content_copy);
                return -1;
            }
            else {
                set_error(error_buf,
                          "line %d: '%s' is not indented, so it is not a factor; "
                          "factor lines are indented under 'factors:'",
                          line_num, trimmed_line);
                free(content_copy);
                return -1;
            }
        }
        /*
         * And outside it. An unrecognised key here is how `arry: L9` silently
         * became "no array specified" and auto-selection quietly picked a
         * different array than the one asked for.
         */
        else {
            set_error(error_buf,
                      "line %d: unknown key in '%s' (expected 'factors:', 'noise:' or 'array:')",
                      line_num, trimmed_line);
            free(content_copy);
            return -1;
        }

        line = next;
    }

    free(content_copy);

    // Validate we have required information
    if (def->factor_count == 0) {
        set_error(error_buf, "No factors defined in experiment");
        return -1;
    }

    // Array type is now optional for auto-selection
    // If specified, validate its format
    if (strlen(def->array_type) > 0) {
        // Validate array type format (should be like L4, L9, L16, L27)
        if (def->array_type[0] != 'L' || strlen(def->array_type) < 2) {
            set_error(error_buf, "Invalid array type format: %s (should be like L4, L9, etc.)", def->array_type);
            return -1;
        }

        // Check that array type follows expected pattern
        for (size_t i = 1; i < strlen(def->array_type); i++) {
            if (!isdigit(def->array_type[i])) {
                set_error(error_buf, "Invalid array type format: %s", def->array_type);
                return -1;
            }
        }
    }
    // If array_type is empty, that's okay for auto-selection

    return 0;
}

/* Validate parsed experiment definition */
bool validate_experiment_def(const ExperimentDef *def, char *error_buf) {
    if (!def) {
        if (error_buf) {
            strcpy(error_buf, "Invalid experiment definition pointer");
        }
        return false;
    }
    
    // Check factor count bounds
    if (def->factor_count == 0 || def->factor_count > MAX_FACTORS) {
        if (error_buf) {
            set_error(error_buf, "Invalid factor count: %zu (must be between 1 and %d)", 
                     def->factor_count, MAX_FACTORS);
        }
        return false;
    }
    
    // Check that all factors have names and levels
    for (size_t i = 0; i < def->factor_count; i++) {
        const Factor *factor = &def->factors[i];
        
        if (strlen(factor->name) == 0) {
            if (error_buf) {
                set_error(error_buf, "Factor at position %zu has no name", i + 1);
            }
            return false;
        }
        
        if (factor->level_count == 0 || factor->level_count > MAX_LEVELS) {
            if (error_buf) {
                set_error(error_buf, "Factor '%s' has invalid number of levels: %zu (must be between 1 and %d)", 
                         factor->name, factor->level_count, MAX_LEVELS);
            }
            return false;
        }
    }
    
    // Array type is now optional for auto-selection
    // If no array is specified, taguchi_suggest_optimal_array can be used
    return true;
}

/* Free resources used by experiment definition */
void free_experiment_def(ExperimentDef *def) {
    if (def) {
        // Note: Our implementation uses fixed-size arrays, so no dynamic allocation to free
        // The structure will be part of a larger object that will be freed separately
        memset(def, 0, sizeof(ExperimentDef));
    }
}
