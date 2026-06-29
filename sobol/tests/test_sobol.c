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

int main(void) {
    printf("sobol tests\n");
    RUN_TEST(test_additive);
    RUN_TEST(test_ishigami);
    RUN_TEST(test_determinism);
    return TEST_SUMMARY();
}
