/*
 * test_serializer.c — src/lib/serializer.c, which had no tests at all.
 *
 * `make coverage` put it at 48%, and the uncovered block was the whole of
 * escape_json_string()'s switch: every escape case, the code that decides what
 * the JSON a user consumes actually looks like.
 *
 * Two things here go beyond line coverage, because the buffer arithmetic in
 * serialize_runs_to_json deserves it:
 *
 *   - the growth path is stressed at the declared limits (MAX_FACTORS
 *     factors, MAX_LEVEL_VALUE-length values, every character escapable) so
 *     that `pos += snprintf(...)` -- which accumulates the length snprintf
 *     WOULD have written, not what it did -- is exercised where truncation
 *     could occur. Under ASan this is what would catch an overflow.
 *   - the emitted JSON is checked for structural validity, not just presence,
 *     so a mangled escape cannot pass as "some output was produced".
 */

#include "test_framework.h"
#include "include/taguchi.h"   /* the public API the effects serializer belongs to */
#include "src/lib/serializer.h"
#include "src/lib/generator.h"
#include "src/config.h"
#include <stdlib.h>
#include <string.h>

/* escape_json_string is not in the header but is non-static and is the
 * function under test here. */
extern char *escape_json_string(const char *input);

/* ---- escape_json_string ----------------------------------------------- */

TEST(serializer_escape_null_and_empty) {
    ASSERT_NULL(escape_json_string(NULL));

    char *e = escape_json_string("");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e, "");
    free(e);
}

TEST(serializer_escape_plain_text_unchanged) {
    char *e = escape_json_string("plain value 123");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e, "plain value 123");
    free(e);
}

/* Every branch of the switch, one per case. */
TEST(serializer_escape_each_case) {
    struct { const char *in; const char *want; } cases[] = {
        { "a\"b",  "a\\\"b"  },   /* quote      */
        { "a\\b",  "a\\\\b"  },   /* backslash  */
        { "a\bb",  "a\\bb"   },   /* backspace  */
        { "a\fb",  "a\\fb"   },   /* form feed  */
        { "a\nb",  "a\\nb"   },   /* newline    */
        { "a\rb",  "a\\rb"   },   /* carriage   */
        { "a\tb",  "a\\tb"   },   /* tab        */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char *e = escape_json_string(cases[i].in);
        ASSERT_NOT_NULL(e);
        ASSERT_STR_EQ(e, cases[i].want);
        free(e);
    }
}

/* Worst case for the allocation: every character needs escaping, so the
 * output is exactly twice the input. escape_json_string allocates
 * len * 2 + 1, which must be exactly sufficient -- not one byte short. */
TEST(serializer_escape_all_escapable_is_exact_fit) {
    char in[128];
    for (size_t i = 0; i < sizeof in - 1; i++) in[i] = '"';
    in[sizeof in - 1] = '\0';

    char *e = escape_json_string(in);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(strlen(e), (sizeof in - 1) * 2);
    for (size_t i = 0; i < strlen(e); i += 2) {
        ASSERT_EQ(e[i], '\\');
        ASSERT_EQ(e[i + 1], '"');
    }
    free(e);
}

/* ---- serialize_runs_to_json ------------------------------------------- */

TEST(serializer_runs_null_or_empty_is_empty_array) {
    char *j = serialize_runs_to_json(NULL, 0);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(j, "[]");
    free_serialized_string(j);

    ExperimentRun dummy;
    memset(&dummy, 0, sizeof dummy);
    j = serialize_runs_to_json(&dummy, 0);
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(j, "[]");
    free_serialized_string(j);

    j = serialize_runs_to_json(NULL, 5);          /* NULL wins over count */
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(j, "[]");
    free_serialized_string(j);
}

/* Brackets balanced, one object per run, every factor present. */
static void check_json_shape(const char *j, size_t nruns) {
    ASSERT_NOT_NULL(j);
    ASSERT_EQ(j[0], '[');
    ASSERT_EQ(j[strlen(j) - 1], ']');

    size_t open = 0, close = 0;
    for (const char *p = j; *p; p++) {
        if (*p == '{') open++;
        if (*p == '}') close++;
    }
    ASSERT_EQ(open, nruns);
    ASSERT_EQ(close, nruns);
}

TEST(serializer_runs_single) {
    ExperimentRun run;
    memset(&run, 0, sizeof run);
    run.run_id = 1;
    run.factor_count = 2;
    strcpy(run.factor_names[0], "temp");
    strcpy(run.values[0], "hot");
    strcpy(run.factor_names[1], "ph");
    strcpy(run.values[1], "7");

    char *j = serialize_runs_to_json(&run, 1);
    check_json_shape(j, 1);
    ASSERT_NOT_NULL(strstr(j, "\"run_id\": 1"));
    ASSERT_NOT_NULL(strstr(j, "\"temp\": \"hot\""));
    ASSERT_NOT_NULL(strstr(j, "\"ph\": \"7\""));
    free_serialized_string(j);
}

TEST(serializer_runs_multiple_are_comma_separated) {
    ExperimentRun runs[3];
    memset(runs, 0, sizeof runs);
    for (size_t i = 0; i < 3; i++) {
        runs[i].run_id = i + 1;
        runs[i].factor_count = 1;
        strcpy(runs[i].factor_names[0], "f");
        snprintf(runs[i].values[0], MAX_LEVEL_VALUE, "v%zu", i);
    }

    char *j = serialize_runs_to_json(runs, 3);
    check_json_shape(j, 3);
    /* Exactly two separators between three objects. */
    size_t seps = 0;
    for (const char *p = j; (p = strstr(p, "},")) != NULL; p++) seps++;
    ASSERT_EQ(seps, (size_t)2);
    ASSERT_NOT_NULL(strstr(j, "\"run_id\": 3"));
    free_serialized_string(j);
}

/* Escaping must survive the round trip into the emitted document: a value
 * containing a quote must appear escaped, never raw. */
TEST(serializer_runs_escapes_values) {
    ExperimentRun run;
    memset(&run, 0, sizeof run);
    run.run_id = 1;
    run.factor_count = 1;
    strcpy(run.factor_names[0], "quo\"te");
    strcpy(run.values[0], "tab\there");

    char *j = serialize_runs_to_json(&run, 1);
    ASSERT_NOT_NULL(j);
    ASSERT_NOT_NULL(strstr(j, "quo\\\"te"));
    ASSERT_NOT_NULL(strstr(j, "tab\\there"));
    /* and no raw tab leaked into the document */
    ASSERT_NULL(strchr(j, '\t'));
    free_serialized_string(j);
}

/*
 * The growth path, at the declared limits. serialize_runs_to_json starts at
 * count * BUFFER_SIZE and doubles as it goes, using `pos += snprintf(...)`,
 * which accumulates the length snprintf WOULD have written. If a write ever
 * truncates, pos passes the end of the buffer and the next `estimated_size -
 * pos` underflows to an enormous size_t. This drives it as hard as the
 * declared constants allow: one run, MAX_FACTORS factors, names and values at
 * their maximum length, every character escapable so each doubles on output.
 */
TEST(serializer_runs_growth_at_declared_limits) {
    ExperimentRun *run = calloc(1, sizeof *run);
    ASSERT_NOT_NULL(run);
    run->run_id = 1;
    run->factor_count = MAX_FACTORS;

    for (size_t f = 0; f < MAX_FACTORS; f++) {
        memset(run->factor_names[f], '"', MAX_FACTOR_NAME - 1);
        run->factor_names[f][MAX_FACTOR_NAME - 1] = '\0';
        memset(run->values[f], '\\', MAX_LEVEL_VALUE - 1);
        run->values[f][MAX_LEVEL_VALUE - 1] = '\0';
    }

    char *j = serialize_runs_to_json(run, 1);
    check_json_shape(j, 1);

    /* Every factor must be represented: count the separators the writer emits
     * between fields. One per factor, plus none for run_id itself. */
    size_t fields = 0;
    for (const char *p = j; (p = strstr(p, ", \"")) != NULL; p++) fields++;
    ASSERT_EQ(fields, (size_t)MAX_FACTORS);

    free_serialized_string(j);
    free(run);
}

/* Many runs, each modest -- exercises the 75%-full resize between runs. */
TEST(serializer_runs_many_runs_growth) {
    const size_t N = 64;
    ExperimentRun *runs = calloc(N, sizeof *runs);
    ASSERT_NOT_NULL(runs);
    for (size_t i = 0; i < N; i++) {
        runs[i].run_id = i + 1;
        runs[i].factor_count = 8;
        for (size_t f = 0; f < 8; f++) {
            snprintf(runs[i].factor_names[f], MAX_FACTOR_NAME, "factor_%zu", f);
            memset(runs[i].values[f], 'v', MAX_LEVEL_VALUE - 1);
            runs[i].values[f][MAX_LEVEL_VALUE - 1] = '\0';
        }
    }

    char *j = serialize_runs_to_json(runs, N);
    check_json_shape(j, N);
    ASSERT_NOT_NULL(strstr(j, "\"run_id\": 64"));
    free_serialized_string(j);
    free(runs);
}

/* ---- taguchi_effects_to_json ------------------------------------------
 *
 * The public effects serializer. It used `pos += snprintf(...)` against a
 * guessed buffer -- the defect STATUS.md says this tree keeps reproducing --
 * and interpolated the factor name RAW, so a quote in a name produced a
 * document no parser would accept.
 */

TEST(effects_json_escapes_a_quoted_factor_name) {
    taguchi_experiment_def_t *def = taguchi_parse_definition(
        "factors:\n  a\"b: 1, 2\n  c: 1, 2\narray: L4\n", (char[TAGUCHI_ERROR_SIZE]){0});
    ASSERT_NOT_NULL(def);

    taguchi_result_set_t *rs = taguchi_create_result_set(def, "response");
    ASSERT_NOT_NULL(rs);
    char aerr[TAGUCHI_ERROR_SIZE];
    for (size_t i = 1; i <= 4; i++)
        ASSERT_EQ(taguchi_add_result(rs, i, (double)i, aerr), 0);

    taguchi_main_effect_t **eff = NULL; size_t n = 0;
    char err2[TAGUCHI_ERROR_SIZE];
    ASSERT_EQ(taguchi_calculate_main_effects(rs, &eff, &n, err2), 0);

    char *j = taguchi_effects_to_json((const taguchi_main_effect_t **)eff, n);
    ASSERT_NOT_NULL(j);
    /* The quote is escaped, and no bare `a"b` survives. */
    ASSERT_NOT_NULL(strstr(j, "a\\\"b"));
    taguchi_free_string(j);

    taguchi_free_effects(eff, n);
    taguchi_free_result_set(rs);
    taguchi_free_definition(def);
}

TEST(effects_json_is_complete_for_every_factor) {
    taguchi_experiment_def_t *def = taguchi_parse_definition(
        "factors:\n  aaa: 1, 2, 3\n  bbb: 1, 2, 3\n  ccc: 1, 2, 3\n  ddd: 1, 2, 3\narray: L9\n",
        (char[TAGUCHI_ERROR_SIZE]){0});
    ASSERT_NOT_NULL(def);

    taguchi_result_set_t *rs = taguchi_create_result_set(def, "response");
    char aerr[TAGUCHI_ERROR_SIZE];
    for (size_t i = 1; i <= 9; i++)
        ASSERT_EQ(taguchi_add_result(rs, i, (double)i * 1.5, aerr), 0);

    taguchi_main_effect_t **eff = NULL; size_t n = 0;
    char err2[TAGUCHI_ERROR_SIZE];
    ASSERT_EQ(taguchi_calculate_main_effects(rs, &eff, &n, err2), 0);

    char *j = taguchi_effects_to_json((const taguchi_main_effect_t **)eff, n);
    ASSERT_NOT_NULL(j);
    /* Every factor present, and the document CLOSES -- a truncating writer
     * would drop the tail and leave an unterminated array. */
    ASSERT_NOT_NULL(strstr(j, "aaa"));
    ASSERT_NOT_NULL(strstr(j, "ddd"));
    ASSERT_EQ(j[strlen(j) - 1], ']');
    taguchi_free_string(j);

    taguchi_free_effects(eff, n);
    taguchi_free_result_set(rs);
    taguchi_free_definition(def);
}

/* ---- definition level accessors ---------------------------------------
 *
 * The pair that turns taguchi_recommend_optimal's "A=level_3" into the value
 * it names. Added with the --json work; the count half went uncovered, which
 * is how an accessor ships returning the wrong thing.
 */

TEST(def_level_accessors_agree_with_the_definition) {
    char perr[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(
        "factors:\n  a: p, q, r\n  b: x, y, z\narray: L9\n", perr);
    ASSERT_NOT_NULL(def);

    ASSERT_EQ(taguchi_def_get_factor_count(def), 2);
    ASSERT_EQ(taguchi_def_get_factor_level_count(def, 0), 3);
    ASSERT_EQ(taguchi_def_get_factor_level_count(def, 1), 3);
    ASSERT_STR_EQ(taguchi_def_get_factor_level(def, 0, 0), "p");
    ASSERT_STR_EQ(taguchi_def_get_factor_level(def, 0, 2), "r");
    ASSERT_STR_EQ(taguchi_def_get_factor_level(def, 1, 1), "y");

    /* Out of range is 0 / NULL, not a read past the array. */
    ASSERT_EQ(taguchi_def_get_factor_level_count(def, 99), 0);
    ASSERT_EQ(taguchi_def_get_factor_level_count(NULL, 0), 0);
    ASSERT_TRUE(taguchi_def_get_factor_level(def, 0, 99) == NULL);
    ASSERT_TRUE(taguchi_def_get_factor_level(def, 99, 0) == NULL);
    ASSERT_TRUE(taguchi_def_get_factor_level(NULL, 0, 0) == NULL);

    taguchi_free_definition(def);
}

TEST(run_get_factor_names_is_null_terminated) {
    /* Public API, and the last function in the tree with no coverage at all.
     * The contract is "NULL-terminated array of factor names" -- a caller
     * walking it without that terminator runs off the end, so the terminator
     * is the thing worth asserting. */
    char perr[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(
        "factors:\n  alpha: 1, 2\n  beta: 1, 2\narray: L4\n", perr);
    ASSERT_NOT_NULL(def);

    taguchi_experiment_run_t **runs = NULL; size_t n = 0;
    char gerr[TAGUCHI_ERROR_SIZE];
    ASSERT_EQ(taguchi_generate_runs(def, &runs, &n, gerr), 0);
    ASSERT_TRUE(n > 0);

    const char **names = taguchi_run_get_factor_names(runs[0]);
    ASSERT_NOT_NULL(names);
    size_t count = 0;
    while (names[count] != NULL) count++;      /* must terminate */
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(taguchi_run_get_value(runs[0], names[0]));

    ASSERT_TRUE(taguchi_run_get_factor_names(NULL) == NULL);

    taguchi_free_runs(runs, n);
    taguchi_free_definition(def);
}

/* ---- free ------------------------------------------------------------- */

TEST(serializer_free_null_is_safe) {
    free_serialized_string(NULL);
}
