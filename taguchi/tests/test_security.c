/*
 * tests/test_security.c
 *
 * Security and boundary tests for the Taguchi library parser.
 * Verifies that adversarial inputs produce clean errors rather than
 * buffer overflows, crashes, or undefined behaviour.
 */

#include "test_framework.h"
#include "../src/lib/parser.h"
#include "../src/config.h"
#include "../include/taguchi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Factor name boundary tests
 * ---------------------------------------------------------------------- */

/* A factor name of exactly MAX_FACTOR_NAME chars (one past the limit) must fail. */
TEST(parse_oversized_factor_name) {
    /* Build a name of MAX_FACTOR_NAME 'x' characters — one too many. */
    char long_name[MAX_FACTOR_NAME + 2];
    memset(long_name, 'x', MAX_FACTOR_NAME);
    long_name[MAX_FACTOR_NAME] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "factors:\n  %s: a, b\n", long_name);

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    ASSERT(result != 0);
    ASSERT(strlen(error) > 0);
}

/* A factor name of MAX_FACTOR_NAME - 1 chars (the maximum allowed) must succeed. */
TEST(parse_max_valid_factor_name) {
    char long_name[MAX_FACTOR_NAME];
    memset(long_name, 'x', MAX_FACTOR_NAME - 1);
    long_name[MAX_FACTOR_NAME - 1] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "factors:\n  %s: a, b\n", long_name);

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 1u);
}

/* -------------------------------------------------------------------------
 * Level value boundary tests
 * ---------------------------------------------------------------------- */

/* A level value of exactly MAX_LEVEL_VALUE chars (one past the limit) must fail. */
TEST(parse_oversized_level_value) {
    char long_val[MAX_LEVEL_VALUE + 2];
    memset(long_val, 'v', MAX_LEVEL_VALUE);
    long_val[MAX_LEVEL_VALUE] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "factors:\n  temp: %s, b\n", long_val);

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    ASSERT(result != 0);
    ASSERT(strlen(error) > 0);
}

/* A level value of MAX_LEVEL_VALUE - 1 chars (the maximum allowed) must succeed. */
TEST(parse_max_valid_level_value) {
    char long_val[MAX_LEVEL_VALUE];
    memset(long_val, 'v', MAX_LEVEL_VALUE - 1);
    long_val[MAX_LEVEL_VALUE - 1] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "factors:\n  temp: %s, b\n", long_val);

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, 1u);
}

/* -------------------------------------------------------------------------
 * Factor count boundary tests
 * ---------------------------------------------------------------------- */

/* Exactly MAX_FACTORS factors must parse successfully. */
TEST(parse_max_factors_at_limit) {
    /* Each factor line: "  fXXX: a, b\n" ~ 16 chars; header 10 chars. */
    size_t buf_size = 20 + (size_t)MAX_FACTORS * 24;
    char *content = malloc(buf_size);
    ASSERT_NOT_NULL(content);

    int pos = snprintf(content, buf_size, "factors:\n");
    for (int i = 0; i < MAX_FACTORS; i++) {
        pos += snprintf(content + pos, buf_size - (size_t)pos,
                        "  f%03d: a, b\n", i);
    }

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    free(content);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, (size_t)MAX_FACTORS);
}

/* MAX_FACTORS + 1 factors must fail with a clean error. */
TEST(parse_too_many_factors) {
    size_t buf_size = 20 + (size_t)(MAX_FACTORS + 2) * 24;
    char *content = malloc(buf_size);
    ASSERT_NOT_NULL(content);

    int pos = snprintf(content, buf_size, "factors:\n");
    for (int i = 0; i <= MAX_FACTORS; i++) {  /* one over the limit */
        pos += snprintf(content + pos, buf_size - (size_t)pos,
                        "  f%03d: a, b\n", i);
    }

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    free(content);
    ASSERT(result != 0);
    ASSERT(strlen(error) > 0);
}

/* -------------------------------------------------------------------------
 * Level count boundary tests
 * ---------------------------------------------------------------------- */

/*
 * Exactly MAX_LEVELS levels must parse successfully.
 * Note: split_string caps at MAX_LEVELS, so excess levels are silently
 * dropped — the parser never sees more than MAX_LEVELS tokens per factor.
 * This test verifies the cap is enforced without overflow.
 */
TEST(parse_max_levels_at_limit) {
    /* Build "  temp: l0, l1, ..., l(MAX_LEVELS-1)\n" */
    char content[4096];
    int pos = snprintf(content, sizeof(content), "factors:\n  temp: l0");
    for (int i = 1; i < MAX_LEVELS; i++) {
        pos += snprintf(content + pos, sizeof(content) - (size_t)pos, ", l%d", i);
    }
    snprintf(content + pos, sizeof(content) - (size_t)pos, "\n");

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factors[0].level_count, (size_t)MAX_LEVELS);
}

/*
 * More than MAX_LEVELS levels: the parser caps without crashing.
 * split_string truncates at MAX_LEVELS, so the extra levels are discarded.
 * The test verifies: no crash, no overflow, result is capped.
 */
TEST(parse_excess_levels_capped_not_crashed) {
    char content[4096];
    int pos = snprintf(content, sizeof(content), "factors:\n  temp: l0");
    /* Write MAX_LEVELS + 5 levels */
    for (int i = 1; i < MAX_LEVELS + 5; i++) {
        pos += snprintf(content + pos, sizeof(content) - (size_t)pos, ", l%d", i);
    }
    snprintf(content + pos, sizeof(content) - (size_t)pos, "\n");

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    /* Must not crash; if it succeeds, levels must be capped at MAX_LEVELS */
    if (result == 0) {
        ASSERT(def.factors[0].level_count <= (size_t)MAX_LEVELS);
    }
}

/* -------------------------------------------------------------------------
 * Null and empty input tests
 * ---------------------------------------------------------------------- */

/* NULL content pointer must return an error without crashing. */
TEST(parse_null_content) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(NULL, &def, error);
    ASSERT(result != 0);
}

/* NULL def pointer must return an error without crashing. */
TEST(parse_null_def) {
    char content[] = "factors:\n  temp: a, b\n";
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, NULL, error);
    ASSERT(result != 0);
}

/* Empty string must return a clean error (no factors found). */
TEST(parse_empty_string) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string("", &def, error);
    ASSERT(result != 0);
}

/* Content with no factors section must return a clean error. */
TEST(parse_no_factors_section) {
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string("# just a comment\n", &def, error);
    ASSERT(result != 0);
    ASSERT(strlen(error) > 0);
}

/* -------------------------------------------------------------------------
 * Special-character injection tests
 * ---------------------------------------------------------------------- */

/*
 * A factor name containing '=' would corrupt env var construction in cmd_run.
 * The parser itself does not reject '=' in names (it uses ':' as delimiter),
 * so "temp=bad: a, b" is parsed — the name becomes "temp=bad".
 * The CLI guards against this when building env var names with setenv().
 * This test documents the parser's behaviour and verifies there is no crash.
 */
TEST(parse_factor_name_with_equals_sign) {
    /* "temp=bad" ends at ':' so name is "temp=bad" */
    char content[] = "factors:\n  temp=bad: a, b\n";
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    /* Parser may succeed or fail; it must not crash */
    parse_experiment_def_from_string(content, &def, error);
    /* If it parsed, the name must not extend beyond MAX_FACTOR_NAME */
    if (def.factor_count > 0) {
        ASSERT(strlen(def.factors[0].name) < MAX_FACTOR_NAME);
    }
}

/*
 * Factor names/values with shell metacharacters ($, ;, `) must be stored
 * verbatim. The library does not interpret them — the CLI uses setenv()
 * which passes them as literal strings to the child environment.
 */
TEST(parse_factor_name_with_shell_metacharacters) {
    char content[] = "factors:\n  cache$size: low;128, high`val\n";
    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    /* Must not crash regardless of outcome */
    parse_experiment_def_from_string(content, &def, error);
    /* Values stored must be within bounds */
    if (def.factor_count > 0 && def.factors[0].level_count > 0) {
        ASSERT(strlen(def.factors[0].values[0]) < MAX_LEVEL_VALUE);
    }
}

/* -------------------------------------------------------------------------
 * Large (valid) input test
 * ---------------------------------------------------------------------- */

/*
 * A legitimate large .tgu — many factors with verbose names and values —
 * must parse correctly without overflows now that file reading is dynamic.
 * Uses 10 factors each with 3 long-but-valid level names.
 */
TEST(parse_large_valid_input) {
    /* Build ~10 KB of valid .tgu content */
    const int num_factors = 10;
    const int name_len    = MAX_FACTOR_NAME - 2;   /* safely under limit */
    const int val_len     = MAX_LEVEL_VALUE - 2;   /* safely under limit */

    /* Estimate: header + num_factors * (indent + name + ": " + 3 vals + "\n") */
    size_t buf_size = 16 + (size_t)num_factors *
                      (2 + (size_t)name_len + 2 + 3 * ((size_t)val_len + 2) + 1);
    char *content = malloc(buf_size);
    ASSERT_NOT_NULL(content);

    int pos = snprintf(content, buf_size, "factors:\n");
    for (int i = 0; i < num_factors; i++) {
        /* Factor name: 'f' + repeated 'a' chars + digit suffix */
        char fname[MAX_FACTOR_NAME];
        memset(fname, 'a', (size_t)name_len - 1);
        fname[0] = 'f';
        fname[name_len - 1] = (char)('0' + i);
        fname[name_len] = '\0';

        /* Level values: repeated 'v' chars */
        char lval[MAX_LEVEL_VALUE];
        memset(lval, 'v', (size_t)val_len - 1);
        lval[0] = 'L';
        lval[val_len - 1] = (char)('0' + i);
        lval[val_len] = '\0';

        pos += snprintf(content + pos, buf_size - (size_t)pos,
                        "  %s: %s1, %s2, %s3\n", fname, lval, lval, lval);
    }

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    int result = parse_experiment_def_from_string(content, &def, error);
    free(content);

    ASSERT_EQ(result, 0);
    ASSERT_EQ(def.factor_count, (size_t)num_factors);
}

/* -------------------------------------------------------------------------
 * Error buffer boundary test
 * ---------------------------------------------------------------------- */

/*
 * Even with a very long factor name triggering an error, the error message
 * must fit within TAGUCHI_ERROR_SIZE and be properly null-terminated.
 *
 * Note: set_error() uses vsnprintf which writes the null terminator
 * immediately after the message text, NOT at the last byte of the buffer.
 * The test verifies a null exists somewhere within the buffer — not that
 * the entire buffer was zeroed.
 */
TEST(error_buffer_never_overflows) {
    char long_name[MAX_FACTOR_NAME + 64];
    memset(long_name, 'x', MAX_FACTOR_NAME + 63);
    long_name[MAX_FACTOR_NAME + 63] = '\0';

    char content[512];
    snprintf(content, sizeof(content), "factors:\n  %s: a, b\n", long_name);

    ExperimentDef def;
    char error[TAGUCHI_ERROR_SIZE];
    /* Poison the buffer to detect any out-of-bounds write */
    memset(error, 0xAB, TAGUCHI_ERROR_SIZE);

    int result = parse_experiment_def_from_string(content, &def, error);

    /* Parser must have detected the oversized name */
    ASSERT(result != 0);

    /* Error buffer must contain a null terminator within its bounds.
     * vsnprintf guarantees this as long as the buffer size constant is used. */
    int null_found = 0;
    for (int i = 0; i < TAGUCHI_ERROR_SIZE; i++) {
        if (error[i] == '\0') { null_found = 1; break; }
    }
    ASSERT(null_found);
}
