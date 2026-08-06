/*
 * morris.c — trajectory construction and elementary-effects analysis.
 *
 * Trajectory (Morris 1991): grid spacing 1/(p-1), Delta = p/(2(p-1)). Each
 * factor's base is drawn from the lower half {0, .., 1-Delta}; with a random
 * direction d in {+1,-1} the factor visits {base, base+Delta} so every point
 * stays in [0,1]. Factors move in a random order, once each, per trajectory.
 */

#include "morris.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static size_t rng_below(doe_rng_t *rng, size_t n) {
    size_t x = (size_t)(doe_rng_uniform(rng) * (double)n);
    return x >= n ? n - 1 : x;   /* guard against u very close to 1.0 */
}

size_t morris_npoints(const doe_space_t *space) {
    return space->trajectories * (space->factor_count + 1);
}

int morris_design_build(const doe_space_t *space, morris_design_t *d, char *err) {
    size_t k = space->factor_count;
    size_t r = space->trajectories;
    size_t p = space->grid_levels;

    memset(d, 0, sizeof *d);
    if (k == 0) { snprintf(err, DOE_ERR_SIZE, "no factors"); return -1; }
    if (r < 1) { snprintf(err, DOE_ERR_SIZE, "trajectories must be >= 1"); return -1; }
    if (p < 2 || p % 2 != 0) {
        snprintf(err, DOE_ERR_SIZE, "grid_levels must be an even number >= 2 (got %zu)", p);
        return -1;
    }

    const double step  = 1.0 / (double)(p - 1);
    const double Delta = (double)p / (2.0 * (double)(p - 1));
    const size_t nbase = p / 2;                 /* base indices 0 .. nbase-1 */
    size_t npoints, ucount, rkcount;
    if (!doe_size_mul_ok(r, k + 1, &npoints) ||
        !doe_size_mul_ok(npoints, k, &ucount) ||
        !doe_size_mul_ok(r, k, &rkcount)) {
        snprintf(err, DOE_ERR_SIZE, "design too large (size overflow)");
        return -1;
    }

    double *u            = malloc(ucount * sizeof *u);
    size_t *moved_factor = malloc(rkcount * sizeof *moved_factor);
    double *moved_delta  = malloc(rkcount * sizeof *moved_delta);
    if (!u || !moved_factor || !moved_delta) {
        free(u); free(moved_factor); free(moved_delta);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    doe_rng_t rng;
    doe_rng_seed(&rng, space->seed);

    size_t perm[DOE_MAX_FACTORS];
    double base[DOE_MAX_FACTORS];
    int    dir[DOE_MAX_FACTORS];

    for (size_t t = 0; t < r; t++) {
        /* random permutation of factors (Fisher-Yates) */
        for (size_t i = 0; i < k; i++) perm[i] = i;
        for (size_t i = k; i > 1; i--) {
            size_t j = rng_below(&rng, i);
            size_t tmp = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = tmp;
        }
        /* per-step base value and direction */
        for (size_t s = 0; s < k; s++) {
            base[s] = (double)rng_below(&rng, nbase) * step;
            dir[s]  = (doe_rng_uniform(&rng) < 0.5) ? -1 : +1;
        }

        /* point 0: each factor at its initial value */
        double *p0 = &u[(t * (k + 1) + 0) * k];
        for (size_t s = 0; s < k; s++) {
            size_t j = perm[s];
            p0[j] = (dir[s] > 0) ? base[s] : base[s] + Delta;
        }

        /* points 1..k: copy previous, move one factor */
        for (size_t s = 0; s < k; s++) {
            double *prev = &u[(t * (k + 1) + s) * k];
            double *cur  = &u[(t * (k + 1) + s + 1) * k];
            memcpy(cur, prev, k * sizeof *cur);

            size_t j = perm[s];
            double delta = (double)dir[s] * Delta;
            cur[j] = prev[j] + delta;

            moved_factor[t * k + s] = j;
            moved_delta[t * k + s]  = delta;
        }
    }

    d->k = k;
    d->r = r;
    d->npoints = npoints;
    d->u = u;
    d->moved_factor = moved_factor;
    d->moved_delta = moved_delta;
    return 0;
}

void morris_design_free(morris_design_t *d) {
    if (!d) return;
    free(d->u);
    free(d->moved_factor);
    free(d->moved_delta);
    memset(d, 0, sizeof *d);
}

int morris_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                   morris_effect_t **out, size_t *count, char *err) {
    morris_design_t d;
    if (morris_design_build(space, &d, err) != 0) return -1;

    if (nresp < d.npoints) {
        snprintf(err, DOE_ERR_SIZE, "need %zu responses, got %zu", d.npoints, nresp);
        morris_design_free(&d);
        return -1;
    }

    size_t k = d.k, r = d.r;
    double *ee = malloc(k * r * sizeof *ee);     /* k factors x r effects each */
    size_t *cnt = calloc(k, sizeof *cnt);
    morris_effect_t *eff = calloc(k, sizeof *eff);
    if (!ee || !cnt || !eff) {
        free(ee); free(cnt); free(eff);
        morris_design_free(&d);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    int rc = 0;
    for (size_t t = 0; t < r && rc == 0; t++) {
        for (size_t s = 0; s < k; s++) {
            size_t i0 = t * (k + 1) + s;
            size_t i1 = i0 + 1;
            double y0 = responses[i0], y1 = responses[i1];
            if (!isfinite(y0) || !isfinite(y1)) {
                snprintf(err, DOE_ERR_SIZE, "missing or non-finite response for run %zu or %zu", i0 + 1, i1 + 1);
                rc = -1;
                break;
            }
            size_t j = d.moved_factor[t * k + s];
            double delta = d.moved_delta[t * k + s];
            ee[j * r + cnt[j]] = (y1 - y0) / delta;
            cnt[j]++;
        }
    }

    if (rc == 0) {
        for (size_t j = 0; j < k; j++) {
            const double *e = &ee[j * r];
            size_t n = cnt[j];
            double sum_abs = 0.0;
            for (size_t i = 0; i < n; i++) sum_abs += fabs(e[i]);

            strncpy(eff[j].name, space->factors[j].name, DOE_MAX_NAME - 1);
            eff[j].mu      = doe_mean(e, n);
            eff[j].mu_star = (n > 0) ? sum_abs / (double)n : 0.0;
            eff[j].sigma   = doe_std(e, n);
        }
        *out = eff;
        *count = k;
    } else {
        free(eff);
    }

    free(ee);
    free(cnt);
    morris_design_free(&d);
    return rc;
}
