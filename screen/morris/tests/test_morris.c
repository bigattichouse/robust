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

/* H1 — design sizing that would overflow size_t is rejected, not wrapped small. */
static int test_design_build_overflow(void) {
    doe_space_t sp;
    memset(&sp, 0, sizeof sp);
    sp.factor_count = 2;
    strcpy(sp.factors[0].name, "a"); sp.factors[0].scale = DOE_LINEAR; sp.factors[0].lo = 0; sp.factors[0].hi = 1;
    strcpy(sp.factors[1].name, "b"); sp.factors[1].scale = DOE_LINEAR; sp.factors[1].lo = 0; sp.factors[1].hi = 1;
    sp.grid_levels = 4;
    sp.trajectories = ((size_t)-1) / 2;   /* r*(k+1) overflows */

    morris_design_t d;
    char err[DOE_ERR_SIZE];
    CHECK(morris_design_build(&sp, &d, err) != 0);
    return 1;
}

/* H5 — a non-finite response is rejected by analyze. */
static int test_analyze_rejects_nonfinite(void) {
    const char *s = "factors:\n  x0: 0,1\n  x1: 0,1\nseed: 1\ntrajectories: 5\ngrid_levels: 4\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    morris_design_t d;
    CHECK(morris_design_build(&sp, &d, err) == 0);
    double *y = malloc(d.npoints * sizeof *y);
    CHECK(y != NULL);
    for (size_t i = 0; i < d.npoints; i++) y[i] = 1.0;
    y[2] = INFINITY;

    morris_effect_t *eff = NULL; size_t n = 0;
    CHECK(morris_analyze(&sp, y, d.npoints, &eff, &n, err) != 0);

    free(y);
    morris_design_free(&d);
    return 1;
}

/* ==========================================================================
 * Group screening (spec/morris-groups.bp)
 * ======================================================================== */

/* y = 10*x0 - 10*x1 : equal and opposite, both in ONE group. */
static double f_opposing(const double *u, size_t k) {
    (void)k;
    return 10.0 * u[0] - 10.0 * u[1];
}

static double *eval_group_design(const doe_space_t *sp, morris_group_design_t *d,
                                 double (*f)(const double *u, size_t k)) {
    char err[DOE_ERR_SIZE];
    if (morris_group_design_build(sp, d, err) != 0) return NULL;
    double *y = malloc(d->npoints * sizeof *y);
    if (!y) return NULL;
    for (size_t i = 0; i < d->npoints; i++) y[i] = f(&d->u[i * d->k], d->k);
    return y;
}

/*
 * The group path must reduce to the scalar path. On a linear model the
 * elementary effects are exact constants, so this holds to the bit regardless
 * of where each design samples -- which is what makes it a meaningful check
 * rather than an RNG-alignment coincidence.
 */
static int test_group_singletons_equal_per_factor(void) {
    const char *per = "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
                      "seed: 2026\n  trajectories: 20\n  grid_levels: 4\n";
    const char *grp = "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
                      "seed: 2026\n  trajectories: 20\n  grid_levels: 4\n"
                      "groups:\n  g0: x0\n  g1: x1\n  g2: x2\n";
    doe_space_t a, b; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(per, &a, err) == 0);
    CHECK(doe_space_parse(grp, &b, err) == 0);
    CHECK(b.group_count == 3);

    morris_design_t d;
    double *y = eval_design(&a, &d, f_linear);
    CHECK(y != NULL);
    morris_effect_t *eff = NULL; size_t n = 0;
    CHECK(morris_analyze(&a, y, d.npoints, &eff, &n, err) == 0);

    morris_group_design_t gd;
    double *gy = eval_group_design(&b, &gd, f_linear);
    CHECK(gy != NULL);
    morris_group_effect_t *geff = NULL; size_t gn = 0;
    CHECK(morris_group_analyze(&b, gy, gd.npoints, &geff, &gn, err) == 0);

    CHECK(gn == n);
    CHECK(gd.npoints == d.npoints);           /* r*(G+1) == r*(k+1) when G==k */
    for (size_t i = 0; i < n; i++) CHECK_DBL(geff[i].mu_star, eff[i].mu_star, 1e-12);

    free(y); free(gy); free(eff); free(geff);
    morris_design_free(&d); morris_group_design_free(&gd);
    return 1;
}

/*
 * THE PROPERTY THAT REPLACES SEQUENTIAL BIFURCATION. Two equal and opposite
 * factors share one group. A signed group total averages to zero and the group
 * would be discarded -- SB's silent false negative. Taking the absolute value
 * of the group effect keeps it visible.
 */
static int test_group_opposing_signs_do_not_cancel(void) {
    const char *spec = "factors:\n  x0: 0.0, 1.0\n  x1: 0.0, 1.0\n  x2: 0.0, 1.0\n"
                       "seed: 99\n  trajectories: 50\n  grid_levels: 4\n"
                       "groups:\n  both: x0, x1\n  other: x2\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(spec, &sp, err) == 0);

    morris_group_design_t d;
    double *y = eval_group_design(&sp, &d, f_opposing);
    CHECK(y != NULL);
    morris_group_effect_t *eff = NULL; size_t n = 0;
    CHECK(morris_group_analyze(&sp, y, d.npoints, &eff, &n, err) == 0);
    CHECK(n == 2);

    /* |d| is 0 when the two move together and 20 when they oppose, so the
     * mean sits near 10 -- and must be well clear of zero. */
    CHECK(strcmp(eff[0].name, "both") == 0);
    CHECK(eff[0].mu_star > 5.0);
    CHECK(eff[1].mu_star == 0.0);          /* x2 has no effect at all */

    free(y); free(eff); morris_group_design_free(&d);
    return 1;
}

/* Cost is exactly r*(G+1), and must be predictable before spending anything. */
static int test_group_cost_model(void) {
    const char *spec = "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\n  d: 0,1\n"
                       "seed: 1\n  trajectories: 7\n  grid_levels: 4\n"
                       "groups:\n  g1: a, b\n  g2: c\n  g3: d\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(spec, &sp, err) == 0);
    CHECK(morris_group_npoints(&sp) == 7 * (3 + 1));

    morris_group_design_t d;
    CHECK(morris_group_design_build(&sp, &d, err) == 0);
    CHECK(d.npoints == 28);
    CHECK(d.G == 3);
    morris_group_design_free(&d);
    return 1;
}

/* Consecutive points differ exactly on one group's members, every point stays
 * in [0,1], and each group moves exactly once per trajectory. */
static int test_group_design_structure(void) {
    const char *spec = "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\n"
                       "seed: 5\n  trajectories: 6\n  grid_levels: 4\n"
                       "groups:\n  g1: a, b\n  g2: c\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(spec, &sp, err) == 0);

    morris_group_design_t d;
    CHECK(morris_group_design_build(&sp, &d, err) == 0);

    for (size_t i = 0; i < d.npoints * d.k; i++) {
        CHECK(d.u[i] >= 0.0 && d.u[i] <= 1.0);
    }
    for (size_t t = 0; t < d.r; t++) {
        size_t seen[DOE_MAX_GROUPS] = {0};
        for (size_t s = 0; s < d.G; s++) {
            size_t g = d.moved_group[t * d.G + s];
            seen[g]++;
            const double *prev = &d.u[(t * (d.G + 1) + s) * d.k];
            const double *cur  = &d.u[(t * (d.G + 1) + s + 1) * d.k];
            for (size_t i = 0; i < d.k; i++) {
                int member = sp.groups[g].members[i];
                if (member) CHECK(cur[i] != prev[i]);
                else        CHECK(cur[i] == prev[i]);
            }
        }
        for (size_t g = 0; g < d.G; g++) CHECK(seen[g] == 1);
    }
    morris_group_design_free(&d);
    return 1;
}

/* Same seed, same design; and declaring groups in a different order must not
 * change any group's mu*. */
static int test_group_determinism_and_order(void) {
    const char *one = "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\n"
                      "seed: 77\n  trajectories: 12\n  grid_levels: 4\n"
                      "groups:\n  g1: a, b\n  g2: c\n";
    const char *two = "factors:\n  a: 0,1\n  b: 0,1\n  c: 0,1\n"
                      "seed: 77\n  trajectories: 12\n  grid_levels: 4\n"
                      "groups:\n  g2: c\n  g1: a, b\n";
    doe_space_t s1, s2; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(one, &s1, err) == 0);
    CHECK(doe_space_parse(two, &s2, err) == 0);

    morris_group_design_t d1, d2;
    double *y1 = eval_group_design(&s1, &d1, f_linear);
    double *y2 = eval_group_design(&s2, &d2, f_linear);
    CHECK(y1 && y2);

    morris_group_effect_t *e1 = NULL, *e2 = NULL; size_t n1 = 0, n2 = 0;
    CHECK(morris_group_analyze(&s1, y1, d1.npoints, &e1, &n1, err) == 0);
    CHECK(morris_group_analyze(&s2, y2, d2.npoints, &e2, &n2, err) == 0);
    CHECK(n1 == 2 && n2 == 2);

    /* Match by name, since the declaration order differs. */
    for (size_t i = 0; i < n1; i++) {
        for (size_t j = 0; j < n2; j++) {
            if (strcmp(e1[i].name, e2[j].name) == 0)
                CHECK_DBL(e1[i].mu_star, e2[j].mu_star, 1e-12);
        }
    }
    free(y1); free(y2); free(e1); free(e2);
    morris_group_design_free(&d1); morris_group_design_free(&d2);
    return 1;
}

static int test_group_rejects_bad_input(void) {
    char err[DOE_ERR_SIZE];
    doe_space_t sp;
    /* No groups declared: the group path must refuse rather than invent one. */
    CHECK(doe_space_parse("factors:\n  a: 0,1\n  b: 0,1\nseed: 1\n", &sp, err) == 0);
    morris_group_design_t d;
    memset(err, 'A', sizeof err);
    CHECK(morris_group_design_build(&sp, &d, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);

    /* Too few responses. */
    const char *spec = "factors:\n  a: 0,1\n  b: 0,1\n"
                       "seed: 1\n  trajectories: 4\n  grid_levels: 4\n"
                       "groups:\n  g1: a\n  g2: b\n";
    CHECK(doe_space_parse(spec, &sp, err) == 0);
    double few[3] = {1, 2, 3};
    morris_group_effect_t *eff = NULL; size_t n = 0;
    memset(err, 'A', sizeof err);
    CHECK(morris_group_analyze(&sp, few, 3, &eff, &n, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    return 1;
}

/* ==========================================================================
 * Recursive splitting
 * ======================================================================== */

/*
 * A planted model: `NPLANT` of `NF` factors carry large coefficients, with
 * ALTERNATING SIGNS so that any two of them landing in one group would cancel
 * under a signed group total. The rest are exactly inert.
 */
#define BIF_NF     64
#define BIF_NPLANT  6
static const size_t bif_planted[BIF_NPLANT] = { 3, 4, 17, 18, 40, 55 };

static double bif_coef(size_t i) {
    for (size_t p = 0; p < BIF_NPLANT; p++)
        if (bif_planted[p] == i) return (p % 2) ? -10.0 : 10.0;
    return 0.0;
}

static int bif_eval(void *ctx, const doe_space_t *sp, const double *u,
                    size_t npoints, size_t k, double *responses, char *err) {
    (void)sp; (void)err;
    size_t *calls = (size_t *)ctx;
    for (size_t i = 0; i < npoints; i++) {
        double y = 0.0;
        for (size_t j = 0; j < k; j++) y += bif_coef(j) * u[i * k + j];
        responses[i] = y;
    }
    if (calls) *calls += npoints;
    return 0;
}

static int make_bif_space(doe_space_t *sp, char *err) {
    char spec[8192];
    int off = snprintf(spec, sizeof spec, "factors:\n");
    for (size_t i = 0; i < BIF_NF; i++)
        off += snprintf(spec + off, sizeof spec - (size_t)off,
                        "  x%zu: 0.0, 1.0\n", i);
    snprintf(spec + off, sizeof spec - (size_t)off,
             "seed: 4242\ntrajectories: 8\ngrid_levels: 4\n");
    return doe_space_parse(spec, sp, err);
}

/*
 * THE DELIVERABLE from EXPANSION_NOTE.md §4A: the false-negative rate under
 * mixed signs. Every planted factor must survive. A method that cancels
 * opposing effects inside a group would drop one silently -- which is exactly
 * why sequential bifurcation was not built.
 */
static int test_bifurcate_finds_planted_factors(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(make_bif_space(&sp, err) == 0);

    morris_bifurcate_opts_t opts;
    memset(&opts, 0, sizeof opts);
    opts.keep_share = 0.95;

    size_t calls = 0;
    morris_bifurcate_result_t res;
    CHECK(morris_bifurcate(&sp, &opts, bif_eval, &calls, &res, err) == 0);

    /* No false negatives: every planted factor is in the survivor set. */
    for (size_t p = 0; p < BIF_NPLANT; p++)
        CHECK(res.survivors[bif_planted[p]]);

    /* And it actually narrowed: far fewer survivors than factors. */
    CHECK(res.survivor_count >= BIF_NPLANT);
    CHECK(res.survivor_count < BIF_NF / 2);

    morris_bifurcate_free(&res);
    return 1;
}

/* Cost must be predictable before spending anything, and the prediction must
 * be an upper bound on what is actually spent. */
static int test_bifurcate_cost_is_bounded_and_predicted(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(make_bif_space(&sp, err) == 0);

    morris_bifurcate_opts_t opts;
    memset(&opts, 0, sizeof opts);
    opts.keep_share = 0.95;

    size_t predicted = morris_bifurcate_budget(&sp, &opts);
    CHECK(predicted > 0);

    size_t calls = 0;
    morris_bifurcate_result_t res;
    CHECK(morris_bifurcate(&sp, &opts, bif_eval, &calls, &res, err) == 0);

    CHECK(res.predicted_max == predicted);   /* same answer before and after */
    CHECK(res.evaluations == calls);         /* accounting matches reality   */
    CHECK(res.evaluations <= res.predicted_max);

    /* And the whole point: far cheaper than screening every factor. */
    CHECK(res.evaluations < sp.trajectories * (BIF_NF + 1));

    morris_bifurcate_free(&res);
    return 1;
}

/* The trace must explain itself: every round recorded, drops marked. */
static int test_bifurcate_trace(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(make_bif_space(&sp, err) == 0);

    morris_bifurcate_opts_t opts;
    memset(&opts, 0, sizeof opts);
    opts.keep_share = 0.95;

    morris_bifurcate_result_t res;
    CHECK(morris_bifurcate(&sp, &opts, bif_eval, NULL, &res, err) == 0);
    CHECK(res.trace_count > 0);
    CHECK(res.rounds_run > 0);

    int dropped_something = 0;
    for (size_t i = 0; i < res.trace_count; i++) {
        CHECK(res.trace[i].round < res.rounds_run);
        CHECK(res.trace[i].mu_star >= 0.0);
        CHECK(res.trace[i].member_count > 0);
        if (!res.trace[i].kept) dropped_something = 1;
    }
    CHECK(dropped_something);      /* with 58 of 64 inert, it must drop some */

    morris_bifurcate_free(&res);
    return 1;
}

/* An eval callback that fails must surface as an error, not a partial answer. */
static int bif_eval_fail(void *ctx, const doe_space_t *sp, const double *u,
                         size_t npoints, size_t k, double *responses, char *err) {
    (void)ctx; (void)sp; (void)u; (void)npoints; (void)k; (void)responses;
    snprintf(err, DOE_ERR_SIZE, "model blew up");
    return -1;
}

static int test_bifurcate_propagates_eval_failure(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(make_bif_space(&sp, err) == 0);
    morris_bifurcate_opts_t opts;
    memset(&opts, 0, sizeof opts);

    morris_bifurcate_result_t res;
    memset(err, 'A', sizeof err);
    CHECK(morris_bifurcate(&sp, &opts, bif_eval_fail, NULL, &res, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "blew up") != NULL);
    return 1;
}

int main(void) {
    printf("morris tests\n");
    RUN_TEST(test_bifurcate_finds_planted_factors);
    RUN_TEST(test_bifurcate_cost_is_bounded_and_predicted);
    RUN_TEST(test_bifurcate_trace);
    RUN_TEST(test_bifurcate_propagates_eval_failure);
    RUN_TEST(test_group_singletons_equal_per_factor);
    RUN_TEST(test_group_opposing_signs_do_not_cancel);
    RUN_TEST(test_group_cost_model);
    RUN_TEST(test_group_design_structure);
    RUN_TEST(test_group_determinism_and_order);
    RUN_TEST(test_group_rejects_bad_input);
    RUN_TEST(test_linear_ranking);
    RUN_TEST(test_interaction_flag);
    RUN_TEST(test_design_determinism);
    RUN_TEST(test_design_build_overflow);
    RUN_TEST(test_analyze_rejects_nonfinite);
    return TEST_SUMMARY();
}
