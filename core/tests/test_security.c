/*
 * test_security.c — adversarial-input tests for the libdoe core (H-IDs refer
 * to SECURITY.md). Hostile .space / results input must produce a clean error,
 * never a crash, overflow, or injection.
 */

#include "doe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* getpid, for the scratch file name */

/*
 * Every distinct way a factor definition can be malformed.
 *
 * These branches existed with no test behind them: `space.c` sat at 78% and
 * the missing lines were almost all rejection paths. A rejection path that is
 * never exercised is exactly where a "clean error" quietly becomes a crash or
 * an unterminated buffer, because nothing ever looks at it.
 *
 * Each case asserts the same three things the invariant promises: non-zero
 * return, a NUL-terminated error inside DOE_ERR_SIZE, and no crash.
 */
static int check_rejected(const char *spec, const char *want_substr) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    memset(err, 'A', sizeof err);          /* no terminator to start with */
    if (doe_space_parse(spec, &sp, err) == 0) {
        printf("\n    accepted but should not have: <<%s>>\n", spec);
        return 0;
    }
    if (memchr(err, '\0', DOE_ERR_SIZE) == NULL) {
        printf("\n    error not NUL-terminated within DOE_ERR_SIZE\n");
        return 0;
    }
    if (want_substr && !strstr(err, want_substr)) {
        printf("\n    error '%s' does not mention '%s'\n", err, want_substr);
        return 0;
    }
    return 1;
}

static int test_space_rejects_malformed_factors(void) {
    char buf[8192];

    /* more levels than the pool holds (DOE_MAX_LEVELS) */
    size_t off = (size_t)snprintf(buf, sizeof buf, "factors:\n  a: v0");
    for (int i = 1; i <= DOE_MAX_LEVELS && off < sizeof buf - 16; i++)
        off += (size_t)snprintf(buf + off, sizeof buf - off, ", v%d", i);
    snprintf(buf + off, sizeof buf - off, "\n");
    CHECK(check_rejected(buf, "too many values"));

    /* a factor with nothing after the colon */
    CHECK(check_rejected("factors:\n  a:\n", "no values"));

    /* one level is not a choice, so it cannot be a categorical factor */
    CHECK(check_rejected("factors:\n  a: solo cat\n", ">= 2 levels"));

    /* an explicit scale marker with the wrong number of bounds */
    CHECK(check_rejected("factors:\n  a: 1, 2, 3 log\n", "two numeric bounds"));
    CHECK(check_rejected("factors:\n  a: 1, 2, 3 linear\n", "two numeric bounds"));

    /* a level value longer than the pool slot */
    {
        char lvl[DOE_MAX_VALUE + 8];
        memset(lvl, 'z', DOE_MAX_VALUE + 1);
        lvl[DOE_MAX_VALUE + 1] = '\0';
        snprintf(buf, sizeof buf, "factors:\n  a: ok, %s\n", lvl);
        CHECK(check_rejected(buf, "too long"));
    }

    /* more categorical factors than the level pool has slots for */
    off = (size_t)snprintf(buf, sizeof buf, "factors:\n");
    for (int i = 0; i <= DOE_MAX_CATEGORICAL && off < sizeof buf - 32; i++)
        off += (size_t)snprintf(buf + off, sizeof buf - off, "  c%d: p, q\n", i);
    CHECK(check_rejected(buf, "categorical"));
    return 1;
}

/*
 * The same for `groups:`. Groups must PARTITION the factors -- an overlap
 * moves a factor twice in one step and an omission never screens it -- so
 * every one of these is a correctness failure, not a style complaint.
 */
static int test_space_rejects_malformed_groups(void) {
    char buf[8192];

    CHECK(check_rejected("factors:\n  a: 0,1\n  b: 0,1\n"
                         "groups:\n  g1: a\n  g1: b\n", "duplicate group"));
    CHECK(check_rejected("factors:\n  a: 0,1\n  b: 0,1\n"
                         "groups:\n  g1: a\n  g2: nosuch\n", "nosuch"));
    CHECK(check_rejected("factors:\n  a: 0,1\n  b: 0,1\n"
                         "groups:\n  g1: a, , b\n", "empty member"));
    CHECK(check_rejected("factors:\n  a: 0,1\n  b: 0,1\n"
                         "groups:\n  g1:\n", "no members"));

    /* a group name past DOE_MAX_NAME */
    {
        char name[DOE_MAX_NAME + 8];
        memset(name, 'g', DOE_MAX_NAME + 1);
        name[DOE_MAX_NAME + 1] = '\0';
        snprintf(buf, sizeof buf, "factors:\n  a: 0,1\ngroups:\n  %s: a\n", name);
        CHECK(check_rejected(buf, "too long"));
    }

    /* more groups than DOE_MAX_GROUPS */
    {
        size_t off = (size_t)snprintf(buf, sizeof buf, "factors:\n");
        for (int i = 0; i <= DOE_MAX_GROUPS && off < sizeof buf - 32; i++)
            off += (size_t)snprintf(buf + off, sizeof buf - off, "  f%d: 0,1\n", i);
        off += (size_t)snprintf(buf + off, sizeof buf - off, "groups:\n");
        for (int i = 0; i <= DOE_MAX_GROUPS && off < sizeof buf - 32; i++)
            off += (size_t)snprintf(buf + off, sizeof buf - off, "  g%d: f%d\n", i, i);
        CHECK(check_rejected(buf, "too many groups"));
    }
    return 1;
}

/*
 * doe_space_parse_file — a public entry point that had NO coverage at all.
 *
 * The directory case is the interesting one. fopen() on a directory SUCCEEDS
 * on Linux and ftell() then reports LONG_MAX, so before the size cap this
 * reached malloc with 9 exabytes and reported "out of memory" -- accurate
 * about the allocation, useless about the cause. It is now bounded before the
 * allocation (SECURITY.md H1) and says what is actually wrong.
 */
static int test_space_parse_file_paths(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    char path[256];
    snprintf(path, sizeof path, "build/test_space_%ld.space", (long)getpid());

    /* a real file round-trips and yields the same thing the string parser does */
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("factors:\n  x: 0.0, 10.0\n  y: a, b, c\nseed: 77\nsamples: 64\n", f);
    fclose(f);

    CHECK(doe_space_parse_file(path, &sp, err) == 0);
    CHECK(sp.factor_count == 2);
    CHECK(sp.seed == 77 && sp.samples == 64);
    CHECK(sp.factors[0].scale == DOE_LINEAR);
    CHECK(sp.factors[1].scale == DOE_CATEGORICAL && sp.factors[1].level_count == 3);
    remove(path);

    /* a parse error in a file is still a parse error, and still located */
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("factors:\n  x: 0,1\n  y: 5.0, 1.0\n", f);
    fclose(f);
    CHECK(doe_space_parse_file(path, &sp, err) != 0);
    CHECK(strstr(err, "line 3:") != NULL);
    remove(path);

    /* missing file */
    CHECK(doe_space_parse_file("build/definitely_not_here.space", &sp, err) != 0);
    CHECK(strstr(err, "cannot open") != NULL);

    /* a directory: clean error naming the real problem, no giant allocation */
    memset(err, 'A', sizeof err);
    CHECK(doe_space_parse_file("build", &sp, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "limit") != NULL);

    /* NULL guards (H3) */
    CHECK(doe_space_parse_file(NULL, &sp, err) != 0);
    CHECK(doe_space_parse_file(path, NULL, err) != 0);
    return 1;
}

/*
 * The results CSV is the third trust boundary (SECURITY.md's threat model
 * table) and its rejection paths were the least exercised of the three: an
 * adversarial or merely broken results file reaches doe_csv_read_metric before
 * anything else looks at it.
 *
 * Every case here is one a real run produces -- a crashed model writing a
 * partial row, a script printing "NaN", a metric column that moved -- so each
 * must be a located, bounded error rather than a silently wrong response
 * vector feeding the estimators.
 */
static int csv_rejected(const char *body, const char *metric,
                        size_t max_rows, const char *want_substr) {
    char path[256];
    snprintf(path, sizeof path, "build/test_csv_%ld.csv", (long)getpid());
    FILE *f = fopen(path, "w");
    if (!f) { printf("\n    cannot create %s\n", path); return 0; }
    fputs(body, f);
    fclose(f);

    double resp[16];
    for (size_t i = 0; i < 16; i++) resp[i] = -999.0;
    size_t got = 0;
    char err[DOE_ERR_SIZE];
    memset(err, 'A', sizeof err);

    int rc = doe_csv_read_metric(path, metric, resp, max_rows, &got, err);
    remove(path);

    if (rc == 0) { printf("\n    accepted but should not have: <<%s>>\n", body); return 0; }
    if (memchr(err, '\0', DOE_ERR_SIZE) == NULL) {
        printf("\n    error not NUL-terminated within DOE_ERR_SIZE\n"); return 0;
    }
    if (want_substr && !strstr(err, want_substr)) {
        printf("\n    error '%s' does not mention '%s'\n", err, want_substr); return 0;
    }
    return 1;
}

static int test_csv_rejects_broken_results(void) {
    /* a metric column that is not in the header */
    CHECK(csv_rejected("run_id,yield\n1,3.0\n", "throughput", 4, "not in CSV header"));

    /* no header at all, and a metric that therefore cannot be located */
    CHECK(csv_rejected("1,3.0\n2,4.0\n", "yield", 4, "no header"));

    /* a truncated row — the classic half-written line from a crashed model */
    CHECK(csv_rejected("run_id,a,yield\n1,5,3.0\n2\n", "yield", 4, "column"));

    /* run_id that is not a number, or is out of range */
    CHECK(csv_rejected("run_id,yield\nabc,3.0\n", "yield", 4, "invalid run_id"));
    CHECK(csv_rejected("run_id,yield\n0,3.0\n", "yield", 4, "invalid run_id"));
    CHECK(csv_rejected("run_id,yield\n99,3.0\n", "yield", 4, "exceeds run count"));

    /* a metric value that is not a number, or is not finite (H5) */
    CHECK(csv_rejected("run_id,yield\n1,not_a_number\n", "yield", 4, "invalid value"));
    CHECK(csv_rejected("run_id,yield\n1,nan\n", "yield", 4, "non-finite"));
    CHECK(csv_rejected("run_id,yield\n1,inf\n", "yield", 4, "non-finite"));

    /* a file with a header and no data rows at all */
    CHECK(csv_rejected("run_id,yield\n", "yield", 4, "no data rows"));

    /* a file that does not exist */
    {
        double resp[4]; size_t got = 0; char err[DOE_ERR_SIZE];
        CHECK(doe_csv_read_metric("build/no_such_results.csv", "yield",
                                  resp, 4, &got, err) != 0);
        CHECK(strstr(err, "cannot open") != NULL);
    }
    return 1;
}

/*
 * And the reads that must SUCCEED, because a reader that rejects everything
 * would pass every test above. In particular a blank metric cell is skipped,
 * not an error: that is how a partially-completed run is meant to be read.
 */
static int test_csv_reads_valid_results(void) {
    char path[256];
    snprintf(path, sizeof path, "build/test_csv_ok_%ld.csv", (long)getpid());
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    /* out of order, a gap at run 3, and the metric in the third column */
    fputs("run_id,setting,yield\n"
          "2,x,20.5\n"
          "1,y,10.25\n"
          "3,z,\n"
          "4,w,40\n", f);
    fclose(f);

    double resp[4];
    for (size_t i = 0; i < 4; i++) resp[i] = -999.0;
    size_t got = 0;
    char err[DOE_ERR_SIZE];
    CHECK(doe_csv_read_metric(path, "yield", resp, 4, &got, err) == 0);
    CHECK(got == 3);                       /* the blank cell is skipped */
    CHECK(resp[0] == 10.25);               /* keyed by run_id, not file order */
    CHECK(resp[1] == 20.5);
    CHECK(resp[2] == -999.0);              /* untouched, so the caller can tell */
    CHECK(resp[3] == 40.0);

    /* the "response" metric is the documented default and needs no header */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("1,7.5\n2,8.5\n", f);
    fclose(f);
    for (size_t i = 0; i < 4; i++) resp[i] = -999.0;
    CHECK(doe_csv_read_metric(path, "response", resp, 4, &got, err) == 0);
    CHECK(got == 2 && resp[0] == 7.5 && resp[1] == 8.5);

    remove(path);
    return 1;
}

/*
 * Sizing a results file without a design to size against.
 *
 * `uq` has no design, so it used to pass its buffer capacity as `max_rows` and
 * grow on "buffer full". The reader treats a run_id past max_rows as a data
 * error, so the probe reported "run_id 1025 exceeds run count 1024" instead --
 * and uq was capped at 1024 rows. This is the function that replaced the
 * guess.
 */
static int test_csv_max_run_id(void) {
    const char *path = "build/test_maxrunid.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("run_id,setting,yield\n"
          "2,x,20.5\n"
          "1,y,10.25\n"
          "900,z,30\n"          /* the greatest id, not the row count */
          "4,w,40\n", f);
    fclose(f);

    size_t max_id = 0;
    char err[DOE_ERR_SIZE];
    CHECK(doe_csv_max_run_id(path, &max_id, err) == 0);
    CHECK(max_id == 900);

    /* A buffer sized by it is a buffer read_metric will not reject. */
    double *resp = malloc(max_id * sizeof *resp);
    CHECK(resp != NULL);
    for (size_t i = 0; i < max_id; i++) resp[i] = -999.0;
    size_t got = 0;
    CHECK(doe_csv_read_metric(path, "yield", resp, max_id, &got, err) == 0);
    CHECK(got == 4);
    CHECK(resp[899] == 30.0);
    free(resp);

    /* No header, the documented default: the first row is already data. */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("1,7.5\n2,8.5\n3,9.5\n", f);
    fclose(f);
    CHECK(doe_csv_max_run_id(path, &max_id, err) == 0);
    CHECK(max_id == 3);

    /* Comments and blank lines are not rows. */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("# a preamble, as a .front file carries\n\nrun_id,response\n5,1\n", f);
    fclose(f);
    CHECK(doe_csv_max_run_id(path, &max_id, err) == 0);
    CHECK(max_id == 5);

    /* Rejections: a bad id, an empty file, a missing file, NULL. */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("run_id,response\n1,1\nzero,2\n", f);
    fclose(f);
    CHECK(doe_csv_max_run_id(path, &max_id, err) != 0);

    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("run_id,response\n", f);
    fclose(f);
    CHECK(doe_csv_max_run_id(path, &max_id, err) != 0);

    CHECK(doe_csv_max_run_id("build/no_such_results.csv", &max_id, err) != 0);
    CHECK(doe_csv_max_run_id(NULL, &max_id, err) != 0);
    CHECK(doe_csv_max_run_id(path, NULL, err) != 0);

    remove(path);
    return 1;
}

/*
 * doe_table — the named-column reader `regress` and `desire` share.
 *
 * Each used to carry its own line splitter, and desire's came with a ceiling:
 * it refused a file past 100000 rows outright, which is a limit invented by
 * the reader rather than the data -- the same defect as the 1024-row cap that
 * made `uq` fail on real Monte-Carlo output.
 */
static int test_table_read(void) {
    const char *path = "build/test_table.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    /* a comment, a blank line, padded names and cells, a short row, an empty
     * cell, and a non-numeric one -- everything a real results file does */
    fputs("# preamble, as a .front file carries\n"
          "\n"
          "run_id, yield ,cost\n"
          "1,10.5,3\n"
          "2,  20.5 ,4\n"
          "3,,5\n"
          "4,oops,6\n"
          "5,30\n", f);
    fclose(f);

    doe_table_t t;
    char err[DOE_ERR_SIZE];
    CHECK(doe_table_read(path, &t, err) == 0);
    CHECK(t.ncols == 3);
    CHECK(t.nrows == 5);

    /* Names are trimmed, so a lookup by name matches " yield ". */
    CHECK(doe_table_col(&t, "yield") == 1);
    CHECK(doe_table_col(&t, "run_id") == 0);
    CHECK(doe_table_col(&t, "cost") == 2);
    CHECK(doe_table_col(&t, "nope") == -1);

    double v = 0.0;
    CHECK(doe_table_number(&t, 0, 1, &v) == 0 && v == 10.5);
    CHECK(doe_table_number(&t, 1, 1, &v) == 0 && v == 20.5);  /* cell trimmed */
    CHECK(doe_table_number(&t, 2, 1, &v) != 0);               /* empty */
    CHECK(doe_table_number(&t, 3, 1, &v) != 0);               /* not a number */
    CHECK(doe_table_number(&t, 4, 1, &v) == 0 && v == 30.0);
    CHECK(doe_table_number(&t, 4, 2, &v) != 0);               /* short row */
    CHECK(doe_table_number(&t, 99, 1, &v) != 0);              /* out of range */

    /* Rows come back exactly as they arrived -- `desire` echoes them. */
    CHECK(strcmp(doe_table_row(&t, 1), "2,  20.5 ,4") == 0);
    CHECK(doe_table_row(&t, 99) == NULL);
    doe_table_free(&t);

    /* A header with no data rows is not a table. */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("run_id,yield\n", f);
    fclose(f);
    CHECK(doe_table_read(path, &t, err) != 0);
    doe_table_free(&t);

    /* Neither is a file of nothing but comments. */
    remove(path);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("# all comment\n# and nothing else\n", f);
    fclose(f);
    CHECK(doe_table_read(path, &t, err) != 0);
    doe_table_free(&t);

    CHECK(doe_table_read("build/no_such_table.csv", &t, err) != 0);
    doe_table_free(&t);
    CHECK(doe_table_read(NULL, &t, err) != 0);

    remove(path);
    return 1;
}

/* No ceiling of the reader's own invention: past desire's old 100000-row cap. */
static int test_table_grows_past_the_old_cap(void) {
    const char *path = "build/test_table_big.csv";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("run_id,yield\n", f);
    for (size_t i = 1; i <= 120000; i++) fprintf(f, "%zu,%zu\n", i, i * 2);
    fclose(f);

    doe_table_t t;
    char err[DOE_ERR_SIZE];
    CHECK(doe_table_read(path, &t, err) == 0);
    CHECK(t.nrows == 120000);
    double v = 0.0;
    CHECK(doe_table_number(&t, 119999, 1, &v) == 0 && v == 240000.0);
    doe_table_free(&t);

    remove(path);
    return 1;
}

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

    /*
     * One factor over the limit. Sized from DOE_MAX_FACTORS rather than a
     * fixed 4096: when that cap went from 64 to 1024 this buffer overflowed
     * and aborted the suite. The offset is also clamped -- `pos += snprintf`
     * accumulates the length snprintf WOULD have written, the same defect
     * already fixed in pareto's error builder and checked for in the
     * serializer.
     */
    size_t bigsz = (size_t)(DOE_MAX_FACTORS + 2) * 32 + 64;
    char *big = malloc(bigsz);
    CHECK(big != NULL);
    size_t pos = 0;
    int n = snprintf(big, bigsz, "factors:\n");
    pos = (n > 0 && (size_t)n < bigsz) ? (size_t)n : 0;
    for (int i = 0; i <= DOE_MAX_FACTORS && pos + 1 < bigsz; i++) {
        n = snprintf(big + pos, bigsz - pos, "  f%04d: 0,1\n", i);
        if (n < 0) break;
        pos += (size_t)n;
        if (pos >= bigsz) { pos = bigsz - 1; break; }
    }
    CHECK(doe_space_parse(big, &sp, err) != 0);
    free(big);

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
    CHECK(strcmp(sp.levels[sp.factors[0].level_slot][0], "rápido") == 0);
    return 1;
}

int main(void) {
    printf("security / adversarial-input tests\n");
    RUN_TEST(test_space_rejects_malformed_factors);
    RUN_TEST(test_space_rejects_malformed_groups);
    RUN_TEST(test_space_parse_file_paths);
    RUN_TEST(test_csv_rejects_broken_results);
    RUN_TEST(test_csv_reads_valid_results);
    RUN_TEST(test_csv_max_run_id);
    RUN_TEST(test_table_read);
    RUN_TEST(test_table_grows_past_the_old_cap);
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
