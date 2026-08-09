/*
 * test_sobol.c — validates the Sobol estimators against functions with known
 * analytic indices: an additive linear function (tight) and the Ishigami
 * function (the standard Sobol benchmark; x3 matters only via interaction).
 */

#include "sobol.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const double PI = 3.14159265358979323846;

/* Evaluate f over the scaled design points. Returns responses + design. */
static double *eval_design(const doe_space_t *sp, sobol_design_t *d,
                           double (*f)(const double *x, size_t k)) {
    char err[DOE_ERR_SIZE];
    if (sobol_design_build(sp, d, err) != 0) return NULL;
    double *y = malloc(d->npoints * sizeof *y);
    if (!y) return NULL;
    double u[DOE_MAX_FACTORS], x[DOE_MAX_FACTORS];
    for (size_t i = 0; i < d->npoints; i++) {
        sobol_point(d, i, u);
        for (size_t j = 0; j < sp->factor_count; j++) x[j] = doe_factor_scale(&sp->factors[j], u[j]);
        y[i] = f(x, sp->factor_count);
    }
    return y;
}

/* y = 3*x0 + x1, x ~ U(0,1): S1=0.9, S2=0.1, no interaction (ST==S). */
static double f_additive(const double *x, size_t k) {
    (void)k;
    return 3.0 * x[0] + 1.0 * x[1];
}

static int test_additive(void) {
    const char *s =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n"
        "seed: 11\n  samples: 4096\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    sobol_design_t d;
    double *y = eval_design(&sp, &d, f_additive);
    CHECK(y != NULL);

    sobol_index_t *ix = NULL; size_t n = 0;
    CHECK(sobol_analyze(&sp, y, d.npoints, &ix, &n, err) == 0);
    CHECK(n == 2);

    CHECK_DBL(ix[0].s1, 0.9, 0.05);
    CHECK_DBL(ix[1].s1, 0.1, 0.05);
    CHECK_DBL(ix[0].st, 0.9, 0.06);   /* additive => total ~ first-order */
    CHECK_DBL(ix[1].st, 0.1, 0.06);
    CHECK(fabs((ix[0].st - ix[0].s1)) < 0.05);   /* negligible interaction */

    free(ix); free(y); sobol_design_free(&d);
    return 1;
}

/* Ishigami, a=7, b=0.1, x ~ U(-pi,pi). Analytic:
 * S1=0.314 S2=0.442 S3=0.0 ; ST1=0.557 ST2=0.442 ST3=0.244. */
static double f_ishigami(const double *x, size_t k) {
    (void)k;
    double a = 7.0, b = 0.1;
    return sin(x[0]) + a * sin(x[1]) * sin(x[1]) + b * pow(x[2], 4.0) * sin(x[0]);
}

static int test_ishigami(void) {
    const char *s =
        "factors:\n"
        "  x1: -3.14159265, 3.14159265\n"
        "  x2: -3.14159265, 3.14159265\n"
        "  x3: -3.14159265, 3.14159265\n"
        "seed: 2026\n  samples: 8192\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);
    (void)PI;

    sobol_design_t d;
    double *y = eval_design(&sp, &d, f_ishigami);
    CHECK(y != NULL);

    sobol_index_t *ix = NULL; size_t n = 0;
    CHECK(sobol_analyze(&sp, y, d.npoints, &ix, &n, err) == 0);
    CHECK(n == 3);

    CHECK(fabs(ix[0].s1 - 0.314) < 0.10);   /* S1 */
    CHECK(fabs(ix[1].s1 - 0.442) < 0.10);   /* S2 */
    CHECK(fabs(ix[2].s1) < 0.10);           /* S3 ~ 0 */
    CHECK(ix[1].s1 > ix[0].s1);             /* x2 dominates first-order */

    /* the headline Sobol insight: x3 has ~0 first-order but a real total */
    CHECK(fabs(ix[2].st - 0.244) < 0.12);
    CHECK(ix[2].st - ix[2].s1 > 0.10);      /* x3 acts purely through interaction */
    CHECK(ix[0].st > ix[0].s1 + 0.05);      /* x1 also interacts (with x3) */

    free(ix); free(y); sobol_design_free(&d);
    return 1;
}

/* Same seed => identical A and B; all points in [0,1). */
static int test_determinism(void) {
    const char *s =
        "factors:\n  a: 0.0, 1.0\n  b: 0.0, 1.0\n  c: 0.0, 1.0\n"
        "seed: 5\n  samples: 512\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    sobol_design_t d1, d2;
    CHECK(sobol_design_build(&sp, &d1, err) == 0);
    CHECK(sobol_design_build(&sp, &d2, err) == 0);
    for (size_t i = 0; i < d1.n * d1.k; i++) {
        CHECK(d1.A[i] == d2.A[i]);
        CHECK(d1.B[i] == d2.B[i]);
        CHECK(d1.A[i] >= 0.0 && d1.A[i] < 1.0);
        CHECK(d1.B[i] >= 0.0 && d1.B[i] < 1.0);
    }
    sobol_design_free(&d1);
    sobol_design_free(&d2);
    return 1;
}

/* H1 — design sizing that would overflow size_t is rejected. */
static int test_design_build_overflow(void) {
    doe_space_t sp;
    memset(&sp, 0, sizeof sp);
    sp.factor_count = 2;
    strcpy(sp.factors[0].name, "a"); sp.factors[0].scale = DOE_LINEAR; sp.factors[0].lo = 0; sp.factors[0].hi = 1;
    strcpy(sp.factors[1].name, "b"); sp.factors[1].scale = DOE_LINEAR; sp.factors[1].lo = 0; sp.factors[1].hi = 1;
    sp.samples = (size_t)-1;   /* n*k overflows */

    sobol_design_t d;
    char err[DOE_ERR_SIZE];
    CHECK(sobol_design_build(&sp, &d, err) != 0);
    return 1;
}

/* H5 — a non-finite response is rejected by analyze. */
static int test_analyze_rejects_nonfinite(void) {
    const char *s = "factors:\n  a: 0,1\n  b: 0,1\nseed: 1\nsamples: 64\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    sobol_design_t d;
    CHECK(sobol_design_build(&sp, &d, err) == 0);
    double *y = malloc(d.npoints * sizeof *y);
    CHECK(y != NULL);
    for (size_t i = 0; i < d.npoints; i++) y[i] = 1.0;
    y[0] = NAN;

    sobol_index_t *ix = NULL; size_t n = 0;
    CHECK(sobol_analyze(&sp, y, d.npoints, &ix, &n, err) != 0);

    free(y);
    sobol_design_free(&d);
    return 1;
}

/*
 * `second_order:` was once a silent no-op, then an explicit rejection, and is
 * now implemented (M5). This pins the contract that replaced the rejection:
 * the flag changes the DESIGN SIZE, and asking for pairs without it is an
 * error rather than a guess.
 */
static int test_second_order_design_and_guard(void) {
    const char *with =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
        "seed: 7\n  samples: 16\n  second_order: true\n";
    const char *without =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
        "seed: 7\n  samples: 16\n";

    doe_space_t a, b; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(with, &a, err) == 0);
    CHECK(doe_space_parse(without, &b, err) == 0);
    CHECK(a.second_order == true && b.second_order == false);

    /* N(k+2) without, N(k+2+k(k-1)/2) with: 3 extra blocks for 3 factors. */
    CHECK(sobol_npoints(&b) == 16 * (3 + 2));
    CHECK(sobol_npoints(&a) == 16 * (3 + 2 + 3));

    sobol_design_t d;
    CHECK(sobol_design_build(&a, &d, err) == 0);
    CHECK(d.npoints == sobol_npoints(&a));   /* the size it reports is the size
                                              * it builds -- getting this wrong
                                              * made every caller read past its
                                              * own responses buffer */
    sobol_design_free(&d);

    /* Pairs without the flag must error: the extra blocks were never sampled,
     * so an answer would have to be invented. */
    double dummy[256] = {0};
    sobol_pair_t *pairs = NULL; size_t np = 0;
    memset(err, 'A', sizeof err);
    CHECK(sobol_analyze_pairs(&b, dummy, 256, &pairs, &np, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "second_order") != NULL);
    return 1;
}

/*
 * Sobol indices are variance SHARES, so a constant response makes them
 * undefined. Before this guard the estimator divided by zero and every index
 * printed as -nan while the tool exited 0 -- silent garbage, and the most
 * likely real cause is a model script that ignores its environment.
 */
static int test_zero_variance_is_rejected(void) {
    const char *spec =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n"
        "seed: 3\n  samples: 8\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(spec, &sp, err) == 0);

    size_t np = sobol_npoints(&sp);
    double *y = malloc(np * sizeof *y);
    CHECK(y != NULL);
    for (size_t i = 0; i < np; i++) y[i] = 2.5;      /* every run identical */

    sobol_index_t *out = NULL; size_t cnt = 0;
    memset(err, 'A', sizeof err);
    int rc = sobol_analyze(&sp, y, np, &out, &cnt, err);
    CHECK(rc != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "variance is zero") != NULL);

    /* A varying response over the same design must still succeed, and must
     * produce finite indices -- no NaN leaking through the new branch. */
    for (size_t i = 0; i < np; i++) y[i] = (double)i;
    memset(err, 'A', sizeof err);
    CHECK(sobol_analyze(&sp, y, np, &out, &cnt, err) == 0);
    for (size_t i = 0; i < cnt; i++) {
        CHECK(isfinite(out[i].s1));
        CHECK(isfinite(out[i].st));
        CHECK(isfinite(out[i].s1_lo) && isfinite(out[i].s1_hi));
        CHECK(isfinite(out[i].st_lo) && isfinite(out[i].st_hi));
    }
    free(out);
    free(y);
    return 1;
}

/* ----------------------------------------------------------------- M5 ----
 * Quasi-random sampling: the Saltelli Sec. 5.1 construction, at the tool
 * level rather than the primitive level.
 */

/* Default is quasi-random, and `sampling:` selects between the two. */
static int test_sampling_default_and_selection(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];

    CHECK(doe_space_parse("factors:\n  a: 0,1\n  b: 0,1\nsamples: 64\n", &sp, err) == 0);
    CHECK(sp.sampling == DOE_SAMPLING_SOBOL);          /* no key => quasi-random */

    CHECK(doe_space_parse("factors:\n  a: 0,1\nsampling: lhs\n", &sp, err) == 0);
    CHECK(sp.sampling == DOE_SAMPLING_LHS);
    CHECK(doe_space_parse("factors:\n  a: 0,1\nsampling: sobol\n", &sp, err) == 0);
    CHECK(sp.sampling == DOE_SAMPLING_SOBOL);

    /* An unknown sampler is an error, never a silent default */
    CHECK(doe_space_parse("factors:\n  a: 0,1\nsampling: halton\n", &sp, err) != 0);
    CHECK(strstr(err, "halton") != NULL);
    return 1;
}

/*
 * The contract from Saltelli et al. 2010 Sec. 5.1 p.263, checked on the
 * design the tool actually builds: A must be dimensions 0..k-1 and B
 * dimensions k..2k-1 of ONE sequence -- not a second draw, and not the two
 * halves swapped (consideration 2 gives A the better-equidistributed columns).
 */
static int test_qr_design_uses_one_sequence(void) {
    const size_t n = 128, k = 4;
    const char *s = "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\n  d: 0,1\n"
                    "samples: 128\nsampling: sobol\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    sobol_design_t d;
    CHECK(sobol_design_build(&sp, &d, err) == 0);
    CHECK(d.n == n && d.k == k);

    double *whole = malloc(n * 2 * k * sizeof *whole);
    CHECK(whole != NULL);
    CHECK(doe_sample_sobol(n, 2 * k, whole) == 0);

    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < k; j++) {
            CHECK(d.A[i * k + j] == whole[i * 2 * k + j]);        /* left half  */
            CHECK(d.B[i * k + j] == whole[i * 2 * k + k + j]);    /* right half */
        }

    free(whole);
    sobol_design_free(&d);
    return 1;
}

/*
 * The failure this whole design guards against. If A and B were two draws of
 * the same deterministic k-dimensional sequence they would be IDENTICAL, and
 * then A_B^(i) == A for every i, so every estimator collapses to zero.
 *
 * Exactly two rows legitimately coincide, and they are always rows 0 and 1:
 *
 *   row 0  the sequence's origin, all zeros in every dimension;
 *   row 1  all 0.5, because m_1 must be odd and less than 2 in EVERY
 *          dimension (Joe-Kuo notes, after eq. (2)), so m_1 = 1 is forced
 *          and v_1 = 1/2 identically -- making x_1 the centre of the cube.
 *
 * For those two rows A_B^(i) equals A, so they contribute nothing to either
 * estimator: 2 rows of N, which is 0.2% at the default N=1024 and 0.003% at
 * N=65536. That is a property of the unscrambled sequence the paper
 * prescribes, not of this implementation, and check E measures the accuracy
 * that results. Pinned at exactly 2 so a change in that cost is visible.
 */
static int test_qr_halves_coincide_only_at_rows_0_and_1(void) {
    static const size_t counts[] = {64, 256, 1024};
    for (size_t t = 0; t < sizeof counts / sizeof *counts; t++) {
        char s[256];
        snprintf(s, sizeof s,
                 "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\nsamples: %zu\n", counts[t]);
        doe_space_t sp; char err[DOE_ERR_SIZE];
        CHECK(doe_space_parse(s, &sp, err) == 0);

        sobol_design_t d;
        CHECK(sobol_design_build(&sp, &d, err) == 0);

        size_t identical = 0;
        for (size_t i = 0; i < d.n; i++) {
            int same = 1;
            for (size_t j = 0; j < d.k; j++)
                if (d.A[i * d.k + j] != d.B[i * d.k + j]) { same = 0; break; }
            if (same) { identical++; CHECK(i <= 1); }
        }
        CHECK(identical == 2);

        for (size_t j = 0; j < d.k; j++) {
            CHECK(d.A[j] == 0.0 && d.B[j] == 0.0);                    /* row 0 */
            CHECK(d.A[d.k + j] == 0.5 && d.B[d.k + j] == 0.5);        /* row 1 */
        }
        sobol_design_free(&d);
    }
    return 1;
}

/* The factor cap must be an error naming the limit, never a quiet swap to
 * LHS -- a user would have no way to tell which method produced the numbers. */
static int test_qr_factor_cap_errors_not_falls_back(void) {
    doe_space_t sp;
    memset(&sp, 0, sizeof sp);
    sp.samples = 16;
    sp.sampling = DOE_SAMPLING_SOBOL;
    sp.factor_count = DOE_SOBOL_MAX_FACTORS + 1;
    for (size_t i = 0; i < sp.factor_count; i++) {
        snprintf(sp.factors[i].name, DOE_MAX_NAME, "x%zu", i);
        sp.factors[i].scale = DOE_LINEAR; sp.factors[i].lo = 0; sp.factors[i].hi = 1;
    }

    sobol_design_t d;
    char err[DOE_ERR_SIZE];
    CHECK(sobol_design_build(&sp, &d, err) != 0);
    CHECK(strstr(err, "512") != NULL);          /* says what the limit is */
    CHECK(strstr(err, "morris") != NULL);       /* and what to do instead */

    /* one fewer factor is fine, so the boundary is exactly where it claims */
    sp.factor_count = DOE_SOBOL_MAX_FACTORS;
    CHECK(sobol_design_build(&sp, &d, err) == 0);
    sobol_design_free(&d);

    /* and LHS is unaffected by the QR table's limit */
    sp.factor_count = DOE_SOBOL_MAX_FACTORS + 1;
    sp.sampling = DOE_SAMPLING_LHS;
    CHECK(sobol_design_build(&sp, &d, err) == 0);
    sobol_design_free(&d);
    return 1;
}

/*
 * The QR design ignores `seed:` -- a quasi-random sequence is deterministic.
 * Stated in doe.h; pinned here because a future change that made the design
 * seed-dependent would silently break reproducibility of published designs.
 */
static int test_qr_design_ignores_seed(void) {
    doe_space_t a, b; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  x: 0,1\n  y: 0,1\nsamples: 64\nseed: 1\n", &a, err) == 0);
    CHECK(doe_space_parse("factors:\n  x: 0,1\n  y: 0,1\nsamples: 64\nseed: 99999\n", &b, err) == 0);

    sobol_design_t da, db;
    CHECK(sobol_design_build(&a, &da, err) == 0);
    CHECK(sobol_design_build(&b, &db, err) == 0);
    for (size_t i = 0; i < da.n * da.k; i++) {
        CHECK(da.A[i] == db.A[i]);
        CHECK(da.B[i] == db.B[i]);
    }
    sobol_design_free(&da); sobol_design_free(&db);

    /* LHS, by contrast, must respond to the seed */
    CHECK(doe_space_parse("factors:\n  x: 0,1\n  y: 0,1\nsamples: 64\nseed: 1\nsampling: lhs\n", &a, err) == 0);
    CHECK(doe_space_parse("factors:\n  x: 0,1\n  y: 0,1\nsamples: 64\nseed: 99999\nsampling: lhs\n", &b, err) == 0);
    CHECK(sobol_design_build(&a, &da, err) == 0);
    CHECK(sobol_design_build(&b, &db, err) == 0);
    int differs = 0;
    for (size_t i = 0; i < da.n * da.k; i++) if (da.A[i] != db.A[i]) differs = 1;
    CHECK(differs);
    sobol_design_free(&da); sobol_design_free(&db);
    return 1;
}

int main(void) {
    printf("sobol tests\n");
    RUN_TEST(test_second_order_design_and_guard);
    RUN_TEST(test_zero_variance_is_rejected);
    RUN_TEST(test_additive);
    RUN_TEST(test_ishigami);
    RUN_TEST(test_determinism);
    RUN_TEST(test_design_build_overflow);
    RUN_TEST(test_analyze_rejects_nonfinite);
    RUN_TEST(test_sampling_default_and_selection);
    RUN_TEST(test_qr_design_uses_one_sequence);
    RUN_TEST(test_qr_halves_coincide_only_at_rows_0_and_1);
    RUN_TEST(test_qr_factor_cap_errors_not_falls_back);
    RUN_TEST(test_qr_design_ignores_seed);
    return TEST_SUMMARY();
}
