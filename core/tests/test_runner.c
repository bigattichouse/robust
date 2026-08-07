/*
 * test_runner.c — the fork/env run loop (core/src/runner.c).
 *
 * This file exists because `make coverage` reported runner.c at 0.00% of 65
 * lines: no suite anywhere called doe_run or doe_run_capture. It is the code
 * that executes a user's model -- fork, setenv per factor, execl /bin/sh,
 * waitpid -- so it was both the most security-sensitive file in the repo and
 * the only one with no coverage at all.
 *
 * The property that matters most is the one SECURITY.md claims and nothing
 * verified: factor values reach the script as environment *values*, set with
 * setenv, and are never spliced into the command string. A value containing
 * `; rm -rf`, `$(...)` or backticks must arrive as inert data.
 *
 * A NOTE ON THE COVERAGE NUMBER. This file's coverage used to read ~52%
 * because the forked child execs or _exit()s before gcov flushes, so its work
 * could not be attributed. That is fixed: runner.c calls __gcov_dump() in the
 * child before exec and before each _exit, under -DDOE_COVERAGE only, and the
 * figure is now 77%. What is still uncovered are the fork(), pipe() and exec
 * failure paths -- reachable only by exhausting process or descriptor limits --
 * and the final _exit statements, which run after the counters are written.
 */

#define _POSIX_C_SOURCE 200809L

#include "doe.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ---- scratch directory, per process ---------------------------------- */

static char TMPDIR[128];

static void tmpdir_setup(void) {
    snprintf(TMPDIR, sizeof TMPDIR, "build/test_runner_%ld", (long)getpid());
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", TMPDIR);
    if (system(cmd) != 0) { fprintf(stderr, "cannot create %s\n", TMPDIR); exit(2); }
}

static void tmpdir_teardown(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", TMPDIR);
    if (system(cmd) != 0) fprintf(stderr, "warning: could not remove %s\n", TMPDIR);
}

static void tmp_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", TMPDIR, name);
}

static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static int slurp(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) { if (n) out[0] = '\0'; return -1; }
    size_t got = fread(out, 1, n - 1, f);
    out[got] = '\0';
    fclose(f);
    return 0;
}

/* doe_run prints per-run exit codes to stdout; silence it so the suite's own
 * output stays readable, and restore afterwards. */
static int stdout_saved = -1;
static void stdout_mute(void) {
    fflush(stdout);
    stdout_saved = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); close(devnull); }
}
static int stderr_saved = -1;
static void stderr_mute(void) {
    fflush(stderr);
    stderr_saved = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
}
static void stderr_restore(void) {
    fflush(stderr);
    if (stderr_saved >= 0) { dup2(stderr_saved, STDERR_FILENO); close(stderr_saved); stderr_saved = -1; }
}

static void stdout_restore(void) {
    fflush(stdout);
    if (stdout_saved >= 0) { dup2(stdout_saved, STDOUT_FILENO); close(stdout_saved); stdout_saved = -1; }
}

/* ---- value callback --------------------------------------------------- */

#define MAXR 4
#define MAXC 4
static const char *g_vals[MAXR][MAXC];
static int g_return_null;

static const char *value_cb(void *ctx, size_t row, size_t col) {
    (void)ctx;
    if (g_return_null) return NULL;
    if (row >= MAXR || col >= MAXC) return "";
    return g_vals[row][col];
}

/* A space with `k` factors named x0..x{k-1}. Names only -- the runner never
 * looks at ranges, it just calls get_value. */
static void make_space(doe_space_t *sp, size_t k) {
    memset(sp, 0, sizeof *sp);
    sp->factor_count = k;
    for (size_t i = 0; i < k; i++)
        snprintf(sp->factors[i].name, DOE_MAX_NAME, "x%zu", i);
}

/* =======================================================================
 * doe_run — environment contract
 * ===================================================================== */

/* Every factor arrives as PREFIX_<name>, and PREFIX_RUN_ID is 1-based. */
static int test_run_exports_env(void) {
    doe_space_t sp; make_space(&sp, 2);
    g_return_null = 0;
    g_vals[0][0] = "alpha"; g_vals[0][1] = "11";
    g_vals[1][0] = "beta";  g_vals[1][1] = "22";

    char out[256], script[512], err[DOE_ERR_SIZE];
    tmp_path(out, sizeof out, "env");
    /* One file per run, named by RUN_ID, so we also prove RUN_ID reaches the
     * child and is distinct per row. */
    snprintf(script, sizeof script,
             "printf '%%s|%%s|%%s\\n' \"$TEST_RUN_ID\" \"$TEST_x0\" \"$TEST_x1\" "
             ">> %s", out);

    stdout_mute();
    int rc = doe_run(&sp, "TEST", script, 2, value_cb, NULL, err);
    stdout_restore();
    CHECK(rc == 0);

    char buf[512];
    CHECK(slurp(out, buf, sizeof buf) == 0);
    CHECK(strstr(buf, "1|alpha|11") != NULL);
    CHECK(strstr(buf, "2|beta|22")  != NULL);
    remove(out);
    return 1;
}

/*
 * THE SECURITY PROPERTY. A factor value full of shell metacharacters must
 * arrive as inert data. If the runner ever spliced values into the command
 * string, these would execute and the marker files would appear.
 */
static int test_values_are_data_not_code(void) {
    doe_space_t sp; make_space(&sp, 3);
    g_return_null = 0;

    char marker1[256], marker2[256], marker3[256];
    tmp_path(marker1, sizeof marker1, "pwned_semicolon");
    tmp_path(marker2, sizeof marker2, "pwned_subshell");
    tmp_path(marker3, sizeof marker3, "pwned_backtick");

    char v0[512], v1[512], v2[512];
    snprintf(v0, sizeof v0, "; touch %s", marker1);
    snprintf(v1, sizeof v1, "$(touch %s)", marker2);
    snprintf(v2, sizeof v2, "`touch %s`", marker3);
    g_vals[0][0] = v0; g_vals[0][1] = v1; g_vals[0][2] = v2;

    char out[256], script[512], err[DOE_ERR_SIZE];
    tmp_path(out, sizeof out, "hostile");
    snprintf(script, sizeof script,
             "printf '%%s\\n%%s\\n%%s\\n' \"$TEST_x0\" \"$TEST_x1\" \"$TEST_x2\" > %s", out);

    stdout_mute();
    int rc = doe_run(&sp, "TEST", script, 1, value_cb, NULL, err);
    stdout_restore();
    CHECK(rc == 0);

    /* Nothing executed. */
    CHECK(!file_exists(marker1));
    CHECK(!file_exists(marker2));
    CHECK(!file_exists(marker3));

    /* And the values arrived verbatim, not mangled or partially evaluated. */
    char buf[2048];
    CHECK(slurp(out, buf, sizeof buf) == 0);
    CHECK(strstr(buf, v0) != NULL);
    CHECK(strstr(buf, v1) != NULL);
    CHECK(strstr(buf, v2) != NULL);
    remove(out);
    return 1;
}

/* A NULL from the value callback becomes an empty string, not a crash. */
static int test_null_value_becomes_empty(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 1;

    char out[256], script[512], err[DOE_ERR_SIZE];
    tmp_path(out, sizeof out, "nullval");
    snprintf(script, sizeof script, "printf '[%%s]' \"$TEST_x0\" > %s", out);

    stdout_mute();
    int rc = doe_run(&sp, "TEST", script, 1, value_cb, NULL, err);
    stdout_restore();
    g_return_null = 0;
    CHECK(rc == 0);

    char buf[64];
    CHECK(slurp(out, buf, sizeof buf) == 0);
    CHECK(strcmp(buf, "[]") == 0);
    remove(out);
    return 1;
}

/* A failing script does not stop doe_run: it reports per-run status and
 * returns 0. (doe_run_capture is the one that treats non-zero as an error.) */
static int test_run_tolerates_failing_script(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0;
    g_vals[0][0] = "v"; g_vals[1][0] = "v";

    char err[DOE_ERR_SIZE];
    stdout_mute();
    int rc = doe_run(&sp, "TEST", "exit 3", 2, value_cb, NULL, err);
    stdout_restore();
    CHECK(rc == 0);
    return 1;
}

/* =======================================================================
 * doe_run_capture
 * ===================================================================== */

static int test_capture_reads_numeric_stdout(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0;
    g_vals[0][0] = "1"; g_vals[1][0] = "2"; g_vals[2][0] = "3";

    double resp[3] = {0};
    char err[DOE_ERR_SIZE];
    /* Each row echoes its own factor value, doubled, so a mixed-up row order
     * or a stale response would show. */
    CHECK(doe_run_capture(&sp, "TEST", "echo $((TEST_x0 * 2))", 3,
                          value_cb, NULL, resp, err) == 0);
    CHECK_DBL(resp[0], 2.0, 1e-12);
    CHECK_DBL(resp[1], 4.0, 1e-12);
    CHECK_DBL(resp[2], 6.0, 1e-12);
    return 1;
}

static int test_capture_accepts_float(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "x";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    CHECK(doe_run_capture(&sp, "TEST", "echo '   -3.5e2  '", 1,
                          value_cb, NULL, resp, err) == 0);
    CHECK_DBL(resp[0], -350.0, 1e-9);
    return 1;
}

static int test_capture_rejects_nonzero_exit(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "x";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    memset(err, 'A', sizeof err);
    /* Prints a perfectly good number, then fails: the exit status must win. */
    CHECK(doe_run_capture(&sp, "TEST", "echo 42; exit 1", 1,
                          value_cb, NULL, resp, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "non-zero") != NULL);
    return 1;
}

static int test_capture_rejects_non_numeric(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "x";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    memset(err, 'A', sizeof err);
    CHECK(doe_run_capture(&sp, "TEST", "echo not-a-number", 1,
                          value_cb, NULL, resp, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "numeric") != NULL);
    return 1;
}

static int test_capture_rejects_empty_output(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "x";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    CHECK(doe_run_capture(&sp, "TEST", "true", 1, value_cb, NULL, resp, err) != 0);
    CHECK(strstr(err, "numeric") != NULL);
    return 1;
}

/*
 * The child's stdout is read into a 256-byte buffer. A script that prints far
 * more must not overflow it, and the leading number must still parse -- the
 * drain loop exists so the child is never blocked on a full pipe, which would
 * deadlock the wait.
 */
static int test_capture_big_output(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "x";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    CHECK(doe_run_capture(&sp, "TEST",
                          "echo 7; head -c 100000 /dev/zero | tr '\\0' 'z'", 1,
                          value_cb, NULL, resp, err) == 0);
    CHECK_DBL(resp[0], 7.0, 1e-12);
    return 1;
}

/* Values reach the capture path as data too, not only the doe_run path. */
static int test_capture_values_are_data(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0;

    char marker[256];
    tmp_path(marker, sizeof marker, "pwned_capture");
    char v[512];
    snprintf(v, sizeof v, "; touch %s", marker);
    g_vals[0][0] = v;

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    CHECK(doe_run_capture(&sp, "TEST", "echo 1", 1, value_cb, NULL, resp, err) == 0);
    CHECK(!file_exists(marker));
    return 1;
}

/*
 * A child killed by a signal is not a normal exit. doe_run reports it and
 * carries on; doe_run_capture must treat it as a failure rather than parsing
 * whatever happened to be in the pipe.
 */
static int test_signal_killed_child(void) {
    doe_space_t sp; make_space(&sp, 1);
    g_return_null = 0; g_vals[0][0] = "v";

    char err[DOE_ERR_SIZE];
    stdout_mute();
    int rc = doe_run(&sp, "TEST", "kill -9 $$", 1, value_cb, NULL, err);
    stdout_restore();
    CHECK(rc == 0);          /* doe_run reports, does not fail */

    /* Even with a valid number already printed, a signal death must not be
     * accepted as a response. */
    double resp[1] = {-1.0};
    memset(err, 'A', sizeof err);
    CHECK(doe_run_capture(&sp, "TEST", "echo 5; kill -9 $$", 1,
                          value_cb, NULL, resp, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    return 1;
}

/* Zero rows is a no-op, not a crash. */
static int test_zero_rows(void) {
    doe_space_t sp; make_space(&sp, 1);
    char err[DOE_ERR_SIZE];
    stdout_mute();
    int rc = doe_run(&sp, "TEST", "true", 0, value_cb, NULL, err);
    stdout_restore();
    CHECK(rc == 0);
    CHECK(doe_run_capture(&sp, "TEST", "true", 0, value_cb, NULL, NULL, err) == 0);
    return 1;
}

/* A factor name containing '=' would produce a malformed env entry, so the
 * child refuses. doe_run_capture surfaces that as a non-zero child exit. */
static int test_factor_name_equals_rejected(void) {
    doe_space_t sp; make_space(&sp, 1);
    snprintf(sp.factors[0].name, DOE_MAX_NAME, "bad=name");
    g_return_null = 0; g_vals[0][0] = "v";

    double resp[1] = {0};
    char err[DOE_ERR_SIZE];
    /* The child prints its complaint to stderr; mute it so the suite output
     * stays readable. The assertion is the return code. */
    stderr_mute();
    int rc = doe_run_capture(&sp, "TEST", "echo 1", 1, value_cb, NULL, resp, err);
    stderr_restore();
    CHECK(rc != 0);
    return 1;
}

int main(void) {
    printf("runner tests\n");
    tmpdir_setup();
    RUN_TEST(test_run_exports_env);
    RUN_TEST(test_values_are_data_not_code);
    RUN_TEST(test_null_value_becomes_empty);
    RUN_TEST(test_run_tolerates_failing_script);
    RUN_TEST(test_capture_reads_numeric_stdout);
    RUN_TEST(test_capture_accepts_float);
    RUN_TEST(test_capture_rejects_nonzero_exit);
    RUN_TEST(test_capture_rejects_non_numeric);
    RUN_TEST(test_capture_rejects_empty_output);
    RUN_TEST(test_capture_big_output);
    RUN_TEST(test_capture_values_are_data);
    RUN_TEST(test_signal_killed_child);
    RUN_TEST(test_zero_rows);
    RUN_TEST(test_factor_name_equals_rejected);
    tmpdir_teardown();
    return TEST_SUMMARY();
}
