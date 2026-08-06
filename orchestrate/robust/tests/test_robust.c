/*
 * test_robust.c — exercises the orchestrated funnel end to end with in-process
 * evaluators (no shell): screening drops the right factors, Sobol runs on the
 * survivors, results are reproducible, and the reports are self-contained.
 */

#define _POSIX_C_SOURCE 200809L

#include "robust.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/* in-process evaluator: scale each design point and apply an analytic function */
typedef double (*scalar_fn)(const double *x, size_t k);
typedef struct { scalar_fn f; } tctx_t;

static int test_run(void *ctx, const doe_space_t *space, const double *u,
                    size_t npoints, double *responses, char *err) {
    (void)err;
    tctx_t *c = (tctx_t *)ctx;
    size_t k = space->factor_count;
    double x[DOE_MAX_FACTORS];
    for (size_t i = 0; i < npoints; i++) {
        for (size_t j = 0; j < k; j++) x[j] = doe_factor_scale(&space->factors[j], u[i * k + j]);
        responses[i] = c->f(x, k);
    }
    return 0;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t n = fread(b, 1, (size_t)sz, f);
    fclose(f);
    b[n] = '\0';
    return b;
}

/* y = 10*x0 + 5*x1 + 0*x2 + 0*x3 — additive; x2,x3 inert. */
static double f_additive(const double *x, size_t k) {
    (void)k;
    return 10.0 * x[0] + 5.0 * x[1] + 0.0 * x[2] + 0.0 * x[3];
}

static const char *SP4 =
    "factors:\n  x0: 0,1\n  x1: 0,1\n  x2: 0,1\n  x3: 0,1\n"
    "seed: 7\n  trajectories: 20\n  grid_levels: 4\n  samples: 2048\n";

static int test_funnel_screen_and_attribute(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(SP4, &sp, err) == 0);

    tctx_t c = { .f = f_additive };
    robust_result_t r;
    CHECK(robust_funnel(&sp, test_run, &c, ROBUST_KEEP_FRACTION, &r, err) == 0);

    /* screening: keep x0,x1; drop x2,x3 */
    CHECK(r.k == 4);
    CHECK(r.keep[0] && r.keep[1] && !r.keep[2] && !r.keep[3]);
    CHECK(r.n_survivors == 2);
    CHECK_DBL(r.effects[0].mu_star, 10.0, 1e-9);
    CHECK_DBL(r.effects[1].mu_star,  5.0, 1e-9);
    CHECK_DBL(r.effects[2].mu_star,  0.0, 1e-9);

    /* attribution runs on exactly the survivors, in order */
    CHECK(r.n_indices == 2);
    CHECK(strcmp(r.indices[0].name, "x0") == 0);
    CHECK(strcmp(r.indices[1].name, "x1") == 0);
    CHECK_DBL(r.indices[0].s1, 0.8, 0.06);     /* 10^2 / (10^2+5^2) */
    CHECK_DBL(r.indices[1].s1, 0.2, 0.06);     /*  5^2 / (10^2+5^2) */
    CHECK(fabs((r.indices[0].s1 + r.indices[1].s1) - 1.0) < 0.08);  /* additive */

    robust_result_free(&r);
    return 1;
}

/* y = 8*x0*x1 + 0*x2 — interaction between x0,x1; x2 inert. */
static double f_interaction(const double *x, size_t k) {
    (void)k;
    return 8.0 * x[0] * x[1] + 0.0 * x[2];
}

static int test_funnel_interaction(void) {
    const char *s =
        "factors:\n  x0: 0,1\n  x1: 0,1\n  x2: 0,1\n"
        "seed: 3\n  trajectories: 30\n  grid_levels: 4\n  samples: 4096\n";
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    tctx_t c = { .f = f_interaction };
    robust_result_t r;
    CHECK(robust_funnel(&sp, test_run, &c, ROBUST_KEEP_FRACTION, &r, err) == 0);

    CHECK(r.keep[0] && r.keep[1] && !r.keep[2]);   /* x2 dropped */
    CHECK(r.n_survivors == 2);
    CHECK(r.n_indices == 2);
    /* both survivors act through interaction: ST clearly exceeds S1 */
    CHECK(r.indices[0].st - r.indices[0].s1 > 0.05);
    CHECK(r.indices[1].st - r.indices[1].s1 > 0.05);

    robust_result_free(&r);
    return 1;
}

static int test_funnel_determinism(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(SP4, &sp, err) == 0);
    tctx_t c = { .f = f_additive };

    robust_result_t r1, r2;
    CHECK(robust_funnel(&sp, test_run, &c, ROBUST_KEEP_FRACTION, &r1, err) == 0);
    CHECK(robust_funnel(&sp, test_run, &c, ROBUST_KEEP_FRACTION, &r2, err) == 0);

    CHECK(r1.n_survivors == r2.n_survivors);
    for (size_t i = 0; i < r1.k; i++) CHECK(r1.keep[i] == r2.keep[i]);
    CHECK(r1.n_indices == r2.n_indices);
    for (size_t i = 0; i < r1.n_indices; i++) {
        CHECK(r1.indices[i].s1 == r2.indices[i].s1);   /* exact: deterministic */
        CHECK(r1.indices[i].st == r2.indices[i].st);
    }
    robust_result_free(&r1);
    robust_result_free(&r2);
    return 1;
}

static int test_report_outputs(void) {
    doe_space_t sp; char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(SP4, &sp, err) == 0);
    tctx_t c = { .f = f_additive };
    robust_result_t r;
    CHECK(robust_funnel(&sp, test_run, &c, ROBUST_KEEP_FRACTION, &r, err) == 0);

    const char *html = "build/robust_test_report.html";
    const char *json = "build/robust_test_report.json";
    const char *tgu  = "build/robust_test_survivors.tgu";

    CHECK(robust_write_html(&r, html, err) == 0);
    char *h = slurp(html);
    CHECK(h != NULL);
    CHECK(strncmp(h, "<!DOCTYPE", 9) == 0);
    CHECK(strstr(h, "Robust funnel") != NULL);
    CHECK(strstr(h, "x0") != NULL);
    CHECK(strstr(h, "KEEP") != NULL);
    CHECK(strstr(h, "http") == NULL);       /* self-contained: no external refs */
    CHECK(strstr(h, "<script") == NULL);
    CHECK(strstr(h, "<link") == NULL);
    free(h);

    CHECK(robust_write_json(&r, json, err) == 0);
    char *j = slurp(json);
    CHECK(j != NULL);
    CHECK(strstr(j, "\"morris\"") != NULL);
    CHECK(strstr(j, "\"sobol\"") != NULL);
    free(j);

    CHECK(robust_write_tgu(&r, tgu, err) == 0);
    char *t = slurp(tgu);
    CHECK(t != NULL);
    CHECK(strstr(t, "factors:") != NULL);
    CHECK(strstr(t, "x0") != NULL);
    free(t);

    remove(html);
    remove(json);
    remove(tgu);
    robust_result_free(&r);
    return 1;
}

/* H2 — an adversarial factor name must be HTML-escaped in the report, not
 * rendered as a live tag. */
static int test_report_escapes_hostile_name(void) {
    morris_effect_t eff;
    memset(&eff, 0, sizeof eff);
    strcpy(eff.name, "<script>x");
    eff.mu_star = 1.0;
    int keep = 1;

    robust_result_t r;
    memset(&r, 0, sizeof r);
    r.k = 1; r.effects = &eff; r.keep = &keep; r.n_survivors = 1; r.keep_fraction = 0.1;

    const char *html = "build/robust_xss_test.html";
    char err[DOE_ERR_SIZE];
    CHECK(robust_write_html(&r, html, err) == 0);
    char *h = slurp(html);
    CHECK(h != NULL);
    CHECK(strstr(h, "&lt;script&gt;x") != NULL);   /* escaped */
    CHECK(strstr(h, "<script>x") == NULL);          /* raw tag absent */
    free(h);
    remove(html);
    /* eff/keep are stack-allocated — do not robust_result_free */
    return 1;
}

/* H8 — the survivors .tgu robust writes must be accepted by `taguchi validate`:
 * the funnel→bench hand-off round-trips through taguchi's (already hardened)
 * parser. Covers all three writer branches: linear, log, categorical. */
static const char *find_taguchi(void) {
    static const char *cands[] = { "build/bin/taguchi", "taguchi/build/taguchi" };
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        if (access(cands[i], X_OK) == 0) return cands[i];
    }
    return NULL;
}

static int test_tgu_roundtrip_taguchi_validate(void) {
    const char *tg = find_taguchi();
    CHECK(tg != NULL);   /* needs the taguchi binary — `make taguchi` first */

    robust_result_t r;
    memset(&r, 0, sizeof r);
    doe_space_t *s = &r.subspace;
    s->factor_count = 3;
    strcpy(s->factors[0].name, "temp");
    s->factors[0].scale = DOE_LINEAR; s->factors[0].lo = 20; s->factors[0].hi = 80;
    strcpy(s->factors[1].name, "conc");
    s->factors[1].scale = DOE_LOG; s->factors[1].lo = 1e-6; s->factors[1].hi = 1;
    strcpy(s->factors[2].name, "mode");
    s->factors[2].scale = DOE_CATEGORICAL; s->factors[2].level_count = 3;
    strcpy(s->factors[2].levels[0], "fast");
    strcpy(s->factors[2].levels[1], "slow");
    strcpy(s->factors[2].levels[2], "turbo_v2");

    const char *tgu = "build/robust_h8_roundtrip.tgu";
    char err[DOE_ERR_SIZE];
    CHECK(robust_write_tgu(&r, tgu, err) == 0);

    char cmd[512];
    snprintf(cmd, sizeof cmd, "./%s validate %s > /dev/null 2>&1", tg, tgu);
    CHECK(system(cmd) == 0);

    remove(tgu);
    /* r.subspace is inline and nothing was allocated — no robust_result_free */
    return 1;
}

int main(void) {
    printf("robust funnel tests\n");
    RUN_TEST(test_funnel_screen_and_attribute);
    RUN_TEST(test_funnel_interaction);
    RUN_TEST(test_funnel_determinism);
    RUN_TEST(test_report_outputs);
    RUN_TEST(test_report_escapes_hostile_name);
    RUN_TEST(test_tgu_roundtrip_taguchi_validate);
    return TEST_SUMMARY();
}
