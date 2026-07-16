/*
 * test_security.c — adversarial-input tests for the libdoe core (HARDENING.md
 * Phase 1). Hostile .space / results input must produce a clean error, never a
 * crash, overflow, or injection.
 */

#include "doe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* H3 — NULL inputs return an error, never dereference. */
static int test_null_inputs(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(NULL, &sp, err) != 0);
    CHECK(doe_space_parse("factors:\n  x: 0,1\n", NULL, err) != 0);
    CHECK(doe_space_parse_file(NULL, &sp, err) != 0);

    double resp[4];
    size_t got = 0;
    CHECK(doe_csv_read_metric(NULL, "response", resp, 4, &got, err) != 0);
    return 1;
}

/* H1 — resource caps reject absurd parameters before any allocation. */
static int test_param_caps(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  x: 0,1\nsamples: 999999999999\n", &sp, err) != 0);
    CHECK(doe_space_parse("factors:\n  x: 0,1\ntrajectories: 999999999\n", &sp, err) != 0);
    CHECK(doe_space_parse("factors:\n  x: 0,1\ngrid_levels: 100000\n", &sp, err) != 0);
    /* sane values still parse */
    CHECK(doe_space_parse("factors:\n  x: 0,1\nsamples: 1024\n", &sp, err) == 0);
    return 1;
}

/* H1 — overflow-checked multiply. */
static int test_size_mul_ok(void) {
    size_t out = 0;
    CHECK(doe_size_mul_ok(1000, 1000, &out) == 1 && out == 1000000);
    CHECK(doe_size_mul_ok(0, 12345, &out) == 1 && out == 0);
    CHECK(doe_size_mul_ok((size_t)-1, 2, &out) == 0);            /* overflow */
    CHECK(doe_size_mul_ok(((size_t)-1) / 4 + 1, 4, &out) == 0);  /* overflow */
    return 1;
}

/* H2 — HTML escaping neutralizes injection characters. */
static int test_html_escape(void) {
    char *e = doe_html_escape("<script>&\"'");
    CHECK(e != NULL);
    CHECK(strcmp(e, "&lt;script&gt;&amp;&quot;&#39;") == 0);
    doe_free(e);

    char *p = doe_html_escape("reflux_ratio");   /* plain text passes through */
    CHECK(p != NULL && strcmp(p, "reflux_ratio") == 0);
    doe_free(p);
    return 1;
}

/* H4 — a line longer than the CSV buffer is rejected, not silently mis-split. */
static int test_csv_long_line(void) {
    const char *path = "build/test_sec_long.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fprintf(f, "1,");
    for (int i = 0; i < 20000; i++) fputc('9', f);
    fputc('\n', f);
    fclose(f);

    double resp[8];
    size_t got = 0;
    char err[DOE_ERR_SIZE];
    CHECK(doe_csv_read_metric(path, "response", resp, 8, &got, err) != 0);
    remove(path);
    return 1;
}

/* run_id outside [1, max_rows] is rejected (no out-of-bounds write). */
static int test_csv_runid_bounds(void) {
    const char *path = "build/test_sec_rid.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fprintf(f, "999,3.14\n");   /* run_id far beyond max_rows */
    fclose(f);

    double resp[4];
    size_t got = 0;
    char err[DOE_ERR_SIZE];
    CHECK(doe_csv_read_metric(path, "response", resp, 4, &got, err) != 0);
    remove(path);
    return 1;
}

/* Parser boundary rejections — the .space parser holds the line taguchi's does. */
static int test_parser_boundaries(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];

    /* oversized factor name (DOE_MAX_NAME chars — one past the limit) */
    char name[DOE_MAX_NAME + 2];
    memset(name, 'x', DOE_MAX_NAME);
    name[DOE_MAX_NAME] = '\0';
    char buf[256];
    snprintf(buf, sizeof buf, "factors:\n  %s: a, b\n", name);
    CHECK(doe_space_parse(buf, &sp, err) != 0);
    CHECK(strlen(err) < DOE_ERR_SIZE);   /* error is bounded + terminated */

    /* one factor over the limit */
    char big[4096];
    int pos = snprintf(big, sizeof big, "factors:\n");
    for (int i = 0; i <= DOE_MAX_FACTORS; i++) {
        pos += snprintf(big + pos, sizeof big - (size_t)pos, "  f%03d: 0,1\n", i);
    }
    CHECK(doe_space_parse(big, &sp, err) != 0);

    /* malformed definitions */
    CHECK(doe_space_parse("factors:\n  x: 5, 1\n", &sp, err) != 0);       /* lo >= hi  */
    CHECK(doe_space_parse("factors:\n  x: -1, 10 log\n", &sp, err) != 0); /* log <= 0  */
    CHECK(doe_space_parse("nonsense without a colon\n", &sp, err) != 0);  /* junk line */
    CHECK(doe_space_parse("", &sp, err) != 0);                            /* empty     */
    return 1;
}

/* H6 — control characters in a factor name are rejected at parse time. */
static int test_space_rejects_ctrl_in_name(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  a\tb: 0,1\n", &sp, err) != 0);
    CHECK(doe_space_parse("factors:\n  a\x01" "b: 0,1\n", &sp, err) != 0);
    return 1;
}

/* H6 — control characters in a categorical level value are rejected. */
static int test_space_rejects_ctrl_in_level(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  a: x1, x\t2\n", &sp, err) != 0);
    CHECK(doe_space_parse("factors:\n  a: x1, x\x1b" "2\n", &sp, err) != 0);
    return 1;
}

/* H7 — non-finite bounds are rejected. The NaN case is the subtle one: NaN
 * compares false against everything, so `a >= b` never fires for it — only the
 * isfinite guard (deliberately placed *before* the ordering check) catches it.
 * The strstr pins that it is the finite guard rejecting, not something else. */
static int test_space_rejects_nonfinite_bounds(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  a: 0, inf\n", &sp, err) != 0);
    CHECK(strstr(err, "finite") != NULL);
    CHECK(doe_space_parse("factors:\n  a: -inf, 1\n", &sp, err) != 0);
    CHECK(strstr(err, "finite") != NULL);
    CHECK(doe_space_parse("factors:\n  a: 0, nan\n", &sp, err) != 0);
    CHECK(strstr(err, "finite") != NULL);
    CHECK(doe_space_parse("factors:\n  a: nan, 1\n", &sp, err) != 0);
    CHECK(strstr(err, "finite") != NULL);
    return 1;
}

/* H5 — inf/nan metric values in a results CSV are rejected on read. */
static int test_csv_rejects_nonfinite_response(void) {
    const char *bad[] = { "inf", "-inf", "nan" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const char *path = "build/test_sec_nonfinite.csv";
        FILE *f = fopen(path, "w");
        CHECK(f != NULL);
        fprintf(f, "1,2.5\n2,%s\n", bad[i]);
        fclose(f);

        double resp[4];
        size_t got = 0;
        char err[DOE_ERR_SIZE];
        CHECK(doe_csv_read_metric(path, "response", resp, 4, &got, err) != 0);
        CHECK(strstr(err, "non-finite") != NULL);
        remove(path);
    }
    return 1;
}

/* Negative control for H6: has_ctrl rejects only C0 + DEL, so UTF-8 factor
 * names (bytes >= 0x80) must keep parsing. Guards against "hardening" the
 * check into ASCII-only and silently breaking non-English names. */
static int test_space_allows_utf8(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  café: 0,1\n", &sp, err) == 0);
    CHECK(sp.factor_count == 1);
    CHECK(strcmp(sp.factors[0].name, "café") == 0);
    /* ...and in level values */
    CHECK(doe_space_parse("factors:\n  mode: rápido, lento\n", &sp, err) == 0);
    CHECK(strcmp(sp.factors[0].levels[0], "rápido") == 0);
    return 1;
}

int main(void) {
    printf("security / adversarial-input tests\n");
    RUN_TEST(test_null_inputs);
    RUN_TEST(test_param_caps);
    RUN_TEST(test_size_mul_ok);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_csv_long_line);
    RUN_TEST(test_csv_runid_bounds);
    RUN_TEST(test_parser_boundaries);
    RUN_TEST(test_space_rejects_ctrl_in_name);
    RUN_TEST(test_space_rejects_ctrl_in_level);
    RUN_TEST(test_space_rejects_nonfinite_bounds);
    RUN_TEST(test_csv_rejects_nonfinite_response);
    RUN_TEST(test_space_allows_utf8);
    return TEST_SUMMARY();
}
