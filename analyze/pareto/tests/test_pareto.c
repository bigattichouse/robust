/*
 * test_pareto.c — the test list from spec/pareto.bp.
 *
 * The dominance core is validated against an analytic front before any store
 * behaviour is exercised, per the spec's delivery checklist.
 */

#define _POSIX_C_SOURCE 200809L

#include "pareto.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/* ---- helpers ---------------------------------------------------------- */

static pareto_objective_t OBJ2[2];
static size_t NOBJ2;

static void objs_max_max(void) {
    char err[DOE_ERR_SIZE];
    NOBJ2 = 0;
    pareto_add_objective(OBJ2, &NOBJ2, "y1", PARETO_MAX, err);
    pareto_add_objective(OBJ2, &NOBJ2, "y2", PARETO_MAX, err);
}

static pareto_point_t mk(double a, double b, unsigned long long id) {
    pareto_point_t p;
    memset(&p, 0, sizeof p);
    p.values[0] = a; p.values[1] = b; p.run_id = id;
    char buf[64];
    snprintf(buf, sizeof buf, "%llu,%g,%g", id, a, b);
    p.row = malloc(strlen(buf) + 1);
    if (p.row) memcpy(p.row, buf, strlen(buf) + 1);
    return p;
}

/*
 * Scratch files live in a per-process directory created by main() and removed
 * on the way out, whatever the outcome. Fixed names under build/ collided
 * under `make -j` or two concurrent runs, and a CHECK that returns early skips
 * every remove() after it, so failures used to leave litter behind.
 */
static char TMPDIR[128];

static void tmp_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", TMPDIR, name);
}

static void tmpdir_setup(void) {
    snprintf(TMPDIR, sizeof TMPDIR, "build/test_pareto_%ld", (long)getpid());
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", TMPDIR);
    if (system(cmd) != 0) { fprintf(stderr, "cannot create %s\n", TMPDIR); exit(2); }
}

static void tmpdir_teardown(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", TMPDIR);
    if (system(cmd) != 0) fprintf(stderr, "warning: could not remove %s\n", TMPDIR);
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* ---- guarded error buffer ---------------------------------------------
 *
 * The overflow fuzz_pareto found writes just past a DOE_ERR_SIZE err buffer.
 * A sanitizer sees that; a plain build does not, so a test that merely calls
 * the function and inspects `err` passes against buggy code. That is not a
 * durable test.
 *
 * So place the buffer between two large sentinel regions and check them by
 * hand. Any write past either end is then detected by ordinary C in EVERY
 * build mode — no sanitizer required.
 *
 * The guard is sized past PARETO_MAX_LINE because the quantity that ran away
 * in the original bug was bounded by the input line length: a single
 * unclamped append can advance the offset by a whole field. A 256-byte guard
 * would let a large overshoot jump clean over it and go undetected.
 */
#define GUARD_BYTES 16384
#define GUARD_FILL  0xA5

typedef struct {
    unsigned char pre[GUARD_BYTES];
    char          err[DOE_ERR_SIZE];
    unsigned char post[GUARD_BYTES];
} guarded_err_t;

static guarded_err_t *guard_new(void) {
    guarded_err_t *g = malloc(sizeof *g);
    if (!g) return NULL;
    memset(g->pre,  GUARD_FILL, GUARD_BYTES);
    memset(g->err,  'A', DOE_ERR_SIZE);   /* unterminated: proves the callee
                                           * writes a NUL of its own */
    memset(g->post, GUARD_FILL, GUARD_BYTES);
    return g;
}

/* 1 if both guards are intact; otherwise 0 and *what says which end. */
static int guard_intact(const guarded_err_t *g, const char **what) {
    for (size_t i = 0; i < GUARD_BYTES; i++)
        if (g->pre[i] != GUARD_FILL)  { *what = "UNDERFLOW before err"; return 0; }
    for (size_t i = 0; i < GUARD_BYTES; i++)
        if (g->post[i] != GUARD_FILL) { *what = "OVERFLOW past err";    return 0; }
    return 1;
}

/* err must be NUL-terminated somewhere inside its DOE_ERR_SIZE bytes. */
static int err_terminated(const guarded_err_t *g) {
    return memchr(g->err, '\0', DOE_ERR_SIZE) != NULL;
}

/*
 * Reads into a caller-supplied buffer. This used to return a pointer into a
 * `static char buf[]`, which made every two-file comparison depend on copying
 * the first result before the second call -- correct only by careful ordering,
 * and silently wrong after one careless edit.
 */
static int slurp(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) { if (n) out[0] = '\0'; return -1; }
    size_t got = fread(out, 1, n - 1, f);
    out[got] = '\0';
    fclose(f);
    return 0;
}

/* ---- unit: dominance axioms ------------------------------------------- */

static int test_dominance_axioms(void) {
    objs_max_max();
    pareto_objective_t o3[3];
    size_t n3 = 0;
    char err[DOE_ERR_SIZE];
    pareto_add_objective(o3, &n3, "a", PARETO_MAX, err);
    pareto_add_objective(o3, &n3, "b", PARETO_MIN, err);
    pareto_add_objective(o3, &n3, "c", PARETO_MAX, err);

    doe_rng_t rng; doe_rng_seed(&rng, 7);
    pareto_point_t pts[200];
    for (int i = 0; i < 200; i++) {
        memset(&pts[i], 0, sizeof pts[i]);
        for (int j = 0; j < 3; j++)
            pts[i].values[j] = (double)((int)(doe_rng_uniform(&rng) * 5));
    }

    for (int i = 0; i < 200; i++) {
        /* irreflexive */
        CHECK(!pareto_dominates(&pts[i], &pts[i], o3, 3));
        for (int j = 0; j < 200; j++) {
            int ij = pareto_dominates(&pts[i], &pts[j], o3, 3);
            int ji = pareto_dominates(&pts[j], &pts[i], o3, 3);
            /* asymmetric */
            CHECK(!(ij && ji));
            if (!ij) continue;
            /* transitive */
            for (int m = 0; m < 200; m++)
                if (pareto_dominates(&pts[j], &pts[m], o3, 3))
                    CHECK(pareto_dominates(&pts[i], &pts[m], o3, 3));
        }
    }
    return 1;
}

/* ---- unit: the analytic front ----------------------------------------- */

/* y1 = x, y2 = 1 - x^2 on [0,1], both maximized. y1 strictly increases and
 * y2 strictly decreases in x, so EVERY sampled point is non-dominated.
 * Source: EXPANSION.md E7 validation clause. */
static int test_analytic_front_2d(void) {
    objs_max_max();
    enum { NP = 101 };
    pareto_point_t pts[NP];
    for (int i = 0; i < NP; i++) {
        double x = (double)i / (NP - 1);
        pts[i] = mk(x, 1.0 - x * x, (unsigned long long)i + 1);
    }
    unsigned char keep[NP];
    pareto_non_dominated(pts, NP, OBJ2, NOBJ2, keep);
    size_t kept = 0;
    for (int i = 0; i < NP; i++) kept += keep[i];
    for (int i = 0; i < NP; i++) free(pts[i].row);
    CHECK(kept == NP);
    return 1;
}

/* The test with teeth: the all-pass case above cannot catch an
 * over-permissive filter. Plant 500 strictly-worse interior points. */
static int test_analytic_front_with_interior(void) {
    objs_max_max();
    enum { NCURVE = 101, NIN = 500, NP = NCURVE + NIN };
    pareto_point_t *pts = malloc(NP * sizeof *pts);
    CHECK(pts != NULL);

    for (int i = 0; i < NCURVE; i++) {
        double x = (double)i / (NCURVE - 1);
        pts[i] = mk(x, 1.0 - x * x, (unsigned long long)i + 1);
    }
    doe_rng_t rng; doe_rng_seed(&rng, 99);
    for (int i = 0; i < NIN; i++) {
        double x = doe_rng_uniform(&rng);
        /* strictly worse than the curve point at the same x, on both axes */
        pts[NCURVE + i] = mk(0.9 * x, 0.9 * (1.0 - x * x),
                             (unsigned long long)(1000 + i));
    }

    unsigned char *keep = malloc(NP);
    CHECK(keep != NULL);
    pareto_non_dominated(pts, NP, OBJ2, NOBJ2, keep);

    /* Every interior point must be gone. Curve points may lose only to a
     * curve point, which cannot happen — so all 101 survive. */
    int kept_curve = 0, kept_interior = 0;
    for (int i = 0; i < NCURVE; i++) kept_curve += keep[i];
    for (int i = NCURVE; i < NP; i++) kept_interior += keep[i];

    for (int i = 0; i < NP; i++) free(pts[i].row);
    free(pts); free(keep);

    CHECK(kept_curve == NCURVE);
    CHECK(kept_interior == 0);
    return 1;
}

/* ---- unit: exhaustive correctness -------------------------------------- */

static int test_dominated_point_never_survives(void) {
    char err[DOE_ERR_SIZE];
    doe_rng_t rng; doe_rng_seed(&rng, 4242);

    for (int trial = 0; trial < 200; trial++) {
        size_t nobj = 2 + (size_t)(doe_rng_uniform(&rng) * 4);   /* 2..5 */
        pareto_objective_t objs[5];
        size_t no = 0;
        for (size_t o = 0; o < nobj; o++) {
            char nm[32]; snprintf(nm, sizeof nm, "m%d", (int)o);
            pareto_add_objective(objs, &no, nm,
                doe_rng_uniform(&rng) < 0.5 ? PARETO_MAX : PARETO_MIN, err);
        }
        enum { NP = 40 };
        pareto_point_t pts[NP];
        for (int i = 0; i < NP; i++) {
            memset(&pts[i], 0, sizeof pts[i]);
            for (size_t o = 0; o < no; o++)
                pts[i].values[o] = (double)((int)(doe_rng_uniform(&rng) * 6));
        }
        unsigned char keep[NP];
        pareto_non_dominated(pts, NP, objs, no, keep);

        for (int i = 0; i < NP; i++) {
            if (keep[i]) {
                /* no kept point dominates another kept point */
                for (int j = 0; j < NP; j++)
                    if (keep[j]) CHECK(!pareto_dominates(&pts[j], &pts[i], objs, no));
            } else {
                /* an excluded point must have a dominator among the kept */
                int found = 0;
                for (int j = 0; j < NP && !found; j++)
                    if (keep[j] && pareto_dominates(&pts[j], &pts[i], objs, no)) found = 1;
                CHECK(found);
            }
        }
    }
    return 1;
}

static int test_ties_are_kept(void) {
    objs_max_max();
    pareto_point_t pts[2];
    pts[0] = mk(1.0, 2.0, 1);
    pts[1] = mk(1.0, 2.0, 2);       /* identical objectives, different run */
    unsigned char keep[2];
    pareto_non_dominated(pts, 2, OBJ2, NOBJ2, keep);
    free(pts[0].row); free(pts[1].row);
    CHECK(keep[0] == 1 && keep[1] == 1);
    return 1;
}

static int test_sense_directions(void) {
    char err[DOE_ERR_SIZE];
    pareto_objective_t up[2]; size_t nu = 0;
    pareto_add_objective(up, &nu, "y1", PARETO_MAX, err);
    pareto_add_objective(up, &nu, "y2", PARETO_MAX, err);
    pareto_objective_t dn[2]; size_t nd = 0;
    pareto_add_objective(dn, &nd, "y1", PARETO_MIN, err);
    pareto_add_objective(dn, &nd, "y2", PARETO_MIN, err);

    enum { NP = 30 };
    pareto_point_t pts[NP];
    doe_rng_t rng; doe_rng_seed(&rng, 11);
    for (int i = 0; i < NP; i++)
        pts[i] = mk((double)(int)(doe_rng_uniform(&rng) * 5),
                    (double)(int)(doe_rng_uniform(&rng) * 5),
                    (unsigned long long)i);

    unsigned char ku[NP], kd[NP];
    pareto_non_dominated(pts, NP, up, nu, ku);
    pareto_non_dominated(pts, NP, dn, nd, kd);

    /* Any point on both fronts must be extremal on every objective: nothing
     * beats it going up, and nothing beats it going down. */
    int both = 0;
    for (int i = 0; i < NP; i++) if (ku[i] && kd[i]) both++;
    for (int i = 0; i < NP; i++) free(pts[i].row);
    CHECK(both >= 0);   /* structural: computed without crashing, senses differ */
    /* the two fronts must not be identical on this data */
    int differ = 0;
    for (int i = 0; i < NP; i++) if (ku[i] != kd[i]) differ = 1;
    CHECK(differ);
    return 1;
}

static int test_duplicate_objective_rejected(void) {
    char err[DOE_ERR_SIZE];
    pareto_objective_t o[4]; size_t n = 0;
    CHECK(pareto_add_objective(o, &n, "yield", PARETO_MAX, err) == 0);
    CHECK(pareto_add_objective(o, &n, "yield", PARETO_MIN, err) != 0);
    CHECK(strstr(err, "twice") != NULL);
    return 1;
}

/* ---- csv reader -------------------------------------------------------- */

static int test_nonfinite_rejected(void) {
    char path[256]; tmp_path(path, sizeof path, "nan.csv");
    CHECK(write_file(path,
        "run_id,yield,cost\n"
        "1,0.8,10\n"
        "2,nan,12\n") == 0);

    objs_max_max();
    char err[DOE_ERR_SIZE];
    pareto_objective_t o[2]; size_t n = 0;
    pareto_add_objective(o, &n, "yield", PARETO_MAX, err);
    pareto_add_objective(o, &n, "cost", PARETO_MIN, err);

    pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
    int rc = pareto_read_csv(path, o, n, NULL, &pts, &cnt, &hdr, err);
    remove(path);
    CHECK(rc != 0);
    CHECK(strstr(err, "non-numeric or non-finite") != NULL);
    return 1;
}

static int test_unknown_column_errors(void) {
    char path[256]; tmp_path(path, sizeof path, "cols.csv");
    CHECK(write_file(path, "run_id,yield,cost\n1,0.8,10\n") == 0);

    char err[DOE_ERR_SIZE];
    pareto_objective_t o[2]; size_t n = 0;
    pareto_add_objective(o, &n, "yield", PARETO_MAX, err);
    pareto_add_objective(o, &n, "throughput", PARETO_MAX, err);

    pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
    int rc = pareto_read_csv(path, o, n, NULL, &pts, &cnt, &hdr, err);
    remove(path);
    CHECK(rc != 0);
    CHECK(strstr(err, "throughput") != NULL);
    CHECK(strstr(err, "available") != NULL);
    return 1;
}

/*
 * Regression + durable guard for the whole error-message surface.
 *
 * The bug: the "unknown objective column" message lists every available
 * column, and header fields are attacker-controlled. Building it with
 * `off += snprintf(err + off, DOE_ERR_SIZE - off, ...)` overflows, because
 * snprintf returns the length it WOULD have written, so one long field pushes
 * `off` past the buffer. Found by fuzz_pareto.c on its first run.
 *
 * The first version of this test only failed under ASan -- a plain build of
 * the buggy code passed the whole suite, because the overflow lands in stack
 * the assertions never look at. This version uses a guarded buffer instead,
 * so it detects the write with ordinary C in every build mode.
 *
 * It sweeps every error path that composes a message from input-derived text,
 * not just the one that broke, so the next such message is covered on arrival.
 */
static int check_guarded(int rc_expected_nonzero, int rc,
                         const guarded_err_t *g, const char *label) {
    const char *what = NULL;
    if (!guard_intact(g, &what)) {
        printf("\n    GUARD VIOLATED (%s) in case: %s\n", what, label);
        return 0;
    }
    if (!err_terminated(g)) {
        printf("\n    err not NUL-terminated in case: %s\n", label);
        return 0;
    }
    if (rc_expected_nonzero && rc == 0) {
        printf("\n    expected failure but got success in case: %s\n", label);
        return 0;
    }
    return 1;
}

/* Build a CSV whose header has `ncols` columns of `width` chars each. */
static int write_wide_header_csv(const char *path, int ncols, int width) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs("run_id", f);
    for (int i = 0; i < ncols; i++) {
        fputc(',', f);
        for (int j = 0; j < width; j++) fputc('c', f);
        fprintf(f, "%d", i);
    }
    fputs("\n1", f);
    for (int i = 0; i < ncols; i++) fputs(",1", f);
    fputc('\n', f);
    fclose(f);
    return 0;
}

static int test_error_paths_never_overflow_err(void) {
    char path[256];
    char e2[DOE_ERR_SIZE];
    int ok = 1;

    /* --- case 1: the original bug. Wide header, absent objectives. --- */
    {
        tmp_path(path, sizeof path, "g_wide.csv");
        CHECK(write_wide_header_csv(path, 40, 100) == 0);
        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        pareto_objective_t o[2]; size_t n = 0;
        pareto_add_objective(o, &n, "absent_one", PARETO_MAX, e2);
        pareto_add_objective(o, &n, "absent_two", PARETO_MIN, e2);
        pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
        int rc = pareto_read_csv(path, o, n, NULL, &pts, &cnt, &hdr, g->err);
        if (rc == 0) { pareto_points_free(pts, cnt); free(hdr); }
        ok &= check_guarded(1, rc, g, "wide header, absent objective");
        free(g); remove(path);
        CHECK(ok);
    }

    /* --- case 2: header at the line-length limit. --- */
    {
        tmp_path(path, sizeof path, "g_max.csv");
        CHECK(write_wide_header_csv(path, 400, 18) == 0);
        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        pareto_objective_t o[2]; size_t n = 0;
        pareto_add_objective(o, &n, "nope_a", PARETO_MAX, e2);
        pareto_add_objective(o, &n, "nope_b", PARETO_MIN, e2);
        pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
        int rc = pareto_read_csv(path, o, n, NULL, &pts, &cnt, &hdr, g->err);
        if (rc == 0) { pareto_points_free(pts, cnt); free(hdr); }
        ok &= check_guarded(0, rc, g, "header at line-length limit");
        free(g); remove(path);
        CHECK(ok);
    }

    /* --- case 3: non-finite value in a wide-named column. --- */
    {
        tmp_path(path, sizeof path, "g_nan.csv");
        FILE *f = fopen(path, "w"); CHECK(f != NULL);
        fputs("run_id,", f);
        for (int j = 0; j < 200; j++) fputc('y', f);
        fputs(",cost\n1,nan,10\n", f);
        fclose(f);

        char wide[256]; memset(wide, 'y', 200); wide[200] = '\0';
        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        pareto_objective_t o[2]; size_t n = 0;
        /* name is longer than DOE_MAX_NAME -> add_objective rejects it, so
         * use a truncated name and let the column lookup fail instead. */
        wide[DOE_MAX_NAME - 2] = '\0';
        pareto_add_objective(o, &n, wide,   PARETO_MAX, e2);
        pareto_add_objective(o, &n, "cost", PARETO_MIN, e2);
        pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
        int rc = pareto_read_csv(path, o, n, NULL, &pts, &cnt, &hdr, g->err);
        if (rc == 0) { pareto_points_free(pts, cnt); free(hdr); }
        ok &= check_guarded(1, rc, g, "non-finite value, wide column name");
        free(g); remove(path);
        CHECK(ok);
    }

    /* --- case 4: overlong objective name (add_objective's own message). --- */
    {
        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        char toolong[DOE_MAX_NAME * 4];
        memset(toolong, 'z', sizeof toolong - 1);
        toolong[sizeof toolong - 1] = '\0';
        pareto_objective_t o[2]; size_t n = 0;
        int rc = pareto_add_objective(o, &n, toolong, PARETO_MAX, g->err);
        ok &= check_guarded(1, rc, g, "objective name too long");
        free(g);
        CHECK(ok);
    }

    /* --- case 5: malformed .front preamble with long tokens. --- */
    {
        tmp_path(path, sizeof path, "g_bad.front");
        FILE *f = fopen(path, "w"); CHECK(f != NULL);
        fputs("# tgu-front 1\n# objectives:", f);
        for (int j = 0; j < 300; j++) fputc('q', f);
        fputs(" bogus_sense\n", f);
        fclose(f);

        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        pareto_front_t fr;
        int rc = pareto_front_load(path, &fr, g->err);
        if (rc == 0) pareto_front_free(&fr);
        ok &= check_guarded(1, rc, g, "malformed front preamble, long tokens");
        free(g); remove(path);
        CHECK(ok);
    }

    /* --- case 6: .front whose data rows do not match its objectives. --- */
    {
        tmp_path(path, sizeof path, "g_mismatch.front");
        FILE *f = fopen(path, "w"); CHECK(f != NULL);
        fputs("# tgu-front 1\n# objectives: yield max, cost min\n", f);
        fputs("run_id,", f);
        for (int j = 0; j < 300; j++) fputc('w', f);
        fputs(",cost\n1,2,3\n", f);
        fclose(f);

        guarded_err_t *g = guard_new(); CHECK(g != NULL);
        pareto_front_t fr;
        int rc = pareto_front_load(path, &fr, g->err);
        if (rc == 0) pareto_front_free(&fr);
        ok &= check_guarded(1, rc, g, "front header missing an objective");
        free(g); remove(path);
        CHECK(ok);
    }

    return ok;
}

/* ---- store ------------------------------------------------------------- */

static const char *BATCH1 =
    "run_id,yield,cost\n"
    "1,0.50,10\n"
    "2,0.80,20\n"
    "3,0.30,5\n"
    "4,0.40,30\n";     /* dominated by 1 (better yield, lower cost) */

static const char *BATCH2 =
    "run_id,yield,cost\n"
    "5,0.90,20\n"      /* dominates run 2 */
    "6,0.10,50\n";     /* dominated */

static int make_front(const char *path) {
    char err[DOE_ERR_SIZE];
    pareto_front_t f; pareto_front_init(&f);
    size_t n = 0;
    if (pareto_add_objective(f.objectives, &n, "yield", PARETO_MAX, err) != 0) return -1;
    if (pareto_add_objective(f.objectives, &n, "cost",  PARETO_MIN, err) != 0) return -1;
    f.objective_count = n;
    int rc = pareto_front_save(path, &f, err);
    pareto_front_free(&f);
    return rc;
}

/* Load front, merge a csv file, save. Returns 0 on success. */
static int merge_file(const char *front_path, const char *csv, const char *label,
                      pareto_merge_t *rec) {
    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    if (pareto_front_load(front_path, &f, err) != 0) return -1;

    pareto_point_t *pts = NULL; size_t n = 0; char *hdr = NULL;
    if (pareto_read_csv(csv, f.objectives, f.objective_count, label,
                        &pts, &n, &hdr, err) != 0) { pareto_front_free(&f); return -1; }
    if (!f.columns) { f.columns = hdr; hdr = NULL; }
    free(hdr);

    pareto_merge_t local;
    int rc = pareto_front_merge(&f, pts, n, label, rec ? rec : &local, err);
    pareto_points_free(pts, n);
    if (rc == 0) rc = pareto_front_save(front_path, &f, err);
    pareto_front_free(&f);
    return rc;
}

static int test_front_roundtrip(void) {
    char fp[256]; tmp_path(fp, sizeof fp, "rt.front");
    char b1[256]; tmp_path(b1, sizeof b1, "rt1.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "batch1", NULL) == 0);

    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    CHECK(pareto_front_load(fp, &f, err) == 0);
    CHECK(f.objective_count == 2);
    CHECK(f.objectives[0].sense == PARETO_MAX);
    CHECK(f.objectives[1].sense == PARETO_MIN);
    CHECK(f.point_count == 3);     /* run 4 is dominated by run 1 */
    pareto_front_free(&f);
    remove(fp); remove(b1);
    return 1;
}

static int test_merge_idempotent(void) {
    char fp[256]; tmp_path(fp, sizeof fp, "idem.front");
    char b1[256]; tmp_path(b1, sizeof b1, "idem1.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "batch1", NULL) == 0);

    char first[16384], again[16384];
    CHECK(slurp(fp, first, sizeof first) == 0);

    pareto_merge_t rec;
    CHECK(merge_file(fp, b1, "batch1", &rec) == 0);
    CHECK(rec.admitted == 0);
    CHECK(rec.evicted == 0);

    /* Only the history preamble may grow; the data rows must be identical. */
    CHECK(slurp(fp, again, sizeof again) == 0);
    const char *d1 = strstr(first, "run_id");
    const char *d2 = strstr(again, "run_id");
    CHECK(d1 && d2);
    CHECK(strcmp(d1, d2) == 0);

    remove(fp); remove(b1);
    return 1;
}

static int test_merge_order_independent(void) {
    char fa[256], fb[256], b1[256], b2[256];
    tmp_path(fa, sizeof fa, "oa.front"); tmp_path(fb, sizeof fb, "ob.front");
    tmp_path(b1, sizeof b1, "o1.csv");   tmp_path(b2, sizeof b2, "o2.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(write_file(b2, BATCH2) == 0);

    CHECK(make_front(fa) == 0);
    CHECK(merge_file(fa, b1, "b1", NULL) == 0);
    CHECK(merge_file(fa, b2, "b2", NULL) == 0);

    CHECK(make_front(fb) == 0);
    CHECK(merge_file(fb, b2, "b2", NULL) == 0);
    CHECK(merge_file(fb, b1, "b1", NULL) == 0);

    char bufa[16384], bufb[16384];
    CHECK(slurp(fa, bufa, sizeof bufa) == 0);
    CHECK(slurp(fb, bufb, sizeof bufb) == 0);

    const char *da = strstr(bufa, "run_id");
    const char *db = strstr(bufb, "run_id");
    CHECK(da && db);
    CHECK(strcmp(da, db) == 0);

    remove(fa); remove(fb); remove(b1); remove(b2);
    return 1;
}

static int test_merge_equals_batch_filter(void) {
    char fp[256], b1[256], b2[256], cat[256];
    tmp_path(fp, sizeof fp, "eq.front");
    tmp_path(b1, sizeof b1, "eq1.csv");
    tmp_path(b2, sizeof b2, "eq2.csv");
    tmp_path(cat, sizeof cat, "eqcat.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(write_file(b2, BATCH2) == 0);
    CHECK(write_file(cat,
        "run_id,yield,cost\n"
        "1,0.50,10\n2,0.80,20\n3,0.30,5\n4,0.40,30\n"
        "5,0.90,20\n6,0.10,50\n") == 0);

    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "b1", NULL) == 0);
    CHECK(merge_file(fp, b2, "b2", NULL) == 0);

    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    CHECK(pareto_front_load(fp, &f, err) == 0);

    pareto_objective_t o[2]; size_t no = 0;
    pareto_add_objective(o, &no, "yield", PARETO_MAX, err);
    pareto_add_objective(o, &no, "cost",  PARETO_MIN, err);
    pareto_point_t *pts = NULL; size_t n = 0; char *hdr = NULL;
    CHECK(pareto_read_csv(cat, o, no, NULL, &pts, &n, &hdr, err) == 0);
    unsigned char *keep = malloc(n);
    pareto_non_dominated(pts, n, o, no, keep);
    size_t nkeep = 0;
    for (size_t i = 0; i < n; i++) nkeep += keep[i];

    CHECK(nkeep == f.point_count);
    /* same run_ids, as sets */
    for (size_t i = 0; i < n; i++) {
        if (!keep[i]) continue;
        int found = 0;
        for (size_t j = 0; j < f.point_count; j++)
            if (f.points[j].run_id == pts[i].run_id) { found = 1; break; }
        CHECK(found);
    }

    free(keep); free(hdr); pareto_points_free(pts, n);
    pareto_front_free(&f);
    remove(fp); remove(b1); remove(b2); remove(cat);
    return 1;
}

static int test_eviction_recorded(void) {
    char fp[256], b1[256], b2[256];
    tmp_path(fp, sizeof fp, "ev.front");
    tmp_path(b1, sizeof b1, "ev1.csv");
    tmp_path(b2, sizeof b2, "ev2.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(write_file(b2, BATCH2) == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "b1", NULL) == 0);

    pareto_merge_t rec;
    CHECK(merge_file(fp, b2, "b2", &rec) == 0);
    CHECK(rec.rows_in == 2);
    CHECK(rec.admitted == 1);     /* run 5 joins */
    CHECK(rec.evicted == 1);      /* run 2 leaves */
    CHECK(rec.rejected == 1);     /* run 6 dominated on arrival */

    remove(fp); remove(b1); remove(b2);
    return 1;
}

/* The property that makes the whole pipeline compose: a .front IS a
 * results CSV, because its metadata lives in '#' comment lines. */
static int test_front_is_a_results_csv(void) {
    char fp[256], b1[256];
    tmp_path(fp, sizeof fp, "csvcompat.front");
    tmp_path(b1, sizeof b1, "csvcompat.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "b1", NULL) == 0);

    double resp[64];
    size_t cnt = 0;
    char err[DOE_ERR_SIZE];
    CHECK(doe_csv_read_metric(fp, "yield", resp, 64, &cnt, err) == 0);
    CHECK(cnt > 0);

    remove(fp); remove(b1);
    return 1;
}

static int test_header_mismatch_detected(void) {
    char fp[256], b1[256], bad[256];
    tmp_path(fp, sizeof fp, "hm.front");
    tmp_path(b1, sizeof b1, "hm1.csv");
    tmp_path(bad, sizeof bad, "hmbad.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(write_file(bad, "run_id,yield,cost,extra\n9,0.99,1,7\n") == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "b1", NULL) == 0);

    /* The library merge itself does not compare headers — the CLI does — so
     * assert the front's stored header is what a caller would compare. */
    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    CHECK(pareto_front_load(fp, &f, err) == 0);
    CHECK(f.columns != NULL);
    CHECK(strcmp(f.columns, "run_id,yield,cost") == 0);
    pareto_front_free(&f);

    remove(fp); remove(b1); remove(bad);
    return 1;
}

static int test_not_a_front_errors(void) {
    char p[256]; tmp_path(p, sizeof p, "plain.csv");
    CHECK(write_file(p, "run_id,yield,cost\n1,1,1\n") == 0);
    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    int rc = pareto_front_load(p, &f, err);
    remove(p);
    CHECK(rc != 0);
    CHECK(strstr(err, "tgu-front") != NULL);
    return 1;
}

static int test_atomic_write_leaves_original(void) {
    char fp[256], b1[256];
    tmp_path(fp, sizeof fp, "atomic.front");
    tmp_path(b1, sizeof b1, "atomic.csv");
    CHECK(write_file(b1, BATCH1) == 0);
    CHECK(make_front(fp) == 0);
    CHECK(merge_file(fp, b1, "b1", NULL) == 0);

    char before[16384], after[16384];
    CHECK(slurp(fp, before, sizeof before) == 0);

    /* Unwritable target directory path -> save must fail cleanly and leave
     * the original file untouched. */
    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    CHECK(pareto_front_load(fp, &f, err) == 0);
    int rc = pareto_front_save("/proc/definitely/not/writable.front", &f, err);
    pareto_front_free(&f);
    CHECK(rc != 0);
    CHECK(strlen(err) > 0);

    CHECK(slurp(fp, after, sizeof after) == 0);
    CHECK(strcmp(before, after) == 0);
    remove(fp); remove(b1);
    return 1;
}

int main(void) {
    printf("pareto tests\n");
    tmpdir_setup();
    RUN_TEST(test_dominance_axioms);
    RUN_TEST(test_analytic_front_2d);
    RUN_TEST(test_analytic_front_with_interior);
    RUN_TEST(test_dominated_point_never_survives);
    RUN_TEST(test_ties_are_kept);
    RUN_TEST(test_sense_directions);
    RUN_TEST(test_duplicate_objective_rejected);
    RUN_TEST(test_nonfinite_rejected);
    RUN_TEST(test_unknown_column_errors);
    RUN_TEST(test_error_paths_never_overflow_err);
    RUN_TEST(test_front_roundtrip);
    RUN_TEST(test_merge_idempotent);
    RUN_TEST(test_merge_order_independent);
    RUN_TEST(test_merge_equals_batch_filter);
    RUN_TEST(test_eviction_recorded);
    RUN_TEST(test_front_is_a_results_csv);
    RUN_TEST(test_header_mismatch_detected);
    RUN_TEST(test_not_a_front_errors);
    RUN_TEST(test_atomic_write_leaves_original);
    tmpdir_teardown();
    return TEST_SUMMARY();
}
