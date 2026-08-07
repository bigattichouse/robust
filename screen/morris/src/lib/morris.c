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
        /* Bootstrap over trajectories: one factor's r elementary effects come
         * from r independent trajectories, so those are the unit to resample.
         * Seeded off the space seed so the interval is reproducible from the
         * .space alone, like every other number this tool reports. */
        enum { NBOOT = 1000 };
        doe_rng_t brng;
        doe_rng_seed(&brng, space->seed ^ 0xB007D0E5ULL);
        double *boot = malloc(NBOOT * sizeof *boot);

        for (size_t j = 0; j < k; j++) {
            const double *e = &ee[j * r];
            size_t n = cnt[j];
            double sum_abs = 0.0;
            for (size_t i = 0; i < n; i++) sum_abs += fabs(e[i]);

            strncpy(eff[j].name, space->factors[j].name, DOE_MAX_NAME - 1);
            eff[j].mu      = doe_mean(e, n);
            eff[j].mu_star = (n > 0) ? sum_abs / (double)n : 0.0;
            eff[j].sigma   = doe_std(e, n);
            eff[j].mu_star_lo = eff[j].mu_star;
            eff[j].mu_star_hi = eff[j].mu_star;

            if (boot && n > 1) {
                for (int b = 0; b < NBOOT; b++) {
                    double acc = 0.0;
                    for (size_t i = 0; i < n; i++)
                        acc += fabs(e[rng_below(&brng, n)]);
                    boot[b] = acc / (double)n;
                }
                for (int a = 0; a < NBOOT; a++)          /* insertion sort */
                    for (int b = a + 1; b < NBOOT; b++)
                        if (boot[b] < boot[a]) { double t = boot[a]; boot[a] = boot[b]; boot[b] = t; }
                eff[j].mu_star_lo = boot[(int)(0.025 * NBOOT)];
                eff[j].mu_star_hi = boot[(int)(0.975 * NBOOT)];
            }
        }
        free(boot);
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

/* ============================================================================
 * Group screening. See morris.h and spec/morris-groups.bp.
 * ============================================================================ */

size_t morris_group_npoints(const doe_space_t *space) {
    return space->trajectories * (space->group_count + 1);
}

int morris_group_design_build(const doe_space_t *space,
                              morris_group_design_t *d, char *err) {
    size_t k = space->factor_count;
    size_t G = space->group_count;
    size_t r = space->trajectories;
    size_t p = space->grid_levels;

    memset(d, 0, sizeof *d);
    if (k == 0) { snprintf(err, DOE_ERR_SIZE, "no factors"); return -1; }
    if (G < 2) {
        snprintf(err, DOE_ERR_SIZE, "group screening needs >= 2 groups (got %zu)", G);
        return -1;
    }
    if (r < 1) { snprintf(err, DOE_ERR_SIZE, "trajectories must be >= 1"); return -1; }
    if (p < 2 || p % 2 != 0) {
        snprintf(err, DOE_ERR_SIZE, "grid_levels must be an even number >= 2 (got %zu)", p);
        return -1;
    }

    const double step  = 1.0 / (double)(p - 1);
    const double Delta = (double)p / (2.0 * (double)(p - 1));
    const size_t nbase = p / 2;
    size_t npoints, ucount, rgcount;
    if (!doe_size_mul_ok(r, G + 1, &npoints) ||
        !doe_size_mul_ok(npoints, k, &ucount) ||
        !doe_size_mul_ok(r, G, &rgcount)) {
        snprintf(err, DOE_ERR_SIZE, "design too large (size overflow)");
        return -1;
    }

    double *u           = malloc(ucount * sizeof *u);
    size_t *moved_group = malloc(rgcount * sizeof *moved_group);
    if (!u || !moved_group) {
        free(u); free(moved_group);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    doe_rng_t rng;
    doe_rng_seed(&rng, space->seed);

    size_t perm[DOE_MAX_GROUPS];
    double base[DOE_MAX_FACTORS];
    int    dir[DOE_MAX_FACTORS];

    for (size_t t = 0; t < r; t++) {
        /* random permutation of GROUPS (Fisher-Yates) */
        for (size_t i = 0; i < G; i++) perm[i] = i;
        for (size_t i = G; i > 1; i--) {
            size_t j = rng_below(&rng, i);
            size_t tmp = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = tmp;
        }
        /*
         * Base value and direction are drawn PER FACTOR, not per group. That
         * is what puts opposing movements inside a single group, exercising
         * the case that breaks sequential bifurcation. One direction per group
         * would make the design quietly easier than reality.
         */
        for (size_t i = 0; i < k; i++) {
            base[i] = (double)rng_below(&rng, nbase) * step;
            dir[i]  = (doe_rng_uniform(&rng) < 0.5) ? -1 : +1;
        }

        /* point 0 */
        double *p0 = &u[(t * (G + 1) + 0) * k];
        for (size_t i = 0; i < k; i++)
            p0[i] = (dir[i] > 0) ? base[i] : base[i] + Delta;

        /* points 1..G: copy previous, move every member of one group */
        for (size_t s = 0; s < G; s++) {
            double *prev = &u[(t * (G + 1) + s) * k];
            double *cur  = &u[(t * (G + 1) + s + 1) * k];
            memcpy(cur, prev, k * sizeof *cur);

            const doe_group_t *grp = &space->groups[perm[s]];
            for (size_t i = 0; i < k; i++)
                if (grp->members[i]) cur[i] = prev[i] + (double)dir[i] * Delta;

            moved_group[t * G + s] = perm[s];
        }
    }

    d->k = k; d->G = G; d->r = r;
    d->npoints = npoints;
    d->u = u;
    d->moved_group = moved_group;
    d->delta = Delta;
    return 0;
}

void morris_group_design_free(morris_group_design_t *d) {
    if (!d) return;
    free(d->u);
    free(d->moved_group);
    memset(d, 0, sizeof *d);
}

int morris_group_analyze(const doe_space_t *space, const double *responses,
                         size_t nresp, morris_group_effect_t **out,
                         size_t *count, char *err) {
    morris_group_design_t d;
    if (morris_group_design_build(space, &d, err) != 0) return -1;

    if (nresp < d.npoints) {
        snprintf(err, DOE_ERR_SIZE, "need %zu responses, got %zu", d.npoints, nresp);
        morris_group_design_free(&d);
        return -1;
    }

    size_t G = d.G, r = d.r;
    double *ee = malloc(G * r * sizeof *ee);     /* |d_u| per group per traj */
    size_t *cnt = calloc(G, sizeof *cnt);
    morris_group_effect_t *eff = calloc(G, sizeof *eff);
    if (!ee || !cnt || !eff) {
        free(ee); free(cnt); free(eff);
        morris_group_design_free(&d);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    int rc = 0;
    for (size_t t = 0; t < r && rc == 0; t++) {
        for (size_t s = 0; s < G; s++) {
            size_t i0 = t * (G + 1) + s;
            size_t i1 = i0 + 1;
            double y0 = responses[i0], y1 = responses[i1];
            if (!isfinite(y0) || !isfinite(y1)) {
                snprintf(err, DOE_ERR_SIZE,
                         "missing or non-finite response for run %zu or %zu", i0 + 1, i1 + 1);
                rc = -1;
                break;
            }
            /* Campolongo 2007 Sec. 3.3: the ABSOLUTE group effect. */
            size_t g = d.moved_group[t * G + s];
            ee[g * r + cnt[g]] = fabs(y1 - y0) / d.delta;
            cnt[g]++;
        }
    }

    if (rc == 0) {
        for (size_t g = 0; g < G; g++) {
            const double *e = &ee[g * r];
            size_t n = cnt[g];
            double sum = 0.0;
            for (size_t i = 0; i < n; i++) sum += e[i];

            strncpy(eff[g].name, space->groups[g].name, DOE_MAX_NAME - 1);
            eff[g].mu_star      = (n > 0) ? sum / (double)n : 0.0;
            eff[g].sigma        = doe_std(e, n);
            eff[g].member_count = space->groups[g].member_count;
        }
        *out = eff;
        *count = G;
    } else {
        free(eff);
    }

    free(ee);
    free(cnt);
    morris_group_design_free(&d);
    return rc;
}

/* ============================================================================
 * Recursive splitting. See morris.h and spec/morris-groups.bp.
 * ============================================================================ */

#define BIFURCATE_GAP_WARN 1.05   /* cut inside this ratio is a near-tie */

static size_t default_rounds(size_t k) {
    size_t r = 1;
    while ((size_t)1 << r < k) r++;
    return r + 1;
}

static size_t default_initial_groups(size_t k) {
    /* ~sqrt(k), clamped: enough groups that the first round is informative,
     * few enough that it is cheap. */
    size_t g = 2;
    while (g * g < k) g++;
    if (g > k) g = k;
    if (g > DOE_MAX_GROUPS) g = DOE_MAX_GROUPS;
    return g < 2 ? 2 : g;
}

size_t morris_bifurcate_budget(const doe_space_t *space,
                               const morris_bifurcate_opts_t *opts) {
    size_t k = space->factor_count;
    if (k == 0) return 0;
    size_t rounds = (opts && opts->max_rounds) ? opts->max_rounds : default_rounds(k);
    size_t G = space->group_count ? space->group_count
             : (opts && opts->initial_groups ? opts->initial_groups
                                             : default_initial_groups(k));
    if (G > k) G = k;

    size_t total = 0;
    for (size_t n = 0; n < rounds; n++) {
        total += space->trajectories * (G + 1);
        if (G >= k) break;              /* already singletons */
        G *= 2;                          /* worst case: everything survives */
        if (G > k) G = k;
    }
    return total;
}

/* Build a space carrying `part` as its groups: section. */
static void set_partition(doe_space_t *sp, const doe_group_t *part, size_t G) {
    sp->group_count = G;
    for (size_t g = 0; g < G; g++) sp->groups[g] = part[g];
}

int morris_bifurcate(const doe_space_t *space,
                     const morris_bifurcate_opts_t *opts,
                     morris_eval_fn eval, void *ctx,
                     morris_bifurcate_result_t *out, char *err) {
    if (!space || !eval || !out) {
        snprintf(err, DOE_ERR_SIZE, "null input to morris_bifurcate");
        return -1;
    }
    size_t k = space->factor_count;
    if (k == 0) { snprintf(err, DOE_ERR_SIZE, "no factors"); return -1; }

    double keep_share = (opts && opts->keep_share > 0.0) ? opts->keep_share : 0.9;
    if (keep_share > 1.0) keep_share = 1.0;
    size_t max_rounds = (opts && opts->max_rounds) ? opts->max_rounds : default_rounds(k);

    memset(out, 0, sizeof *out);
    out->predicted_max = morris_bifurcate_budget(space, opts);

    /*
     * Flag the low-trajectory false-negative risk. Not an error: with factors
     * spread across groups, r=10 was clean in every seed tried. It only bites
     * when equal-and-opposite factors share a group, which the caller cannot
     * know in advance -- which is exactly why it is worth saying out loud.
     */
    if (space->trajectories < MORRIS_BIFURCATE_MIN_TRAJECTORIES &&
        space->factor_count > 1) {
        out->low_trajectories = 1;
    }

    doe_space_t *work = malloc(sizeof *work);
    doe_group_t *part = malloc(DOE_MAX_GROUPS * sizeof *part);
    doe_group_t *next = malloc(DOE_MAX_GROUPS * sizeof *next);
    if (!work || !part || !next) {
        free(work); free(part); free(next);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }
    *work = *space;

    /* Initial partition: the .space's own groups, or contiguous blocks. */
    size_t G;
    if (space->group_count > 0) {
        G = space->group_count;
        for (size_t g = 0; g < G; g++) part[g] = space->groups[g];
    } else {
        G = (opts && opts->initial_groups) ? opts->initial_groups
                                           : default_initial_groups(k);
        if (G > k) G = k;
        if (G > DOE_MAX_GROUPS) G = DOE_MAX_GROUPS;
        memset(part, 0, G * sizeof *part);
        for (size_t i = 0; i < k; i++) {
            size_t g = (i * G) / k;                 /* contiguous blocks */
            if (g >= G) g = G - 1;
            part[g].members[i] = true;
            part[g].member_count++;
        }
        for (size_t g = 0; g < G; g++)
            snprintf(part[g].name, DOE_MAX_NAME, "g%zu", g);
    }

    size_t trace_cap = 64, trace_n = 0;
    morris_bifurcate_step_t *trace = malloc(trace_cap * sizeof *trace);
    if (!trace) {
        free(work); free(part); free(next);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    int rc = 0;
    size_t round = 0;
    for (; round < max_rounds; round++) {
        set_partition(work, part, G);
        /* Fresh trajectories each round; still a pure function of the seed. */
        work->seed = space->seed ^ (0x9E3779B97F4A7C15ULL * (round + 1));

        morris_group_design_t d;
        if (morris_group_design_build(work, &d, err) != 0) { rc = -1; break; }

        double *y = malloc(d.npoints * sizeof *y);
        if (!y) { morris_group_design_free(&d);
                  snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }

        if (eval(ctx, work, d.u, d.npoints, d.k, y, err) != 0) {
            free(y); morris_group_design_free(&d); rc = -1; break;
        }
        out->evaluations += d.npoints;

        morris_group_effect_t *eff = NULL; size_t n = 0;
        if (morris_group_analyze(work, y, d.npoints, &eff, &n, err) != 0) {
            free(y); morris_group_design_free(&d); rc = -1; break;
        }
        free(y);
        morris_group_design_free(&d);

        /* Rank by mu*, then keep until the cumulative share is reached. */
        size_t *order = malloc(n * sizeof *order);
        if (!order) { free(eff); snprintf(err, DOE_ERR_SIZE, "out of memory");
                      rc = -1; break; }
        for (size_t i = 0; i < n; i++) order[i] = i;
        for (size_t i = 0; i < n; i++)
            for (size_t j = i + 1; j < n; j++)
                if (eff[order[j]].mu_star > eff[order[i]].mu_star) {
                    size_t t = order[i]; order[i] = order[j]; order[j] = t;
                }

        double total = 0.0;
        for (size_t i = 0; i < n; i++) total += eff[i].mu_star;

        size_t keep_upto = 1;                     /* always keep at least one */
        if (total > 0.0) {
            double cum = 0.0;
            for (size_t i = 0; i < n; i++) {
                cum += eff[order[i]].mu_star;
                keep_upto = i + 1;
                if (cum / total >= keep_share) break;
            }
        } else {
            keep_upto = n;   /* nothing moved: cannot discriminate, keep all */
        }

        /*
         * The cut-gap check. Validation check C measured that a keep/drop
         * boundary inside a near-tie never resolves, at any trajectory count.
         * Splitting further cannot help either, so stop and say so rather
         * than spend the next round pretending otherwise.
         */
        int tie_at_cut = 0;
        if (keep_upto < n) {
            double hi = eff[order[keep_upto - 1]].mu_star;
            double lo = eff[order[keep_upto]].mu_star;
            if (lo > 0.0 && hi / lo < BIFURCATE_GAP_WARN) tie_at_cut = 1;
        }

        /* Record the round, and build the surviving partition. */
        size_t nkept = 0;
        for (size_t i = 0; i < n; i++) {
            size_t gi = order[i];
            int kept = (i < keep_upto) || tie_at_cut;   /* a tie keeps both */
            if (trace_n == trace_cap) {
                size_t nc = trace_cap * 2;
                morris_bifurcate_step_t *t2 = realloc(trace, nc * sizeof *t2);
                /* Do NOT free here: the check after this loop owns the
                 * cleanup for both buffers. Freeing in both places was a
                 * use-after-free that -Werror caught. */
                if (!t2) { snprintf(err, DOE_ERR_SIZE, "out of memory");
                           rc = -1; break; }
                trace = t2; trace_cap = nc;
            }
            snprintf(trace[trace_n].group, DOE_MAX_NAME, "%s", eff[gi].name);
            trace[trace_n].round        = round;
            trace[trace_n].mu_star      = eff[gi].mu_star;
            trace[trace_n].member_count = eff[gi].member_count;
            trace[trace_n].kept         = kept;
            trace_n++;
            if (kept) nkept++;
        }
        if (rc != 0) { free(order); free(eff); break; }

        /* Survivors, in ranked order, split in two where possible. */
        size_t G2 = 0;
        int all_singletons = 1;
        for (size_t i = 0; i < n && G2 < DOE_MAX_GROUPS; i++) {
            if (!(i < keep_upto || tie_at_cut)) continue;
            const doe_group_t *g = &part[order[i]];
            if (g->member_count <= 1) {
                next[G2++] = *g;                       /* already a singleton */
                continue;
            }
            all_singletons = 0;
            size_t half = g->member_count / 2, seen = 0;
            doe_group_t a, b;
            memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            for (size_t f = 0; f < k; f++) {
                if (!g->members[f]) continue;
                if (seen < half) { a.members[f] = true; a.member_count++; }
                else             { b.members[f] = true; b.member_count++; }
                seen++;
            }
            snprintf(a.name, DOE_MAX_NAME, "%.*sA", DOE_MAX_NAME - 2, g->name);
            snprintf(b.name, DOE_MAX_NAME, "%.*sB", DOE_MAX_NAME - 2, g->name);
            if (G2 < DOE_MAX_GROUPS) next[G2++] = a;
            if (G2 < DOE_MAX_GROUPS) next[G2++] = b;
        }

        free(order);
        free(eff);

        out->rounds_run = round + 1;

        if (tie_at_cut) { out->stopped_on_tie = 1; }

        /* Stop when nothing can be split further, or the cut was a tie. */
        if (all_singletons || tie_at_cut || G2 < 2) {
            /* Survivors are whatever `next` holds. */
            for (size_t g = 0; g < G2; g++)
                for (size_t f = 0; f < k; f++)
                    if (next[g].members[f]) out->survivors[f] = true;
            G = G2;
            round++;
            break;
        }

        memcpy(part, next, G2 * sizeof *part);
        G = G2;
        (void)nkept;
    }

    if (rc == 0) {
        /* If the loop ran out of rounds, the current partition is the answer. */
        int any = 0;
        for (size_t f = 0; f < k; f++) if (out->survivors[f]) { any = 1; break; }
        if (!any) {
            for (size_t g = 0; g < G; g++)
                for (size_t f = 0; f < k; f++)
                    if (part[g].members[f]) out->survivors[f] = true;
        }
        for (size_t f = 0; f < k; f++) if (out->survivors[f]) out->survivor_count++;
        out->trace = trace;
        out->trace_count = trace_n;
    } else {
        free(trace);
    }

    free(work); free(part); free(next);
    return rc;
}

void morris_bifurcate_free(morris_bifurcate_result_t *r) {
    if (!r) return;
    free(r->trace);
    r->trace = NULL;
    r->trace_count = 0;
}
