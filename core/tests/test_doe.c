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
    CHECK(strcmp(doe_factor_value(&sp.factors[0], 0.0,  buf, sizeof buf), "random") == 0);
    CHECK(strcmp(doe_factor_value(&sp.factors[0], 0.5,  buf, sizeof buf), "structured") == 0);
    CHECK(strcmp(doe_factor_value(&sp.factors[0], 0.99, buf, sizeof buf), "gauze") == 0);
    CHECK(strcmp(doe_factor_value(&sp.factors[0], 1.0,  buf, sizeof buf), "gauze") == 0);  /* u==1 guard */

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

int main(void) {
    printf("libdoe core tests\n");
    RUN_TEST(test_prng_determinism);
    RUN_TEST(test_prng_uniform_range);
    RUN_TEST(test_space_linear_and_log);
    RUN_TEST(test_space_categorical);
    RUN_TEST(test_space_errors);
    RUN_TEST(test_stats);
    RUN_TEST(test_json_escape);
    return TEST_SUMMARY();
}
