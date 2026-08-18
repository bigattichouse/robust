#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include "include/taguchi.h"
#include "../config.h"       /* MAX_LEVELS, MAX_NOISE_FACTORS */
#include "doe.h"             /* the suite's shared CSV reader and JSON helpers */

/* Forward declaration — defined below, with the other file helpers. */
static char *read_file_dynamic(const char *filename);

/*
 * ============================================================================
 * The machine-readable contract: --json on generate, effects and analyze.
 *
 * These three outputs were prose, and they were being scraped. The Python
 * binding split `Run 1: a=1, b=2` on ", " and "=", and matched the effects
 * table with \s*(\w+)\s+([\d.]+)\s+(.+) -- both skipping any line that did
 * not match, which is the silent-drop behaviour that let a formatting change
 * to `morris analyze` cost hours of GPU time before anyone noticed.
 *
 * Measured, not hypothesised: \w+ does not match a factor named `kv-type`, so
 * that factor was being dropped from the effects table with no error, no
 * warning and no gap in the output. Two of three factors parsed; the analysis
 * silently lost the second-most-important one.
 *
 * More is at stake here than in morris. Morris decides which factors to drop,
 * so a bad parse costs a wider sweep. taguchi produces the DESIGN and the
 * RECOMMENDATION -- a partial parse corrupts the answer rather than the
 * effort.
 *
 * Same rules as morris and sobol: the tables are a display, these keys are the
 * interface, `schema` is bumped on a rename or removal and never on an
 * addition, and diagnostics stay on stderr in both modes.
 * ============================================================================
 */
#define TAGUCHI_JSON_SCHEMA 1

/*
 * Both of these were local copies of what core already provides, which is how
 * the suite ended up with two definitions of "a JSON string" and two of "a
 * JSON number". They forward now: one implementation, one set of edge cases.
 * doe_json_escape allocates because a factor name and a --metric are
 * unbounded and user-chosen.
 */
static void json_str(const char *s) {
    char *e = doe_json_escape(s ? s : "");
    printf("\"%s\"", e ? e : "");
    doe_free(e);
}

/*
 * A JSON number, or `null` when the value is not finite. JSON has no NaN or
 * Infinity literal, so printf's bare `nan` would make one odd cell cost the
 * consumer the whole document.
 *
 * %.10g, not the %.3f the tables print -- a level mean rounded to three
 * decimals is a different number, and this output exists to be computed on.
 * That is core's rule too, which is why this is now core's function.
 */
static void json_num(double v) {
    char buf[DOE_JSON_NUM];
    printf("%s", doe_json_number(v, buf, sizeof buf));
}

static void print_usage(const char *program_name) {
    fprintf(stderr, 
        "Usage: %s [OPTIONS] <command> [ARGS]\n"
        "\n"
        "Commands:\n"
        "  generate <file.tgu> [--csv|--json]  Generate experiment runs\n"
        "  run <file.tgu> <script>          Execute experiments with a script\n"
        "  analyze <file.tgu> <results.csv> [--metric N] [--minimize] [--json]\n"
        "                                   Main effects + optimal configuration\n"
        "  effects <file.tgu> <results.csv> [--metric N] [--json]\n"
        "                                   Main effects only\n"
        "  confirm <file.tgu> <results.csv> [--measured V] [--metric N]\n"
        "                                   [--minimize] [--json]\n"
        "                                   Predict the optimum, and test that\n"
        "                                   prediction against a measured run\n"
        "  validate <file.tgu>              Validate experiment definition\n"
        "  suggest-array <file.tgu>         Suggest optimal orthogonal array\n"
        "  list-arrays [--json]             List available orthogonal arrays\n"
        "  --help                           Show this help message\n"
        "  --version                        Show version information\n"
        "\n"
        "--json gives machine-readable output on generate, analyze, effects and\n"
        "list-arrays.\n"
        "The tables are a display and will keep changing; --json is the contract,\n"
        "versioned by a `schema` key. Parse that, not the tables.\n"
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
    int as_json = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    const char **arrays = taguchi_list_arrays();

    if (as_json) {
        /*
         * Static reference data, but a binding choosing an array reads it, and
         * "(9 runs, 4 cols, 3 levels)" is a sentence to be regex'd. `levels`
         * is null for a mixed-level array rather than 0, because 0 levels and
         * "levels vary by column" are different statements.
         */
        printf("{\n");
        printf("  \"tool\": \"taguchi\",\n");
        printf("  \"command\": \"list-arrays\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"arrays\": [\n");
        for (int i = 0; arrays[i] != NULL; i++) {
            size_t rows, cols, levels;
            int ok = taguchi_get_array_info(arrays[i], &rows, &cols, &levels) == 0;
            printf("    {\"name\": ");
            json_str(arrays[i]);
            if (ok) {
                printf(", \"runs\": %zu, \"columns\": %zu, \"levels\": ", rows, cols);
                if (levels == 0) printf("null, \"mixed_levels\": true}");
                else             printf("%zu, \"mixed_levels\": false}", levels);
            } else {
                printf(", \"runs\": null, \"columns\": null, \"levels\": null, "
                       "\"mixed_levels\": null}");
            }
            printf("%s\n", arrays[i + 1] != NULL ? "," : "");
        }
        printf("  ]\n}\n");
        return 0;
    }

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

#define OUTER_MAX 4096

static size_t outer_size(const taguchi_experiment_def_t *def) {
    size_t n = 1;
    size_t nc = taguchi_def_get_noise_count(def);
    for (size_t i = 0; i < nc; i++) {
        size_t lv = taguchi_def_get_noise_level_count(def, i);
        if (lv == 0) return 0;
        if (n > OUTER_MAX / lv) return 0;      /* refuse to overflow */
        n *= lv;
    }
    return n;
}

/* The j-th point of the full factorial, as a level index per noise factor. */
static void outer_point(const taguchi_experiment_def_t *def, size_t j, size_t *lv_out) {
    size_t nc = taguchi_def_get_noise_count(def);
    for (size_t i = 0; i < nc; i++) {
        size_t lv = taguchi_def_get_noise_level_count(def, i);
        lv_out[i] = j % lv;
        j /= lv;
    }
}

static int cmd_generate(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: generate command requires .tgu file\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    int as_json = 0, as_csv = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else if (strcmp(argv[i], "--csv") == 0) as_csv = 1;
        else {
            /* Rejected, not ignored: a caller asking for a mode this build
             * lacks must not receive the human table and exit 0. */
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

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
    
    size_t factor_count = taguchi_def_get_factor_count(def);
    size_t noise_count = taguchi_def_get_noise_count(def);
    size_t outer = noise_count ? outer_size(def) : 1;
    if (outer == 0) {
        fprintf(stderr, "Error: the outer array would exceed %d points\n", OUTER_MAX);
        taguchi_free_runs(runs, count);
        taguchi_free_definition(def);
        return 1;
    }

    /*
     * With a `noise:` section the design is CROSSED: every inner (control) run
     * is paired with every outer (noise) point, and run ids number the pairs
     * i*outer + j + 1. You have to run all of them -- the spread across the
     * outer array is the measurement, so a missing pair is a missing number,
     * not one fewer replicate.
     */
    if (noise_count > 0 && !as_json) {
        /* The banner goes to stderr so stdout is clean CSV a harness can read. */
        fprintf(stderr, "Crossed design: %zu control runs x %zu noise points = %zu runs\n",
                count, outer, count * outer);
        printf("run_id");
        for (size_t f = 0; f < factor_count; f++)
            printf(",%s", taguchi_def_get_factor_name(def, f));
        for (size_t n = 0; n < noise_count; n++)
            printf(",%s", taguchi_def_get_noise_name(def, n));
        printf("\n");
        size_t lv[MAX_NOISE_FACTORS];
        for (size_t i = 0; i < count; i++) {
            for (size_t j = 0; j < outer; j++) {
                printf("%zu", i * outer + j + 1);
                for (size_t f = 0; f < factor_count; f++) {
                    const char *fn = taguchi_def_get_factor_name(def, f);
                    const char *v = taguchi_run_get_value(runs[i], fn);
                    printf(",%s", v ? v : "");
                }
                outer_point(def, j, lv);
                for (size_t n = 0; n < noise_count; n++)
                    printf(",%s", taguchi_def_get_noise_level(def, n, lv[n]));
                printf("\n");
            }
        }
        taguchi_free_runs(runs, count);
        taguchi_free_definition(def);
        return 0;
    }

    if (as_csv) {
        /*
         * The design as a plain results-CSV skeleton: one row per run, a
         * column per factor. It is what you feed to whatever runs your
         * experiment, and it matches what `morris sample` and `sobol sample`
         * emit -- so one harness works for every tool. Without it the only
         * machine-readable form was --json, which a shell loop cannot read.
         */
        printf("run_id");
        for (size_t f = 0; f < factor_count; f++)
            printf(",%s", taguchi_def_get_factor_name(def, f));
        printf("\n");
        for (size_t i = 0; i < count; i++) {
            printf("%zu", taguchi_run_get_id(runs[i]));
            for (size_t f = 0; f < factor_count; f++) {
                const char *fn = taguchi_def_get_factor_name(def, f);
                const char *v = taguchi_run_get_value(runs[i], fn);
                printf(",%s", v ? v : "");
            }
            printf("\n");
        }
        taguchi_free_runs(runs, count);
        taguchi_free_definition(def);
        return 0;
    }

    if (as_json) {
        /*
         * The design, as data. `Run 1: a=1, b=2` needed splitting on ", " and
         * then on "=", which silently loses any run whose line does not match
         * -- and a design with a missing run is not a design.
         *
         * `factors` is an ordered list, and each run's `values` is in that same
         * order, so a consumer never has to key off a name it might have
         * mis-scraped. `settings` carries the same thing keyed by name, for
         * callers that would rather look factors up.
         */
        printf("{\n");
        printf("  \"tool\": \"taguchi\",\n");
        printf("  \"command\": \"generate\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"run_count\": %zu,\n", count);
        printf("  \"factor_count\": %zu,\n", factor_count);
        printf("  \"factors\": [");
        for (size_t f = 0; f < factor_count; f++) {
            if (f) printf(", ");
            json_str(taguchi_def_get_factor_name(def, f));
        }
        printf("],\n");
        printf("  \"runs\": [\n");
        for (size_t i = 0; i < count; i++) {
            printf("    {\"run_id\": %zu, \"values\": [",
                   taguchi_run_get_id(runs[i]));
            for (size_t f = 0; f < factor_count; f++) {
                if (f) printf(", ");
                json_str(taguchi_run_get_value(runs[i],
                                               taguchi_def_get_factor_name(def, f)));
            }
            printf("], \"settings\": {");
            for (size_t f = 0; f < factor_count; f++) {
                const char *fn = taguchi_def_get_factor_name(def, f);
                if (f) printf(", ");
                json_str(fn);
                printf(": ");
                json_str(taguchi_run_get_value(runs[i], fn));
            }
            printf("}}%s\n", i + 1 < count ? "," : "");
        }
        printf("  ]\n}\n");
    } else {
    // Print runs with factor details
    printf("Generated %zu experiment runs:\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("Run %zu: ", taguchi_run_get_id(runs[i]));

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

/*
 * Map an effect back to its factor in the definition BY NAME.
 *
 * The effects array happens to come back in definition order today, so
 * effects[i] could index the definition directly -- but that couples the
 * level VALUES printed against an effect to an ordering nothing guarantees,
 * and a mismatch would label each level with another factor's value while
 * still producing a perfectly well-formed document. Silently wrong beats
 * loudly broken only for the person who ships it.
 */
static size_t def_index_of(const taguchi_experiment_def_t *def, const char *name) {
    size_t n = taguchi_def_get_factor_count(def);
    for (size_t i = 0; i < n; i++) {
        const char *fn = taguchi_def_get_factor_name(def, i);
        if (fn && name && strcmp(fn, name) == 0) return i;
    }
    return (size_t)-1;
}

/* A level value, or JSON null when the effect names a factor the definition
 * does not have -- which should be impossible, and is therefore worth
 * reporting as null rather than crashing or mislabelling. */
static void json_level_value(const taguchi_experiment_def_t *def,
                             const char *factor_name, size_t level_index) {
    size_t fi = def_index_of(def, factor_name);
    const char *v = (fi == (size_t)-1)
                  ? NULL : taguchi_def_get_factor_level(def, fi, level_index);
    if (v) json_str(v);
    else   printf("null");
}

/*
 * The main-effects table, as data. Shared by `effects` and `analyze` so the
 * two cannot drift.
 *
 * `effects` is in DEFINITION order, which is the order level_means is indexed
 * in and the order the .tgu declares -- not sorted by importance. Sort on
 * `range` if you want the ranking; the order here is the one other fields are
 * keyed to.
 *
 * Each level carries its index, its VALUE, and its mean. The table printed
 * "L1=-13.000, L2=-33.000": the means glued together in one column, and the
 * level values absent entirely, so a reader could not tell what L1 was without
 * going back to the .tgu.
 */
static void print_effects_json(const taguchi_experiment_def_t *def,
                               taguchi_main_effect_t **effects,
                               size_t effect_count) {
    printf("  \"effects\": [\n");
    for (size_t i = 0; i < effect_count; i++) {
        const char *name = taguchi_effect_get_factor(effects[i]);
        size_t level_count = 0;
        const double *means = taguchi_effect_get_level_means(effects[i], &level_count);

        printf("    {\"factor\": ");
        json_str(name);
        printf(", \"range\": ");
        json_num(taguchi_effect_get_range(effects[i]));
        printf(", \"levels\": [");
        for (size_t lv = 0; lv < level_count; lv++) {
            if (lv) printf(", ");
            printf("{\"level\": %zu, \"value\": ", lv + 1);
            json_level_value(def, name, lv);
            printf(", \"mean\": ");
            json_num(means[lv]);
            printf("}");
        }
        printf("]}%s\n", i + 1 < effect_count ? "," : "");
    }
    printf("  ]");
}

/*
 * Read a results file against the design it claims to describe.
 *
 * This used to be two things: a private CSV parser and, bolted on top of it, a
 * coverage check. The parser was the problem. Every other tool reads results
 * through `doe_csv_read_metric`, which refuses a run id the design does not
 * have; taguchi had its own copy that did not, and so `analyze` accepted any
 * file at all. Two failure modes, both silent:
 *
 *   Too many rows -- the extras were skipped, so an L9 read against a 20-row
 *   file produced a complete-looking ranking from rows 1-9.
 *
 *   Too few rows -- a level nothing landed in got a mean of 0.0, printed in
 *   the same column as the real means. "L3=0.000" was a fabricated number in a
 *   table of measurements, at exit 0.
 *
 * Worst is a CROSSED design. With a `noise:` section the run ids number inner
 * x outer PAIRS, so the first nine rows of a 144-row L9 file are nine noise
 * points of control setting 1 -- and the table came out as a main-effects
 * ranking of pure noise, labelled with the control factor names. Nothing here
 * can tell that file from an uncrossed nine-run one by shape alone, and
 * guessing is the one option worth refusing outright: `robust` already reads
 * crossed results correctly, so send the reader there.
 *
 * The private parser is gone. `doe_csv_read_metric_seen` does the reading and
 * hands back a per-run tally, which is exactly the three questions worth
 * asking: a run mentioned twice, a run never mentioned, and (caught before the
 * read, so the message can say what the design expected) a run the design does
 * not have.
 *
 * Returns 0 having filled `results`, or -1 having explained why not.
 */
static int read_results_for_design(const taguchi_experiment_def_t *def,
                                   const char *csv_file, const char *metric,
                                   const char *cmd,
                                   taguchi_result_set_t *results,
                                   size_t *runs_out) {
    size_t outer = taguchi_def_get_noise_count(def) ? outer_size(def) : 1;
    if (outer != 1) {
        fprintf(stderr,
                "Error: this is a crossed design -- its `noise:` section makes every\n"
                "run id an inner x outer PAIR, not a control setting. `%s` builds a\n"
                "main-effects table over control settings, so it would read the first\n"
                "%zu rows of %s as %zu different control runs when they are %zu noise\n"
                "points of the SAME one.\n"
                "\nUse `taguchi robust` instead: it reads the crossed layout and reports\n"
                "the mean, spread and S/N of each control run across the outer array.\n",
                cmd, outer, csv_file, outer, outer);
        return -1;
    }

    char terr[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_run_t **runs = NULL;
    size_t expected = 0;
    if (taguchi_generate_runs(def, &runs, &expected, terr) != 0) {
        fprintf(stderr, "Error generating runs: %s\n", terr);
        return -1;
    }
    taguchi_free_runs(runs, expected);
    if (expected == 0) {
        fprintf(stderr, "Error: the design has no runs\n");
        return -1;
    }

    /*
     * Size the file first. The shared reader would refuse an out-of-range run
     * id on its own, but its message is about a buffer; this one can say what
     * the design actually expected, which is the thing the reader has to fix.
     */
    char err[DOE_ERR_SIZE];
    size_t max_id = 0;
    if (doe_csv_max_run_id(csv_file, &max_id, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return -1;
    }
    if (max_id > expected) {
        fprintf(stderr,
                "Error: %s has a row for run %zu, but the design has %zu run%s.\n"
                "Extra rows were silently ignored before this check existed; a\n"
                "results file and the .tgu that produced it must agree.\n",
                csv_file, max_id, expected, expected == 1 ? "" : "s");
        return -1;
    }

    double   *y    = malloc(expected * sizeof *y);
    unsigned *seen = calloc(expected, sizeof *seen);
    if (!y || !seen) {
        fprintf(stderr, "Error: out of memory\n");
        free(y); free(seen);
        return -1;
    }
    for (size_t i = 0; i < expected; i++) y[i] = NAN;

    size_t got = 0;
    if (doe_csv_read_metric_seen(csv_file, metric, y, expected, seen, &got, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(y); free(seen);
        return -1;
    }

    for (size_t i = 0; i < expected; i++) {
        if (seen[i] > 1) {
            fprintf(stderr,
                    "Error: %s has %u rows for run %zu. Each run appears once; the\n"
                    "analysis has no way to tell a replicate from a duplicated row.\n",
                    csv_file, seen[i], i + 1);
            free(y); free(seen);
            return -1;
        }
    }

    /* Name every hole rather than only the first: someone fixing one row per
     * attempt learns the file is short once per attempt. */
    size_t missing = 0;
    for (size_t i = 0; i < expected; i++) if (seen[i] == 0) missing++;
    if (missing > 0) {
        fprintf(stderr, "Error: %s is missing %zu of the design's %zu runs: ",
                csv_file, missing, expected);
        size_t shown = 0;
        for (size_t i = 0; i < expected && shown < 12; i++) {
            if (seen[i]) continue;
            fprintf(stderr, "%s%zu", shown ? ", " : "", i + 1);
            shown++;
        }
        if (missing > shown) fprintf(stderr, ", ...");
        fprintf(stderr, "\n"
                "A missing run is not one fewer replicate: no response lands in its\n"
                "level, so that level's mean came out as 0.000 and printed alongside\n"
                "real measurements. Measure it, or drop it from the design.\n");
        free(y); free(seen);
        return -1;
    }

    for (size_t i = 0; i < expected; i++) {
        if (taguchi_add_result(results, i + 1, y[i], terr) != 0) {
            fprintf(stderr, "Error: %s\n", terr);
            free(y); free(seen);
            return -1;
        }
    }

    free(y); free(seen);
    if (runs_out) *runs_out = expected;
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
    int as_json = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric_name = argv[++i];
        else if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
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

    size_t design_runs = 0;
    if (read_results_for_design(def, csv_file, metric_name, "effects",
                                results, &design_runs) != 0) {
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

    if (as_json) {
        printf("{\n");
        printf("  \"tool\": \"taguchi\",\n");
        printf("  \"command\": \"effects\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"metric\": ");
        json_str(metric_name);
        /* `runs` so a consumer can check the file covered the design without
         * re-deriving the array size from the table. */
        printf(",\n  \"runs\": %zu,\n", design_runs);
        printf("  \"factor_count\": %zu,\n", effect_count);
        print_effects_json(def, effects, effect_count);
        printf("\n}\n");
    } else {
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
    }

    taguchi_free_effects(effects, effect_count);
    taguchi_free_result_set(results);
    taguchi_free_definition(def);
    return 0;
}

/*
 * confirm — test the additive prediction instead of assuming it.
 *
 * A Taguchi analysis PREDICTS the response at the settings it recommends, by
 * adding each factor's best-level deviation to the grand mean:
 *
 *     y_hat = grand + SUM_i (mean_i[best] - grand)
 *
 * That is a hypothesis: it assumes the factors act additively, which is
 * exactly what an orthogonal array cannot check, because it never runs the
 * combination it recommends. spec/screening-methods.md is blunt about it --
 * "the additive prediction is the hypothesis, not the result; skipping the
 * confirmation run means never testing it."
 *
 * So: print the settings to run and the value to expect, and with --measured,
 * say whether the answer came back where the model said it would.
 *
 * The grand mean is taken as the mean of one factor's level means. In a
 * balanced array every factor gives the same answer, which is the property the
 * array exists for.
 */
static double grand_mean_of(taguchi_main_effect_t **effects, size_t count) {
    if (count == 0) return 0.0;
    size_t n = 0;
    const double *means = taguchi_effect_get_level_means(effects[0], &n);
    if (n == 0) return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += means[i];
    return sum / (double)n;
}

static size_t best_level_of(taguchi_main_effect_t *effect, bool higher_is_better) {
    size_t n = 0;
    const double *means = taguchi_effect_get_level_means(effect, &n);
    size_t best = 0;
    for (size_t lv = 1; lv < n; lv++) {
        int better = higher_is_better ? (means[lv] > means[best])
                                      : (means[lv] < means[best]);
        if (better) best = lv;
    }
    return best;
}

/* ---- E5: robust parameter design, control x noise ---- */

/*
 * The classic Taguchi "robust", and the thing this repo is named for.
 *
 * Control factors go in an INNER array; noise factors -- what you cannot hold
 * still in production but CAN vary deliberately on the bench -- go in an OUTER
 * one. Crossing them scores every control setting by how much the response
 * moves when the noise moves, so the recommendation becomes "the setting least
 * sensitive to what you cannot control" rather than "the best average".
 *
 * The outer array is the FULL FACTORIAL of the noise factors. Noise factors
 * are few and usually two-level, so this is both what practice does and the
 * honest choice: an outer array that aliases noise effects would defeat the
 * purpose of deliberately exercising them.
 *
 * S/N ratios, in dB, per Taguchi:
 *   larger-better   -10 log10( mean(1/y^2) )
 *   smaller-better  -10 log10( mean(y^2)   )
 *   nominal-best     10 log10( mean^2 / var )
 */
typedef enum { SN_LARGER, SN_SMALLER, SN_NOMINAL } sn_kind_t;

static double sn_ratio(const double *y, size_t n, sn_kind_t kind, int *bad) {
    double acc = 0.0, mean = 0.0;
    *bad = 0;
    for (size_t i = 0; i < n; i++) mean += y[i];
    mean /= (double)n;

    if (kind == SN_LARGER) {
        for (size_t i = 0; i < n; i++) {
            if (y[i] == 0.0) { *bad = 1; return 0.0; }
            acc += 1.0 / (y[i] * y[i]);
        }
        acc /= (double)n;
        if (acc <= 0.0) { *bad = 1; return 0.0; }
        return -10.0 * log10(acc);
    }
    if (kind == SN_SMALLER) {
        for (size_t i = 0; i < n; i++) acc += y[i] * y[i];
        acc /= (double)n;
        if (acc <= 0.0) { *bad = 1; return 0.0; }
        return -10.0 * log10(acc);
    }
    /* nominal-best */
    for (size_t i = 0; i < n; i++) acc += (y[i] - mean) * (y[i] - mean);
    acc /= (n > 1) ? (double)(n - 1) : 1.0;
    if (acc <= 0.0 || mean == 0.0) { *bad = 1; return 0.0; }
    return 10.0 * log10((mean * mean) / acc);
}

/*
 * Level means of `score` over the inner runs, for one control factor. This is
 * a main effect computed on whatever score is passed -- S/N or the plain mean
 * -- which is what lets the two recommendations be compared directly.
 */
static void level_scores(const taguchi_experiment_def_t *def,
                         taguchi_experiment_run_t **runs, size_t nruns,
                         const double *score, size_t fi,
                         double *means_out, size_t nlevels) {
    const char *fname = taguchi_def_get_factor_name(def, fi);
    for (size_t lv = 0; lv < nlevels; lv++) {
        const char *want = taguchi_def_get_factor_level(def, fi, lv);
        double sum = 0.0;
        size_t n = 0;
        for (size_t r = 0; r < nruns; r++) {
            const char *got = taguchi_run_get_value(runs[r], fname);
            if (got && want && strcmp(got, want) == 0) { sum += score[r]; n++; }
        }
        means_out[lv] = n ? sum / (double)n : 0.0;
    }
}

static size_t argbest(const double *v, size_t n, int higher_is_better) {
    size_t best = 0;
    for (size_t i = 1; i < n; i++) {
        int better = higher_is_better ? (v[i] > v[best]) : (v[i] < v[best]);
        if (better) best = i;
    }
    return best;
}

static int cmd_robust(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: robust requires .tgu file and results CSV\n");
        fprintf(stderr, "Usage: robust <file.tgu> <results.csv> "
                        "[--sn larger|smaller|nominal] [--metric NAME] [--json]\n");
        return 1;
    }

    const char *tgu_file = argv[1], *csv_file = argv[2];
    const char *metric_name = "response";
    sn_kind_t kind = SN_LARGER;
    int as_json = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric_name = argv[++i];
        else if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else if (strcmp(argv[i], "--sn") == 0 && i + 1 < argc) {
            const char *k = argv[++i];
            if      (strcmp(k, "larger") == 0)  kind = SN_LARGER;
            else if (strcmp(k, "smaller") == 0) kind = SN_SMALLER;
            else if (strcmp(k, "nominal") == 0) kind = SN_NOMINAL;
            else { fprintf(stderr, "--sn must be larger, smaller or nominal\n"); return 1; }
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    char *content = read_file_dynamic(tgu_file);
    if (!content) return 1;
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    free(content);
    if (!def) { fprintf(stderr, "Error parsing %s: %s\n", tgu_file, error); return 1; }

    size_t nnoise = taguchi_def_get_noise_count(def);
    if (nnoise == 0) {
        fprintf(stderr, "Error: %s has no `noise:` section, so there is nothing to be\n"
                        "robust against. Use `analyze` for a single-array experiment.\n",
                tgu_file);
        taguchi_free_definition(def);
        return 1;
    }
    size_t outer = outer_size(def);
    if (outer == 0) {
        fprintf(stderr, "Error: the outer array would exceed %d points\n", OUTER_MAX);
        taguchi_free_definition(def);
        return 1;
    }

    taguchi_experiment_run_t **runs = NULL;
    size_t inner = 0;
    if (taguchi_generate_runs(def, &runs, &inner, error) != 0) {
        fprintf(stderr, "Error generating runs: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }

    /*
     * Responses arrive as inner x outer rows, run_id = i*outer + j + 1.
     *
     * This read the file by hand, because the CLI's old private parser
     * validated run ids against the INNER design, which is the wrong shape
     * here. The shared reader takes the run count as a parameter, so the
     * crossed total is just what it gets told -- and its per-run tally is the
     * same "every pair is required" check that used to be written out longhand
     * below it.
     */
    size_t total = inner * outer;
    double   *y    = malloc(total * sizeof *y);
    unsigned *seen = calloc(total, sizeof *seen);
    if (!y || !seen) {
        fprintf(stderr, "Error: out of memory\n");
        free(y); free(seen);
        taguchi_free_runs(runs, inner);
        taguchi_free_definition(def);
        return 1;
    }
    for (size_t i = 0; i < total; i++) y[i] = NAN;

    {
        char rerr[DOE_ERR_SIZE];
        size_t got = 0;
        if (doe_csv_read_metric_seen(csv_file, metric_name, y, total,
                                     seen, &got, rerr) != 0) {
            fprintf(stderr, "Error: %s\n", rerr);
            free(y); free(seen);
            taguchi_free_runs(runs, inner);
            taguchi_free_definition(def);
            return 1;
        }
    }

    for (size_t i = 0; i < total; i++) {
        if (!seen[i]) {
            fprintf(stderr, "Error: missing response for run %zu. A crossed design needs\n"
                            "every inner x outer combination: %zu inner rows x %zu outer\n"
                            "points = %zu runs.\n", i + 1, inner, outer, total);
            free(y); free(seen);
            taguchi_free_runs(runs, inner);
            taguchi_free_definition(def);
            return 1;
        }
    }

    /* Per inner row: mean, sd and S/N across the outer array. */
    double *mean = calloc(inner, sizeof *mean);
    double *sd   = calloc(inner, sizeof *sd);
    double *sn   = calloc(inner, sizeof *sn);
    if (!mean || !sd || !sn) { fprintf(stderr, "Error: out of memory\n"); return 1; }

    int any_bad = 0;
    for (size_t i = 0; i < inner; i++) {
        const double *row = &y[i * outer];
        double m = 0.0;
        for (size_t j = 0; j < outer; j++) m += row[j];
        m /= (double)outer;
        double v = 0.0;
        for (size_t j = 0; j < outer; j++) v += (row[j] - m) * (row[j] - m);
        v /= (outer > 1) ? (double)(outer - 1) : 1.0;
        mean[i] = m;
        sd[i] = sqrt(v);
        int bad = 0;
        sn[i] = sn_ratio(row, outer, kind, &bad);
        if (bad) any_bad = 1;
    }

    if (any_bad) {
        fprintf(stderr, "Error: the S/N ratio is undefined for at least one inner row\n"
                        "(a zero response for larger/smaller-better, or a constant row\n"
                        "for nominal-best). Choose a different --sn, or check the model.\n");
        free(y); free(seen); free(mean); free(sd); free(sn);
        taguchi_free_runs(runs, inner);
        taguchi_free_definition(def);
        return 1;
    }

    /*
     * The two recommendations, side by side. S/N is always maximised -- that
     * is what the ratio is built to mean -- while the mean-optimal setting
     * follows the objective the S/N kind implies. When they disagree, a
     * control x noise interaction is what put them apart, and the S/N answer
     * is the robust one.
     */
    size_t ncontrol = taguchi_def_get_factor_count(def);
    int mean_higher_better = (kind != SN_SMALLER);

    if (as_json) {
        printf("{\n  \"tool\": \"taguchi\",\n  \"command\": \"robust\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"metric\": ");
        json_str(metric_name);
        printf(",\n  \"sn\": \"%s\",\n",
               kind == SN_LARGER ? "larger" : kind == SN_SMALLER ? "smaller" : "nominal");
        printf("  \"inner_runs\": %zu,\n  \"outer_points\": %zu,\n  \"total_runs\": %zu,\n",
               inner, outer, total);
        printf("  \"rows\": [\n");
        for (size_t i = 0; i < inner; i++) {
            printf("    {\"run_id\": %zu, \"mean\": ", i + 1);
            json_num(mean[i]);
            printf(", \"sd\": ");
            json_num(sd[i]);
            printf(", \"sn_db\": ");
            json_num(sn[i]);
            printf("}%s\n", i + 1 < inner ? "," : "");
        }
        printf("  ],\n  \"factors\": [\n");
    } else {
        printf("Robust analysis of '%s' (%s-the-better)\n", metric_name,
               kind == SN_LARGER ? "larger" : kind == SN_SMALLER ? "smaller" : "nominal");
        printf("%zu inner runs x %zu outer points = %zu runs\n\n", inner, outer, total);
        printf("%-6s %12s %12s %12s\n", "run", "mean", "sd", "S/N (dB)");
        printf("%-6s %12s %12s %12s\n", "---", "----", "--", "--------");
        for (size_t i = 0; i < inner; i++)
            printf("%-6zu %12.4g %12.4g %12.4g\n", i + 1, mean[i], sd[i], sn[i]);
        printf("\n%-20s %-16s %-16s\n", "factor", "robust (S/N)", "mean-optimal");
        printf("%-20s %-16s %-16s\n", "------", "------------", "------------");
    }

    int differ = 0;
    for (size_t fi = 0; fi < ncontrol; fi++) {
        size_t nlv = taguchi_def_get_factor_level_count(def, fi);
        if (nlv == 0 || nlv > MAX_LEVELS) continue;
        double sn_means[MAX_LEVELS], mu_means[MAX_LEVELS];
        level_scores(def, runs, inner, sn, fi, sn_means, nlv);
        level_scores(def, runs, inner, mean, fi, mu_means, nlv);

        size_t best_sn = argbest(sn_means, nlv, 1);
        size_t best_mu = argbest(mu_means, nlv, mean_higher_better);
        if (best_sn != best_mu) differ = 1;

        const char *fname = taguchi_def_get_factor_name(def, fi);
        const char *v_sn = taguchi_def_get_factor_level(def, fi, best_sn);
        const char *v_mu = taguchi_def_get_factor_level(def, fi, best_mu);

        if (as_json) {
            printf("    {\"factor\": ");
            json_str(fname);
            printf(", \"robust_level\": %zu, \"robust_value\": ", best_sn + 1);
            json_str(v_sn ? v_sn : "");
            printf(", \"mean_level\": %zu, \"mean_value\": ", best_mu + 1);
            json_str(v_mu ? v_mu : "");
            printf(", \"differs\": %s}%s\n",
                   best_sn == best_mu ? "false" : "true",
                   fi + 1 < ncontrol ? "," : "");
        } else {
            printf("%-20s %-16s %-16s%s\n", fname, v_sn ? v_sn : "?", v_mu ? v_mu : "?",
                   best_sn == best_mu ? "" : "   <- differ");
        }
    }

    if (as_json) {
        printf("  ],\n  \"recommendations_differ\": %s\n}\n", differ ? "true" : "false");
    } else if (differ) {
        printf("\nThe robust and mean-optimal settings DIFFER. That is a control x noise\n"
               "interaction: the setting with the best average is not the one that holds\n"
               "up when the noise moves. Take the S/N column -- that is the whole reason\n"
               "for running a crossed design.\n");
    } else {
        printf("\nBoth columns agree, so no control factor changes the response's\n"
               "SENSITIVITY to this noise -- only its level. A crossed design was not\n"
               "needed for this answer, which is itself worth knowing.\n");
    }

    free(y); free(seen); free(mean); free(sd); free(sn);
    taguchi_free_runs(runs, inner);
    taguchi_free_definition(def);
    return 0;
}

static int cmd_confirm(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: confirm requires .tgu file and results CSV\n");
        fprintf(stderr, "Usage: confirm <file.tgu> <results.csv> [--measured V] "
                        "[--metric NAME] [--minimize] [--json]\n");
        return 1;
    }

    const char *tgu_file = argv[1];
    const char *csv_file = argv[2];
    const char *metric_name = "response";
    bool higher_is_better = true;
    int as_json = 0, have_measured = 0;
    double measured = 0.0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric_name = argv[++i];
        else if (strcmp(argv[i], "--minimize") == 0) higher_is_better = false;
        else if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else if (strcmp(argv[i], "--measured") == 0 && i + 1 < argc) {
            char *end;
            measured = strtod(argv[++i], &end);
            if (end == argv[i] || *end != '\0' || !isfinite(measured)) {
                fprintf(stderr, "Error: --measured needs a finite number\n");
                return 1;
            }
            have_measured = 1;
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
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
    size_t design_runs = 0;
    if (read_results_for_design(def, csv_file, metric_name, "confirm",
                                results, &design_runs) != 0) {
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

    double grand = grand_mean_of(effects, effect_count);
    double predicted = grand;
    double biggest_effect = 0.0;
    for (size_t i = 0; i < effect_count; i++) {
        size_t n = 0;
        const double *means = taguchi_effect_get_level_means(effects[i], &n);
        size_t best = best_level_of(effects[i], higher_is_better);
        if (n > 0) predicted += means[best] - grand;
        double r = taguchi_effect_get_range(effects[i]);
        if (r > biggest_effect) biggest_effect = r;
    }

    /*
     * The verdict, and what it is worth.
     *
     * Judged against the LARGEST MAIN EFFECT, because that is the size of the
     * thing the design claims to have measured: an error small next to it means
     * the additive model tracks what you set out to detect, and an error
     * comparable to it means the model missed something as big as the answer.
     *
     * This is a heuristic, not a significance test. A real one needs an error
     * variance, which a saturated array has no degrees of freedom to estimate
     * -- so it needs replication or an unassigned column, and neither is
     * something this command can conjure. Say the number and say what it does
     * not prove.
     */
    double error_abs = 0.0, error_share = 0.0;
    int held = 0;
    if (have_measured) {
        error_abs = fabs(measured - predicted);
        error_share = biggest_effect > 0.0 ? error_abs / biggest_effect : 0.0;
        held = biggest_effect > 0.0 ? (error_share <= 0.25) : (error_abs == 0.0);
    }

    if (as_json) {
        printf("{\n");
        printf("  \"tool\": \"taguchi\",\n");
        printf("  \"command\": \"confirm\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"metric\": ");
        json_str(metric_name);
        printf(",\n  \"objective\": \"%s\",\n", higher_is_better ? "maximize" : "minimize");
        /* `runs` so a consumer can check the file covered the design without
         * re-deriving the array size from the table. */
        printf("  \"runs\": %zu,\n", design_runs);
        printf("  \"grand_mean\": ");
        json_num(grand);
        printf(",\n  \"predicted\": ");
        json_num(predicted);
        printf(",\n  \"largest_main_effect\": ");
        json_num(biggest_effect);
        printf(",\n  \"settings\": [\n");
        for (size_t i = 0; i < effect_count; i++) {
            size_t n = 0;
            const double *means = taguchi_effect_get_level_means(effects[i], &n);
            size_t best = best_level_of(effects[i], higher_is_better);
            const char *fname = taguchi_effect_get_factor(effects[i]);
            printf("    {\"factor\": ");
            json_str(fname);
            printf(", \"level\": %zu, \"value\": ", best + 1);
            json_level_value(def, fname, best);
            printf(", \"contribution\": ");
            json_num(n > 0 ? means[best] - grand : 0.0);
            printf("}%s\n", i + 1 < effect_count ? "," : "");
        }
        printf("  ],\n");
        if (!have_measured) {
            printf("  \"measured\": null,\n");
            printf("  \"error\": null,\n");
            printf("  \"error_share_of_largest_effect\": null,\n");
            printf("  \"additive_model_held\": null\n");
        } else {
            printf("  \"measured\": ");
            json_num(measured);
            printf(",\n  \"error\": ");
            json_num(measured - predicted);
            printf(",\n  \"error_share_of_largest_effect\": ");
            json_num(error_share);
            printf(",\n  \"additive_model_held\": %s\n", held ? "true" : "false");
        }
        printf("}\n");
    } else {
        printf("Confirmation for metric: %s (%s)\n\n",
               metric_name, higher_is_better ? "maximizing" : "minimizing");
        printf("Run this combination:\n");
        for (size_t i = 0; i < effect_count; i++) {
            size_t n = 0;
            const double *means = taguchi_effect_get_level_means(effects[i], &n);
            size_t best = best_level_of(effects[i], higher_is_better);
            const char *fname = taguchi_effect_get_factor(effects[i]);
            size_t fi = def_index_of(def, fname);
            const char *val = (fi == (size_t)-1) ? NULL
                            : taguchi_def_get_factor_level(def, fi, best);
            printf("  %-20s %-16s (contributes %+.4g)\n",
                   fname, val ? val : "?", n > 0 ? means[best] - grand : 0.0);
        }
        printf("\nGrand mean          %.6g\n", grand);
        printf("Predicted response  %.6g\n", predicted);

        if (!have_measured) {
            printf("\nThat prediction is the HYPOTHESIS, not the result. The array never\n"
                   "ran this combination, so nothing here has tested whether the factors\n"
                   "actually add up. Run it, then:\n"
                   "  taguchi confirm %s %s --measured <value>\n", tgu_file, csv_file);
        } else {
            printf("Measured response   %.6g\n", measured);
            printf("Error               %+.6g", measured - predicted);
            if (biggest_effect > 0.0)
                printf("  (%.0f%% of the largest main effect)", 100.0 * error_share);
            printf("\n\n");
            if (held) {
                printf("The additive prediction HELD. The factors behave independently at\n"
                       "these settings, so the main effects can be read as the answer.\n");
            } else {
                printf("The additive prediction did NOT hold. The measurement is off by a\n"
                       "margin comparable to the effects themselves, which means something\n"
                       "the array could not see -- an interaction, or aliasing. Resolve the\n"
                       "suspect pair with `grid`, and do not act on the ranking until you\n"
                       "have.\n");
            }
            printf("\nThis compares one run against one prediction. It is a sanity check,\n"
                   "not a significance test: that needs an error variance, and a saturated\n"
                   "array has no degrees of freedom left to estimate one. Replicate, or\n"
                   "leave a column unassigned, if you need the stronger claim.\n");
        }
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

    int as_json = 0;

    /* Parse optional flags */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) {
            metric_name = argv[++i];
        } else if (strcmp(argv[i], "--minimize") == 0) {
            higher_is_better = false;
        } else if (strcmp(argv[i], "--json") == 0) {
            as_json = 1;
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
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

    size_t design_runs = 0;
    if (read_results_for_design(def, csv_file, metric_name, "analyze",
                                results, &design_runs) != 0) {
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

    /*
     * The recommendation is the deliverable, and it was the least usable thing
     * in the output: taguchi_recommend_optimal names the best LEVEL INDEX
     * ("batch=level_1"), and the CLI never printed what level_1 was. A reader
     * had to map indices back through the .tgu by hand. In --json each
     * recommended setting carries the index, the VALUE, and the mean it won
     * on, so it can be applied directly.
     *
     * `text` is the library's own string, emitted alongside the structured
     * form. Two renderings of one decision: a test pins them in agreement, so
     * a divergence fails the build instead of reaching a consumer.
     */
    char recommendation[1024];
    int have_rec = taguchi_recommend_optimal((const taguchi_main_effect_t **)effects,
                                             effect_count, higher_is_better,
                                             recommendation, sizeof(recommendation)) == 0;

    if (as_json) {
        printf("{\n");
        printf("  \"tool\": \"taguchi\",\n");
        printf("  \"command\": \"analyze\",\n");
        printf("  \"schema\": %d,\n", TAGUCHI_JSON_SCHEMA);
        printf("  \"metric\": ");
        json_str(metric_name);
        printf(",\n  \"objective\": \"%s\",\n",
               higher_is_better ? "maximize" : "minimize");
        /* `runs` so a consumer can check the file covered the design without
         * re-deriving the array size from the table. */
        printf("  \"runs\": %zu,\n", design_runs);
        printf("  \"factor_count\": %zu,\n", effect_count);
        print_effects_json(def, effects, effect_count);
        printf(",\n");

        if (!have_rec) {
            printf("  \"recommendation\": null\n");
        } else {
            printf("  \"recommendation\": {\n");
            printf("    \"text\": ");
            json_str(recommendation);
            printf(",\n    \"settings\": [\n");
            for (size_t i = 0; i < effect_count; i++) {
                size_t level_count = 0;
                const double *means =
                    taguchi_effect_get_level_means(effects[i], &level_count);
                if (level_count == 0) continue;

                /* Same rule the library applies: the best level mean, highest
                 * or lowest according to the objective. */
                size_t best = 0;
                for (size_t lv = 1; lv < level_count; lv++) {
                    int better = higher_is_better ? (means[lv] > means[best])
                                                  : (means[lv] < means[best]);
                    if (better) best = lv;
                }
                printf("      {\"factor\": ");
                json_str(taguchi_effect_get_factor(effects[i]));
                printf(", \"level\": %zu, \"value\": ", best + 1);
                json_level_value(def, taguchi_effect_get_factor(effects[i]), best);
                printf(", \"mean\": ");
                json_num(means[best]);
                printf("}%s\n", i + 1 < effect_count ? "," : "");
            }
            printf("    ]\n  }\n");
        }
        printf("}\n");

        taguchi_free_effects(effects, effect_count);
        taguchi_free_result_set(results);
        taguchi_free_definition(def);
        return 0;
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
    if (have_rec) {
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
    } else if (strcmp(command, "robust") == 0) {
        return cmd_robust(sub_argc, sub_argv);
    } else if (strcmp(command, "confirm") == 0) {
        return cmd_confirm(sub_argc, sub_argv);
    } else if (strcmp(command, "effects") == 0) {
        return cmd_effects(sub_argc, sub_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
