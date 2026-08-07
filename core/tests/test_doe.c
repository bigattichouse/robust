/*
 * test_doe.c — unit tests for the libdoe core (PRNG, .space parser/scaling,
 * stats, JSON). The determinism test is the load-bearing one: reproducible
 * designs depend on identical PRNG streams from a given seed.
 */

#include "doe.h"
#include "test_framework.h"

#include <string.h>
#include <math.h>

/* ---- PRNG -------------------------------------------------------------- */

static int test_prng_determinism(void) {
    doe_rng_t a, b;
    doe_rng_seed(&a, 12345);
    doe_rng_seed(&b, 12345);
    for (int i = 0; i < 1000; i++) {
        CHECK(doe_rng_next(&a) == doe_rng_next(&b));   /* same seed -> same stream */
    }

    doe_rng_t c;
    doe_rng_seed(&a, 12345);
    doe_rng_seed(&c, 99999);
    int differs = 0;
    for (int i = 0; i < 10; i++) {
        if (doe_rng_next(&a) != doe_rng_next(&c)) differs = 1;
    }
    CHECK(differs);                                    /* different seed -> different stream */
    return 1;
}

static int test_prng_uniform_range(void) {
    doe_rng_t r;
    doe_rng_seed(&r, 7);
    const int N = 100000;
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        double u = doe_rng_uniform(&r);
        CHECK(u >= 0.0 && u < 1.0);
        sum += u;
    }
    CHECK_DBL(sum / N, 0.5, 0.01);                     /* mean ~ 0.5 */
    return 1;
}

/* ---- .space parsing + scaling ----------------------------------------- */

static int test_space_linear_and_log(void) {
    const char *s =
        "factors:\n"
        "  x: 0.0, 10.0\n"
        "  y: 1e-3, 1.0 log\n"
        "seed: 42\n"
        "samples: 256\n"
        "trajectories: 15\n";
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);
    CHECK(sp.factor_count == 2);
    CHECK(sp.seed == 42);
    CHECK(sp.samples == 256);
    CHECK(sp.trajectories == 15);

    CHECK(sp.factors[0].scale == DOE_LINEAR);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 0.0), 0.0, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 0.5), 5.0, 1e-9);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 1.0), 10.0, 1e-9);

    CHECK(sp.factors[1].scale == DOE_LOG);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 0.0), 1e-3, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 1.0), 1.0, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 0.5), sqrt(1e-3 * 1.0), 1e-9);  /* geometric mid */
    return 1;
}

static int test_space_categorical(void) {
    const char *s =
        "factors:\n"
        "  mode: random, structured, gauze\n"
        "  recycle: true, false\n";
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    CHECK(sp.factors[0].scale == DOE_CATEGORICAL);
    CHECK(sp.factors[0].level_count == 3);
    char buf[DOE_MAX_VALUE];
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.0,  buf, sizeof buf), "random") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.5,  buf, sizeof buf), "structured") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.99, buf, sizeof buf), "gauze") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 1.0,  buf, sizeof buf), "gauze") == 0);  /* u==1 guard */

    CHECK(sp.factors[1].scale == DOE_CATEGORICAL);
    CHECK(sp.factors[1].level_count == 2);
    return 1;
}

static int test_space_errors(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  x: 5.0, 1.0\n",   &sp, err) != 0);  /* lo >= hi  */
    CHECK(doe_space_parse("factors:\n  x: -1, 10 log\n", &sp, err) != 0);  /* log <= 0  */
    CHECK(doe_space_parse("seed: 1\n",                   &sp, err) != 0);  /* no factors */
    return 1;
}

/* ---- stats ------------------------------------------------------------- */

static int test_stats(void) {
    double x[5] = {2, 4, 4, 4, 5};
    CHECK_DBL(doe_mean(x, 5), 3.8, 1e-12);
    CHECK_DBL(doe_variance(x, 5), 1.2, 1e-12);          /* sample variance (n-1) */
    CHECK_DBL(doe_std(x, 5), sqrt(1.2), 1e-12);
    return 1;
}

/* ---- JSON -------------------------------------------------------------- */

static int test_json_escape(void) {
    char *j = doe_json_escape("a\"b\\c\n");
    CHECK(j != NULL);
    CHECK(strcmp(j, "a\\\"b\\\\c\\n") == 0);
    doe_free(j);
    return 1;
}

/* ---- doe_ols_src: standardized regression -------------------------------- */

/*
 * EXPANSION.md E1's validation clause: exact on a known linear model. With
 * y = 10*x0 + 5*x1 + 0*x2 the standardized coefficients must be proportional
 * to (10, 5, 0) and R^2 must be exactly 1 -- there is nothing for a linear
 * model to miss.
 */
static int test_ols_exact_on_linear(void) {
    enum { N = 60, K = 3 };
    double X[N * K], y[N];
    doe_rng_t rng; doe_rng_seed(&rng, 11);
    /*
     * Each column is an independent PERMUTATION of the same values, so every
     * column has identical mean and standard deviation by construction. That
     * matters: SRC_j = beta_j * sd_j / sd_y, so unequal column spread shifts
     * the ratio away from beta_0/beta_1 for reasons that have nothing to do
     * with the regression. With plain random draws the sd's differ by a few
     * percent and the ratio lands near 1.6 rather than 2 -- noise, not error.
     */
    for (size_t j = 0; j < K; j++) {
        double v[N];
        for (size_t i = 0; i < N; i++) v[i] = (double)i / (double)N;
        for (size_t i = N; i > 1; i--) {          /* Fisher-Yates */
            size_t r = (size_t)(doe_rng_uniform(&rng) * (double)i);
            if (r >= i) r = i - 1;
            double t = v[i-1]; v[i-1] = v[r]; v[r] = t;
        }
        for (size_t i = 0; i < N; i++) X[i * K + j] = v[i];
    }
    for (size_t i = 0; i < N; i++)
        y[i] = 10.0 * X[i*K+0] + 5.0 * X[i*K+1] + 0.0 * X[i*K+2];
    double coef[K], r2 = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) == 0);

    CHECK_DBL(r2, 1.0, 1e-9);
    CHECK(coef[0] > 0.0 && coef[1] > 0.0);
    CHECK(fabs(coef[2]) < 1e-9);              /* inert factor is exactly flat */
    /* SRC is proportional to the coefficients when the factors share a
     * distribution, so the ratio must recover 10/5. */
    CHECK_DBL(coef[0] / coef[1], 2.0, 1e-6);
    return 1;
}

/* The sign is the point: SRC reports direction, which variance shares cannot. */
static int test_ols_reports_direction(void) {
    enum { N = 50, K = 2 };
    double X[N * K], y[N];
    doe_rng_t rng; doe_rng_seed(&rng, 3);
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < K; j++) X[i * K + j] = doe_rng_uniform(&rng);
        y[i] = 4.0 * X[i*K+0] - 9.0 * X[i*K+1];
    }
    double coef[K], r2 = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) == 0);
    CHECK(coef[0] > 0.0);                     /* raises  */
    CHECK(coef[1] < 0.0);                     /* lowers  */
    CHECK(fabs(coef[1]) > fabs(coef[0]));     /* and matters more */
    return 1;
}

/* A curved but monotone response: SRC is imperfect, ranks recover it. */
static int test_ols_ranks_beat_values_on_curvature(void) {
    enum { N = 80, K = 1 };
    double X[N * K], y[N], Xr[N * K], yr[N];
    doe_rng_t rng; doe_rng_seed(&rng, 21);
    for (size_t i = 0; i < N; i++) {
        double x = doe_rng_uniform(&rng);
        X[i] = x;
        y[i] = x * x * x * x * x;             /* strictly increasing, very curved */
    }
    memcpy(Xr, X, sizeof X); memcpy(yr, y, sizeof y);

    double c1[K], r2v = 0.0, c2[K], r2r = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, c1, &r2v, err) == 0);
    doe_rank_transform(Xr, yr, N, K);
    CHECK(doe_ols_src(Xr, yr, N, K, c2, &r2r, err) == 0);

    CHECK(r2r > r2v);                          /* ranks explain more */
    CHECK(r2r > 0.99);                         /* monotone: nearly perfect */
    return 1;
}

static int test_ols_rejects_degenerate(void) {
    enum { N = 20, K = 2 };
    double X[N * K], y[N]; char err[DOE_ERR_SIZE];
    doe_rng_t rng; doe_rng_seed(&rng, 7);

    /* constant response */
    for (size_t i = 0; i < N; i++) {
        X[i*K+0] = doe_rng_uniform(&rng); X[i*K+1] = doe_rng_uniform(&rng);
        y[i] = 3.0;
    }
    double coef[K], r2;
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "constant") != NULL);

    /* constant factor column */
    for (size_t i = 0; i < N; i++) { X[i*K+0] = 0.5; y[i] = doe_rng_uniform(&rng); }
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "constant") != NULL);

    /* two factors that move together: effects cannot be separated */
    for (size_t i = 0; i < N; i++) {
        double v = doe_rng_uniform(&rng);
        X[i*K+0] = v; X[i*K+1] = 2.0 * v;
        y[i] = v;
    }
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "rank-deficient") != NULL);

    /* fewer runs than factors */
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, 2, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "more runs than factors") != NULL);
    return 1;
}

static int test_quantiles(void) {
    double v[9] = {9, 1, 8, 2, 7, 3, 6, 4, 5};
    CHECK_DBL(doe_median(v, 9), 5.0, 1e-12);
    double w[5] = {10, 20, 30, 40, 50};
    CHECK_DBL(doe_quantile(w, 5, 0.0), 10.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 1.0), 50.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 0.5), 30.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 0.25), 20.0, 1e-12);
    return 1;
}

/*
 * The JSON escaper allocates len*6+1, which is EXACTLY the worst case: every
 * character escaping to \u00XX. A string entirely of control characters sits
 * on that boundary, so it is the input that would expose an off-by-one.
 */
static int test_json_escape_worst_case(void) {
    char in[64];
    for (size_t i = 0; i < sizeof in - 1; i++) in[i] = 0x01;   /* all escape */
    in[sizeof in - 1] = '\0';

    char *e = doe_json_escape(in);
    CHECK(e != NULL);
    CHECK(strlen(e) == (sizeof in - 1) * 6);
    for (size_t i = 0; i < strlen(e); i += 6)
        CHECK(strncmp(e + i, "\\u0001", 6) == 0);
    doe_free(e);

    /* The named escapes, and a plain string passing through untouched. */
    e = doe_json_escape("a\"b\\c\nd\re\tf");
    CHECK(e != NULL);
    CHECK(strstr(e, "\\\"") && strstr(e, "\\\\") && strstr(e, "\\n")
          && strstr(e, "\\r") && strstr(e, "\\t"));
    doe_free(e);

    e = doe_json_escape("plain");
    CHECK(e != NULL && strcmp(e, "plain") == 0);
    doe_free(e);

    CHECK(doe_json_escape(NULL) == NULL);
    return 1;
}

int main(void) {
    printf("libdoe core tests\n");
    RUN_TEST(test_json_escape_worst_case);
    RUN_TEST(test_ols_exact_on_linear);
    RUN_TEST(test_ols_reports_direction);
    RUN_TEST(test_ols_ranks_beat_values_on_curvature);
    RUN_TEST(test_ols_rejects_degenerate);
    RUN_TEST(test_quantiles);
    RUN_TEST(test_prng_determinism);
    RUN_TEST(test_prng_uniform_range);
    RUN_TEST(test_space_linear_and_log);
    RUN_TEST(test_space_categorical);
    RUN_TEST(test_space_errors);
    RUN_TEST(test_stats);
    RUN_TEST(test_json_escape);
    return TEST_SUMMARY();
}
