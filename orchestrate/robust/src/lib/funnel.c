/*
 * funnel.c — the orchestration core: screen -> subspace -> attribute.
 *
 * Each stage builds its design, hands the design points to an injectable
 * evaluator (`run`) to get responses, then analyzes. The Sobol stage embeds the
 * survivor sub-design into the full factor space with dropped factors held at
 * the midpoint, so the evaluator always sees a complete factor vector.
 */

#include "robust.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void robust_result_free(robust_result_t *r) {
    if (!r) return;
    free(r->effects);
    free(r->keep);
    free(r->indices);
    r->effects = NULL;
    r->keep = NULL;
    r->indices = NULL;
}

int robust_screen(const doe_space_t *space, robust_run_fn run, void *ctx,
                  double keep_fraction, morris_effect_t **effects_out,
                  int *keep, size_t *n_survivors, char *err) {
    morris_design_t d;
    if (morris_design_build(space, &d, err) != 0) return -1;

    double *responses = malloc(d.npoints * sizeof *responses);
    if (!responses) {
        morris_design_free(&d);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    int rc = run(ctx, space, d.u, d.npoints, responses, err);
    if (rc != 0) { free(responses); morris_design_free(&d); return -1; }

    morris_effect_t *eff = NULL;
    size_t neff = 0;
    rc = morris_analyze(space, responses, d.npoints, &eff, &neff, err);
    free(responses);
    morris_design_free(&d);
    if (rc != 0) return -1;

    double maxmu = 0.0;
    for (size_t i = 0; i < neff; i++) {
        if (eff[i].mu_star > maxmu) maxmu = eff[i].mu_star;
    }
    double thresh = keep_fraction * maxmu;

    size_t nsurv = 0;
    for (size_t i = 0; i < neff; i++) {
        keep[i] = (maxmu > 0.0 && eff[i].mu_star >= thresh) ? 1 : 0;
        if (keep[i]) nsurv++;
    }
    /* never drop everything — keep the single most important factor */
    if (nsurv == 0 && neff > 0) {
        size_t arg = 0;
        for (size_t i = 1; i < neff; i++) {
            if (eff[i].mu_star > eff[arg].mu_star) arg = i;
        }
        keep[arg] = 1;
        nsurv = 1;
    }

    *effects_out = eff;
    *n_survivors = nsurv;
    return 0;
}

int robust_subspace(const doe_space_t *space, const int *keep,
                    doe_space_t *sub, char *err) {
    memset(sub, 0, sizeof *sub);
    sub->seed = space->seed;
    sub->trajectories = space->trajectories;
    sub->grid_levels = space->grid_levels;
    sub->samples = space->samples;
    sub->second_order = space->second_order;

    size_t j = 0;
    for (size_t i = 0; i < space->factor_count; i++) {
        if (keep[i]) sub->factors[j++] = space->factors[i];
    }
    sub->factor_count = j;
    if (j == 0) {
        snprintf(err, DOE_ERR_SIZE, "no survivors to attribute");
        return -1;
    }
    return 0;
}

int robust_attribute(const doe_space_t *space, const doe_space_t *sub, const int *keep,
                     robust_run_fn run, void *ctx,
                     sobol_index_t **indices, size_t *n, char *err) {
    sobol_design_t d;
    if (sobol_design_build(sub, &d, err) != 0) return -1;

    size_t kfull = space->factor_count;
    size_t ksub = sub->factor_count;
    size_t npoints = d.npoints;

    /* survivor column j -> full factor index */
    size_t map[DOE_MAX_FACTORS];
    { size_t j = 0; for (size_t i = 0; i < kfull; i++) if (keep[i]) map[j++] = i; }

    double *full = malloc(npoints * kfull * sizeof *full);
    double *responses = malloc(npoints * sizeof *responses);
    if (!full || !responses) {
        free(full); free(responses); sobol_design_free(&d);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    double subu[DOE_MAX_FACTORS];
    for (size_t r = 0; r < npoints; r++) {
        sobol_point(&d, r, subu);
        for (size_t fcol = 0; fcol < kfull; fcol++) full[r * kfull + fcol] = 0.5;  /* dropped */
        for (size_t j = 0; j < ksub; j++) full[r * kfull + map[j]] = subu[j];       /* survivors */
    }

    int rc = run(ctx, space, full, npoints, responses, err);
    free(full);
    if (rc != 0) { free(responses); sobol_design_free(&d); return -1; }

    sobol_index_t *idx = NULL;
    size_t nidx = 0;
    rc = sobol_analyze(sub, responses, npoints, &idx, &nidx, err);
    free(responses);
    sobol_design_free(&d);
    if (rc != 0) return -1;

    *indices = idx;
    *n = nidx;
    return 0;
}

int robust_funnel(const doe_space_t *space, robust_run_fn run, void *ctx,
                  double keep_fraction, robust_result_t *result, char *err) {
    memset(result, 0, sizeof *result);
    result->k = space->factor_count;
    result->keep_fraction = keep_fraction;

    int *keep = malloc(space->factor_count * sizeof *keep);
    if (!keep) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }

    morris_effect_t *eff = NULL;
    size_t nsurv = 0;
    if (robust_screen(space, run, ctx, keep_fraction, &eff, keep, &nsurv, err) != 0) {
        free(keep);
        return -1;
    }
    result->effects = eff;
    result->keep = keep;
    result->n_survivors = nsurv;

    if (robust_subspace(space, keep, &result->subspace, err) != 0) return -1;

    sobol_index_t *idx = NULL;
    size_t nidx = 0;
    if (robust_attribute(space, &result->subspace, keep, run, ctx, &idx, &nidx, err) != 0) {
        return -1;
    }
    result->indices = idx;
    result->n_indices = nidx;
    return 0;
}
