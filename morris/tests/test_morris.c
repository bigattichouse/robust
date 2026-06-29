/*
 * test_morris.c — validates the elementary-effects math against functions with
 * known sensitivity structure, in-process (no script/run machinery).
 */

#include "morris.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Evaluate f over a freshly built design and return the responses + design. */
static double *eval_design(const doe_space_t *sp, morris_design_t *d,
                           double (*f)(const double *u, size_t k)) {
    char err[DOE_ERR_SIZE];
    if (morris_design_build(sp, d, err) != 0) return NULL;
    double *y = malloc(d->npoints * sizeof *y);
    if (!y) return NULL;
    for (size_t i = 0; i < d->npoints; i++) {
        y[i] = f(&d->u[i * d->k], d->k);
    }
    return y;
}

/* y = 10*x0 + 1*x1 + 0*x2  — additive; EE are exact constants. */
static double f_linear(const double *u, size_t k) {
    (void)k;
    return 10.0 * u[0] + 1.0 * u[1] + 0.0 * u[2];
}

static int test_linear_ranking(void) {
    const char *s =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
        "seed: 2026\n  trajectories: 20\n  grid_levels: 4\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    morris_design_t d;
    double *y = eval_design(&sp, &d, f_linear);
    CHECK(y != NULL);

    morris_effect_t *eff = NULL; size_t n = 0;
    CHECK(morris_analyze(&sp, y, d.npoints, &eff, &n, err) == 0);
    CHECK(n == 3);

    /* effects come back in factor order */
    CHECK_DBL(eff[0].mu_star, 10.0, 1e-9);
    CHECK_DBL(eff[1].mu_star,  1.0, 1e-9);
    CHECK_DBL(eff[2].mu_star,  0.0, 1e-9);
    /* additive => no spread */
    CHECK_DBL(eff[0].sigma, 0.0, 1e-9);
    CHECK_DBL(eff[1].sigma, 0.0, 1e-9);
    CHECK_DBL(eff[2].sigma, 0.0, 1e-9);

    free(eff); free(y); morris_design_free(&d);
    return 1;
}

/* y = 5*x0 + 8*x1*x2  — x0 additive (sigma 0); x1,x2 interact (sigma > 0). */
static double f_interaction(const double *u, size_t k) {
    (void)k;
    return 5.0 * u[0] + 8.0 * u[1] * u[2];
}

static int test_interaction_flag(void) {
    const char *s =
        "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
        "seed: 99\n  trajectories: 30\n  grid_levels: 4\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    morris_design_t d;
    double *y = eval_design(&sp, &d, f_interaction);
    CHECK(y != NULL);

    morris_effect_t *eff = NULL; size_t n = 0;
    CHECK(morris_analyze(&sp, y, d.npoints, &eff, &n, err) == 0);

    CHECK_DBL(eff[0].mu_star, 5.0, 1e-9);   /* linear term, exact */
    CHECK(eff[0].sigma < 1e-9);             /* additive: no spread */
    CHECK(eff[1].sigma > 0.1);              /* EE_1 = 8*x2 varies */
    CHECK(eff[2].sigma > 0.1);              /* EE_2 = 8*x1 varies */
    CHECK(eff[1].mu_star > 0.5);
    CHECK(eff[2].mu_star > 0.5);

    free(eff); free(y); morris_design_free(&d);
    return 1;
}

/* Same seed => identical design (the reconstruction guarantee). */
static int test_design_determinism(void) {
    const char *s =
        "factors:\n  a: 0.0, 1.0\n  b: 0.0, 1.0\n  c: 0.0, 1.0\n"
        "seed: 7\n  trajectories: 12\n  grid_levels: 4\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    morris_design_t d1, d2;
    CHECK(morris_design_build(&sp, &d1, err) == 0);
    CHECK(morris_design_build(&sp, &d2, err) == 0);
    CHECK(d1.npoints == d2.npoints);
    for (size_t i = 0; i < d1.npoints * d1.k; i++) CHECK(d1.u[i] == d2.u[i]);

    /* every design point stays inside [0,1] */
    for (size_t i = 0; i < d1.npoints * d1.k; i++) CHECK(d1.u[i] >= 0.0 && d1.u[i] <= 1.0);

    morris_design_free(&d1);
    morris_design_free(&d2);
    return 1;
}

int main(void) {
    printf("morris tests\n");
    RUN_TEST(test_linear_ranking);
    RUN_TEST(test_interaction_flag);
    RUN_TEST(test_design_determinism);
    return TEST_SUMMARY();
}
