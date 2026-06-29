#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include "include/taguchi.h"

/* Forward declaration — defined below after parse_csv_results */
static char *read_file_dynamic(const char *filename);

static void print_usage(const char *program_name) {
    fprintf(stderr, 
        "Usage: %s [OPTIONS] <command> [ARGS]\n"
        "\n"
        "Commands:\n"
        "  generate <file.tgu>     Generate experiment runs\n"
        "  run <file.tgu> <script> Execute experiments with external script\n"
        "  analyze <file.tgu> <results.csv> Analyze experimental results\n"
        "  effects <file.tgu> <results.csv> Calculate main effects\n"
        "  validate <file.tgu>     Validate experiment definition\n"
        "  suggest-array <file.tgu> Suggest optimal orthogonal array\n"
        "  list-arrays             List available orthogonal arrays\n"
        "  --help                  Show this help message\n"
        "  --version               Show version information\n"
        "\n"
        "Examples:\n"
        "  %s generate experiment.tgu\n"
        "  %s run experiment.tgu './my_script.sh'\n"
        "  %s analyze experiment.tgu results.csv --metric throughput\n",
        program_name, program_name, program_name, program_name);
}

static void print_version(void) {
    printf("Taguchi Array Tool v%d.%d.%d\n", 
           TAGUCHI_VERSION_MAJOR, TAGUCHI_VERSION_MINOR, TAGUCHI_VERSION_PATCH);
}

static int cmd_help(int argc, char *argv[]) {
    (void)argc; // Suppress unused parameter warning
    print_usage(argv[0]);
    return 0;
}

static int cmd_version(int argc, char *argv[]) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    print_version();
    return 0;
}

static int cmd_list_arrays(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    const char **arrays = taguchi_list_arrays();
    printf("Available orthogonal arrays:\n");
    for (int i = 0; arrays[i] != NULL; i++) {
        size_t rows, cols, levels;
        if (taguchi_get_array_info(arrays[i], &rows, &cols, &levels) == 0) {
            if (levels == 0) {
                printf("  %-5s (%3zu runs, %3zu cols, mixed)\n",
                       arrays[i], rows, cols);
            } else {
                printf("  %-5s (%3zu runs, %3zu cols, %zu levels)\n",
                       arrays[i], rows, cols, levels);
            }
        } else {
            printf("  %s\n", arrays[i]);
        }
    }
    return 0;
}

static int cmd_generate(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: generate command requires .tgu file\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    
    // Read the file
    char *content = read_file_dynamic(filename);
    if (!content) return 1;

    // Parse the definition
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error parsing file %s: %s\n", filename, error);
        return 1;
    }

    // Generate runs
    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;
    
    if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
        fprintf(stderr, "Error generating runs: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }
    
    // Print runs with factor details
    printf("Generated %zu experiment runs:\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("Run %zu: ", taguchi_run_get_id(runs[i]));

        // Get factor count from the original definition to know how many to print
        size_t factor_count = taguchi_def_get_factor_count(def);

        // Print each factor-value pair
        for (size_t f = 0; f < factor_count; f++) {
            const char *factor_name = taguchi_def_get_factor_name(def, f);
            if (factor_name) {
                const char *factor_value = taguchi_run_get_value(runs[i], factor_name);
                if (factor_value) {
                    if (f > 0) printf(", "); // Comma separator except for first
                    printf("%s=%s", factor_name, factor_value);
                }
            }
        }
        printf("\n");
    }
    
    // Cleanup
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
    
    return 0;
}

static int cmd_suggest_array(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: suggest-array command requires .tgu file\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    char *content = read_file_dynamic(filename);
    if (!content) return 1;

    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error: Invalid .tgu file %s: %s\n", filename, error);
        return 1;
    }

    const char *array_name = taguchi_suggest_optimal_array(def, error);
    if (!array_name) {
        fprintf(stderr, "Error: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }

    printf("%s\n", array_name);
    taguchi_free_definition(def);
    return 0;
}

static int cmd_validate(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: validate command requires .tgu file\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    
    // Read the file
    char *content = read_file_dynamic(filename);
    if (!content) return 1;

    // Parse the definition
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error: Invalid .tgu file %s: %s\n", filename, error);
        return 1;
    }
    
    // Validate the definition
    if (!taguchi_validate_definition(def, error)) {
        fprintf(stderr, "Validation failed: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }
    
    printf("Valid .tgu file: %s\n", filename);
    taguchi_free_definition(def);
    
    return 0;
}

// Command to run experiments with external script
static int cmd_run(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: run command requires .tgu file and script\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *tgu_file = argv[1];
    const char *script = argv[2];
    
    // Read the .tgu file
    char *content = read_file_dynamic(tgu_file);
    if (!content) return 1;

    // Parse the definition
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error parsing .tgu file %s: %s\n", tgu_file, error);
        return 1;
    }
    
    // Generate runs
    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;
    
    if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
        fprintf(stderr, "Error generating runs: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }
    
    // Execute each run as a separate process
    printf("Executing %zu experiment runs using '%s'...\n", count, script);
    
    for (size_t i = 0; i < count; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process: set environment variables and run the script

            // Set run ID as environment variable
            char run_id_str[64];
            snprintf(run_id_str, sizeof(run_id_str), "%zu", taguchi_run_get_id(runs[i]));
            setenv("TAGUCHI_RUN_ID", run_id_str, 1);

            // Set environment variables for each factor-value pair
            size_t factor_count = taguchi_run_get_factor_count(runs[i]);
            for (size_t f = 0; f < factor_count; f++) {
                const char *factor_name = taguchi_run_get_factor_name_at_index(runs[i], f);
                const char *factor_value = taguchi_run_get_value(runs[i], factor_name);

                if (factor_name && factor_value) {
                    /* Reject factor names containing '=' — would corrupt the env block */
                    if (strchr(factor_name, '=') != NULL) {
                        fprintf(stderr, "Error: factor name '%s' contains invalid character '='\n", factor_name);
                        exit(1);
                    }
                    char env_name[256];
                    int nw = snprintf(env_name, sizeof(env_name), "TAGUCHI_%s", factor_name);
                    if (nw < 0 || nw >= (int)sizeof(env_name)) {
                        fprintf(stderr, "Error: factor name too long for environment variable\n");
                        exit(1);
                    }
                    setenv(env_name, factor_value, 1);
                }
            }

            // Execute the script
            execl("/bin/sh", "sh", "-c", script, (char *)NULL);
            
            // If execl returns, it failed
            perror("exec failed");
            exit(1);
        } else if (pid > 0) {
            // Parent process: wait for child process
            int status;
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                printf("Run %zu completed with exit code %d\n", taguchi_run_get_id(runs[i]), exit_code);
            } else {
                printf("Run %zu terminated abnormally\n", taguchi_run_get_id(runs[i]));
            }
        } else {
            // Fork failed
            perror("fork failed");
            taguchi_free_runs(runs, count);
            taguchi_free_definition(def);
            return 1;
        }
    }
    
    // Cleanup
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
    
    printf("All experiment runs completed.\n");
    return 0;
}

/* Helper: read a file into a dynamically allocated buffer. Caller must free(). */
static char *read_file_dynamic(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        perror("Error seeking in file");
        fclose(file);
        return NULL;
    }
    long sz = ftell(file);
    if (sz < 0) {
        perror("Error getting file size");
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(file);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, file);
    fclose(file);
    buf[n] = '\0';
    return buf;
}

/* Split a CSV line in-place into field pointers. Returns field count.
 * Replaces commas with NUL bytes; max_fields caps the result. */
static int csv_split(char *line, char **fields, int max_fields) {
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = '\0';
    }
    return n;
}

/* Trim leading/trailing ASCII spaces and tabs in-place.
 * Returns the new start pointer (may differ from s). */
static char *csv_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';
    return s;
}

/*
 * Parse a CSV results file.
 *
 * The file may have any number of columns.  If the first non-comment,
 * non-empty row is a header (its first field is not a plain integer), the
 * column whose name matches metric_name is used as the response value.
 * When no header is present the second column (index 1) is used.
 *
 * Lines starting with '#' are treated as comments.
 */
static int parse_csv_results(const char *filename, const char *metric_name,
                              taguchi_result_set_t *results, char *error_buf) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        snprintf(error_buf, TAGUCHI_ERROR_SIZE, "Cannot open results file: %s", filename);
        return -1;
    }

    char line[4096];
    int line_num = 0;
    int data_lines = 0;
    int metric_col = -1;   /* column index for the response value; -1 = not yet resolved */
    bool header_seen = false;

    while (fgets(line, sizeof(line), file)) {
        line_num++;
        /* Detect truncated lines — buffer full with no newline means line exceeded limit */
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            snprintf(error_buf, TAGUCHI_ERROR_SIZE,
                     "Line %d exceeds maximum length (%zu chars)", line_num, sizeof(line) - 2);
            fclose(file);
            return -1;
        }
        /* Trim trailing newline/CR */
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#') continue;

        if (!header_seen) {
            header_seen = true;

            /* Decide if this row is a header by checking whether its first
             * comma-delimited field parses as a plain integer. */
            char tmp[4096];
            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            char *hfields[512];
            int nhf = csv_split(tmp, hfields, 512);

            char *endptr;
            strtol(csv_trim(hfields[0]), &endptr, 10);
            bool is_header = (*endptr != '\0'); /* non-numeric first field → header */

            if (is_header) {
                /* Locate the column whose name matches metric_name */
                for (int col = 0; col < nhf; col++) {
                    if (strcmp(csv_trim(hfields[col]), metric_name) == 0) {
                        metric_col = col;
                        break;
                    }
                }

                if (metric_col == -1) {
                    if (strcmp(metric_name, "response") == 0) {
                        /* Default metric not present in header — fall back to col 1 */
                        metric_col = 1;
                    } else {
                        snprintf(error_buf, TAGUCHI_ERROR_SIZE,
                                 "Metric '%s' not found in CSV header", metric_name);
                        fclose(file);
                        return -1;
                    }
                }
                continue; /* header consumed; proceed to data rows */
            } else {
                /* No header row */
                if (strcmp(metric_name, "response") != 0) {
                    snprintf(error_buf, TAGUCHI_ERROR_SIZE,
                             "No header row in '%s'; cannot locate metric '%s'",
                             filename, metric_name);
                    fclose(file);
                    return -1;
                }
                metric_col = 1;
                /* Fall through — parse this line as the first data row */
            }
        }

        /* --- Data row --- */
        char row[4096];
        strncpy(row, line, sizeof(row) - 1);
        row[sizeof(row) - 1] = '\0';

        char *fields[512];
        int nf = csv_split(row, fields, 512);

        if (nf <= metric_col) {
            snprintf(error_buf, TAGUCHI_ERROR_SIZE,
                     "Row at line %d has %d column(s); metric '%s' is at column %d",
                     line_num, nf, metric_name, metric_col + 1);
            fclose(file);
            return -1;
        }

        char *endptr;
        long run_id = strtol(csv_trim(fields[0]), &endptr, 10);
        if (*endptr != '\0' || run_id < 1) {
            snprintf(error_buf, TAGUCHI_ERROR_SIZE, "Invalid run_id at line %d", line_num);
            fclose(file);
            return -1;
        }

        char *val_str = csv_trim(fields[metric_col]);
        /* Skip rows where the metric value is absent (missing data) */
        if (val_str[0] == '\0') continue;

        double response = strtod(val_str, &endptr);
        if (*endptr != '\0') {
            snprintf(error_buf, TAGUCHI_ERROR_SIZE,
                     "Invalid value for metric '%s' at line %d: '%s'",
                     metric_name, line_num, val_str);
            fclose(file);
            return -1;
        }

        if (taguchi_add_result(results, (size_t)run_id, response, error_buf) != 0) {
            fclose(file);
            return -1;
        }
        data_lines++;
    }

    fclose(file);

    if (data_lines == 0) {
        snprintf(error_buf, TAGUCHI_ERROR_SIZE, "No data rows found in %s", filename);
        return -1;
    }

    return 0;
}

static int cmd_effects(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: effects command requires .tgu file and results CSV\n");
        fprintf(stderr, "Usage: effects <file.tgu> <results.csv> [--metric name]\n");
        return 1;
    }

    const char *tgu_file = argv[1];
    const char *csv_file = argv[2];
    const char *metric_name = "response";

    /* Parse optional --metric flag */
    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "--metric") == 0) {
            metric_name = argv[i + 1];
            break;
        }
    }

    char *content = read_file_dynamic(tgu_file);
    if (!content) return 1;

    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error parsing %s: %s\n", tgu_file, error);
        return 1;
    }

    taguchi_result_set_t *results = taguchi_create_result_set(def, metric_name);
    if (!results) {
        fprintf(stderr, "Error creating result set\n");
        taguchi_free_definition(def);
        return 1;
    }

    if (parse_csv_results(csv_file, metric_name, results, error) != 0) {
        fprintf(stderr, "Error reading results: %s\n", error);
        taguchi_free_result_set(results);
        taguchi_free_definition(def);
        return 1;
    }

    taguchi_main_effect_t **effects = NULL;
    size_t effect_count = 0;
    if (taguchi_calculate_main_effects(results, &effects, &effect_count, error) != 0) {
        fprintf(stderr, "Error calculating effects: %s\n", error);
        taguchi_free_result_set(results);
        taguchi_free_definition(def);
        return 1;
    }

    /* Print main effects table */
    printf("Main Effects for metric: %s\n", metric_name);
    printf("%-20s %8s   Level Means\n", "Factor", "Range");
    printf("%-20s %8s   -----------\n", "------", "-----");

    for (size_t i = 0; i < effect_count; i++) {
        const char *name = taguchi_effect_get_factor(effects[i]);
        double range = taguchi_effect_get_range(effects[i]);
        size_t level_count = 0;
        const double *means = taguchi_effect_get_level_means(effects[i], &level_count);

        printf("%-20s %8.3f   ", name, range);
        for (size_t lv = 0; lv < level_count; lv++) {
            if (lv > 0) printf(", ");
            printf("L%zu=%.3f", lv + 1, means[lv]);
        }
        printf("\n");
    }

    taguchi_free_effects(effects, effect_count);
    taguchi_free_result_set(results);
    taguchi_free_definition(def);
    return 0;
}

static int cmd_analyze(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: analyze command requires .tgu file and results CSV\n");
        fprintf(stderr, "Usage: analyze <file.tgu> <results.csv> [--metric name] [--minimize]\n");
        return 1;
    }

    const char *tgu_file = argv[1];
    const char *csv_file = argv[2];
    const char *metric_name = "response";
    bool higher_is_better = true;

    /* Parse optional flags */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) {
            metric_name = argv[++i];
        } else if (strcmp(argv[i], "--minimize") == 0) {
            higher_is_better = false;
        }
    }

    char *content = read_file_dynamic(tgu_file);
    if (!content) return 1;

    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) {
        fprintf(stderr, "Error parsing %s: %s\n", tgu_file, error);
        return 1;
    }

    taguchi_result_set_t *results = taguchi_create_result_set(def, metric_name);
    if (!results) {
        fprintf(stderr, "Error creating result set\n");
        taguchi_free_definition(def);
        return 1;
    }

    if (parse_csv_results(csv_file, metric_name, results, error) != 0) {
        fprintf(stderr, "Error reading results: %s\n", error);
        taguchi_free_result_set(results);
        taguchi_free_definition(def);
        return 1;
    }

    taguchi_main_effect_t **effects = NULL;
    size_t effect_count = 0;
    if (taguchi_calculate_main_effects(results, &effects, &effect_count, error) != 0) {
        fprintf(stderr, "Error calculating effects: %s\n", error);
        taguchi_free_result_set(results);
        taguchi_free_definition(def);
        return 1;
    }

    /* Print effects summary */
    printf("Analysis for metric: %s (%s)\n\n",
           metric_name, higher_is_better ? "maximizing" : "minimizing");

    printf("Main Effects:\n");
    printf("%-20s %8s   Level Means\n", "Factor", "Range");
    printf("%-20s %8s   -----------\n", "------", "-----");

    for (size_t i = 0; i < effect_count; i++) {
        const char *name = taguchi_effect_get_factor(effects[i]);
        double range = taguchi_effect_get_range(effects[i]);
        size_t level_count = 0;
        const double *means = taguchi_effect_get_level_means(effects[i], &level_count);

        printf("%-20s %8.3f   ", name, range);
        for (size_t lv = 0; lv < level_count; lv++) {
            if (lv > 0) printf(", ");
            printf("L%zu=%.3f", lv + 1, means[lv]);
        }
        printf("\n");
    }

    /* Print recommendation */
    char recommendation[1024];
    if (taguchi_recommend_optimal((const taguchi_main_effect_t **)effects, effect_count,
                                   higher_is_better, recommendation, sizeof(recommendation)) == 0) {
        printf("\nOptimal Configuration: %s\n", recommendation);
    }

    taguchi_free_effects(effects, effect_count);
    taguchi_free_result_set(results);
    taguchi_free_definition(def);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *command = argv[1];
    
    // Shift arguments for command handlers
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1;
    
    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0) {
        print_version();
        return 0;
    } else if (strcmp(command, "help") == 0) {
        return cmd_help(sub_argc, sub_argv);
    } else if (strcmp(command, "version") == 0) {
        return cmd_version(sub_argc, sub_argv);
    } else if (strcmp(command, "list-arrays") == 0) {
        return cmd_list_arrays(sub_argc, sub_argv);
    } else if (strcmp(command, "generate") == 0) {
        return cmd_generate(sub_argc, sub_argv);
    } else if (strcmp(command, "validate") == 0) {
        return cmd_validate(sub_argc, sub_argv);
    } else if (strcmp(command, "suggest-array") == 0) {
        return cmd_suggest_array(sub_argc, sub_argv);
    } else if (strcmp(command, "run") == 0) {
        return cmd_run(sub_argc, sub_argv);
    } else if (strcmp(command, "analyze") == 0) {
        return cmd_analyze(sub_argc, sub_argv);
    } else if (strcmp(command, "effects") == 0) {
        return cmd_effects(sub_argc, sub_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}