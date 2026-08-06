/*
 * sobol.c — Saltelli design + Sobol index estimation with bootstrap CIs.
 *
 * Source, verified against the paper (sources/pdf/saltelli-2010-total-index-
 * estimator.pdf): Saltelli, Annoni, Azzini, Campolongo, Ratto & Tarantola
 * (2010), "Variance based sensitivity analysis of model output. Design and
 * estimator for the total sensitivity index", Comput. Phys. Comm. 181(2)
 * 259-270.
 *
 *   S_i  = 1/N sum_j f(B)_j ( f(A_B^(i))_j - f(A)_j ) / V
 *          Table 2 formula (b), p.262. Sec. 5.1 p.263 recommends this one
 *          specifically for the A, B, A_B^(i) triplet we use.
 *   S_Ti = 1/(2N) sum_j ( f(A)_j - f(A_B^(i))_j )^2 / V
 *          Table 2 formula (f), p.262 (Jansen 1999). The Table 2 caption
 *          calls it "the best practice so far for S_Ti"; Sec. 7 conclusion 1
 *          confirms it.
 *   A_B^(i) = A with column i taken from B (Table 1, p.260). Sec. 3 p.261
 *          prefers this triplet over the older B, A, B_A^(i). Cost N(k+2).
 *
 * Sec. 7 also concludes: radial sampling, n = 1, and quasi-random numbers.
 * The first two describe this design; the third is pending (M5).
 * Numerically validated against the g-function closed form in
 * validation/validate.c check E — `make validate`.
 */

#include "sobol.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define SOBOL_BOOTSTRAP 1000

static size_t rng_below(doe_rng_t *rng, size_t n) {
    size_t x = (size_t)(doe_rng_uniform(rng) * (double)n);
    return x >= n ? n - 1 : x;
}

size_t sobol_npoints(const doe_space_t *space) {
    return space->samples * (space->factor_count + 2);
}

int sobol_design_build(const doe_space_t *space, sobol_design_t *d, char *err) {
    size_t k = space->factor_count;
    size_t n = space->samples;

    memset(d, 0, sizeof *d);
    if (k == 0) { snprintf(err, DOE_ERR_SIZE, "no factors"); return -1; }
    if (n < 2) { snprintf(err, DOE_ERR_SIZE, "samples must be >= 2 (got %zu)", n); return -1; }

    /*
     * `second_order:` parses and is carried through the .space struct, but no
     * estimator computes second-order indices yet (M5). Until then, refuse the
     * run rather than quietly returning first-order results to someone who
     * asked for interactions -- silently answering a different question is
     * worse than not answering. EXPANSION.md E0.
     *
     * This is the single choke point: every sobol entry path reaches here,
     * including sobol_analyze and the robust funnel, so one check covers them
     * all and none can drift.
     */
    if (space->second_order) {
        snprintf(err, DOE_ERR_SIZE,
                 "second_order is not implemented yet (planned for M5); "
                 "remove it from the .space to run first-order and total indices");
        return -1;
    }

    size_t nk, npoints;
    if (!doe_size_mul_ok(n, k, &nk) || !doe_size_mul_ok(n, k + 2, &npoints)) {
        snprintf(err, DOE_ERR_SIZE, "design too large (size overflow)");
        return -1;
    }

    double *A = malloc(nk * sizeof *A);
    double *B = malloc(nk * sizeof *B);
    if (!A || !B) {
        free(A); free(B);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }

    doe_rng_t rng;
    doe_rng_seed(&rng, space->seed);
    /*
     * Two LHS draws from one stream: independent, because the stream advances.
     *
     * !! DO NOT carry this pattern over to M5. !! When the Joe-Kuo sequence
     * lands, A and B must be the LEFT and RIGHT HALVES OF A SINGLE
     * 2k-DIMENSIONAL quasi-random sequence -- Saltelli et al. 2010 Sec. 5.1
     * p.263: "Matrices A and B of size (N,k) can be easily generated from a
     * quasi-random sequence of size (N,2k): A is the left half of the
     * quasi-random sequence, and B is the right part of it."
     *
     * Drawing a k-dimensional QR sequence twice in sequence does NOT give two
     * independent samples the way two LHS draws do: a QR sequence is
     * deterministic, so restarting it reproduces the same points, and
     * continuing it yields points that are correlated by construction rather
     * than independent. Same paper, Sec. 5.1 consideration 1: skipping rows of
     * the quasi-random matrix is a mistake; consideration 2: uniformity
     * degrades as the column index grows, which is why A takes the leading
     * (better equidistributed) columns and B the trailing ones.
     */
    if (doe_sample_lhs(&rng, n, k, A) != 0 || doe_sample_lhs(&rng, n, k, B) != 0) {
        free(A); free(B);
        snprintf(err, DOE_ERR_SIZE, "sampling failed");
        return -1;
    }

    d->k = k;
    d->n = n;
    d->npoints = npoints;
    d->A = A;
    d->B = B;
    return 0;
}

void sobol_design_free(sobol_design_t *d) {
    if (!d) return;
    free(d->A);
    free(d->B);
    memset(d, 0, sizeof *d);
}

void sobol_point(const sobol_design_t *d, size_t idx, double *u_out) {
    size_t n = d->n, k = d->k;
    if (idx < n) {                          /* block A */
        memcpy(u_out, &d->A[idx * k], k * sizeof *u_out);
    } else if (idx < 2 * n) {               /* block B */
        memcpy(u_out, &d->B[(idx - n) * k], k * sizeof *u_out);
    } else {                                /* block A_B^(i) */
        size_t i = idx / n - 2;
        size_t row = idx % n;
        memcpy(u_out, &d->A[row * k], k * sizeof *u_out);
        u_out[i] = d->B[row * k + i];       /* swap in column i from B */
    }
}

/* Estimate Si and STi for one factor over the row indices in idx[0..n).
 * Var is computed from the union of yA and yB over those same rows, so the
 * estimator is self-contained and the bootstrap can resample freely. */
static void est_factor(const double *yA, const double *yB, const double *yABi,
                       const size_t *idx, size_t n, double *si, double *sti) {
    double sum = 0.0;
    for (size_t r = 0; r < n; r++) sum += yA[idx[r]] + yB[idx[r]];
    double mean = sum / (2.0 * (double)n);

    double var = 0.0;
    for (size_t r = 0; r < n; r++) {
        double da = yA[idx[r]] - mean, db = yB[idx[r]] - mean;
        var += da * da + db * db;
    }
    var /= (2.0 * (double)n - 1.0);

    double s = 0.0, t = 0.0;
    for (size_t r = 0; r < n; r++) {
        size_t j = idx[r];
        double a = yA[j], b = yB[j], ab = yABi[j];
        s += b * (ab - a);          /* Saltelli 2010 */
        t += (a - ab) * (a - ab);   /* Jansen 1999    */
    }
    /* A bootstrap replicate can be degenerate (every resampled row identical)
     * even when the full sample is not. Dividing by that variance yields NaN,
     * and NaN in the array makes the qsort below produce an arbitrary order,
     * so the reported CI would be garbage rather than obviously wrong. Report
     * zero for such a replicate instead. The full-sample case is caught up
     * front in sobol_analyze with a clean error. */
    if (!(var > 0.0) || !isfinite(var)) { *si = 0.0; *sti = 0.0; return; }

    *si  = (s / (double)n) / var;
    *sti = (t / (2.0 * (double)n)) / var;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

int sobol_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                  sobol_index_t **out, size_t *count, char *err) {
    sobol_design_t d;
    if (sobol_design_build(space, &d, err) != 0) return -1;

    size_t N = d.n, k = d.k, M = d.npoints;
    if (nresp < M) {
        snprintf(err, DOE_ERR_SIZE, "need %zu responses, got %zu", M, nresp);
        sobol_design_free(&d);
        return -1;
    }
    for (size_t i = 0; i < M; i++) {
        if (!isfinite(responses[i])) {
            snprintf(err, DOE_ERR_SIZE, "missing or non-finite response for run %zu", i + 1);
            sobol_design_free(&d);
            return -1;
        }
    }

    const double *yA = responses;
    const double *yB = responses + N;

    /*
     * Sobol indices are variance SHARES, so they are undefined when the output
     * has no variance. Without this the estimator divides by zero and every
     * index prints as -nan while the tool exits 0 -- silent garbage. A
     * constant response usually means the model ignores its inputs, the script
     * echoes a fixed value, or the ranges are too narrow to move it, so say so.
     */
    {
        double mean = 0.0;
        for (size_t i = 0; i < N; i++) mean += yA[i] + yB[i];
        mean /= (2.0 * (double)N);
        double var = 0.0;
        for (size_t i = 0; i < N; i++) {
            double da = yA[i] - mean, db = yB[i] - mean;
            var += da * da + db * db;
        }
        var /= (2.0 * (double)N - 1.0);
        if (!(var > 0.0) || !isfinite(var)) {
            snprintf(err, DOE_ERR_SIZE,
                     "output variance is zero over %zu samples (every response is %g): "
                     "sensitivity indices are shares of variance and are undefined. "
                     "Check that the model actually depends on the factors and that "
                     "their ranges are wide enough to move it.", N, yA[0]);
            sobol_design_free(&d);
            return -1;
        }
    }

    sobol_index_t *res = calloc(k, sizeof *res);
    size_t *id0 = malloc(N * sizeof *id0);
    size_t *idb = malloc(N * sizeof *idb);
    double *sb  = malloc(SOBOL_BOOTSTRAP * sizeof *sb);
    double *tb  = malloc(SOBOL_BOOTSTRAP * sizeof *tb);
    if (!res || !id0 || !idb || !sb || !tb) {
        free(res); free(id0); free(idb); free(sb); free(tb);
        sobol_design_free(&d);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }
    for (size_t r = 0; r < N; r++) id0[r] = r;

    doe_rng_t brng;
    doe_rng_seed(&brng, space->seed ^ 0xB007D0E5ULL);   /* distinct, reproducible */

    size_t lo = (size_t)(0.025 * SOBOL_BOOTSTRAP);
    size_t hi = (size_t)(0.975 * SOBOL_BOOTSTRAP);
    if (hi >= SOBOL_BOOTSTRAP) hi = SOBOL_BOOTSTRAP - 1;

    for (size_t i = 0; i < k; i++) {
        const double *yABi = responses + (2 + i) * N;

        double si, sti;
        est_factor(yA, yB, yABi, id0, N, &si, &sti);

        for (int b = 0; b < SOBOL_BOOTSTRAP; b++) {
            for (size_t r = 0; r < N; r++) idb[r] = rng_below(&brng, N);
            est_factor(yA, yB, yABi, idb, N, &sb[b], &tb[b]);
        }
        qsort(sb, SOBOL_BOOTSTRAP, sizeof *sb, cmp_double);
        qsort(tb, SOBOL_BOOTSTRAP, sizeof *tb, cmp_double);

        strncpy(res[i].name, space->factors[i].name, DOE_MAX_NAME - 1);
        res[i].s1 = si; res[i].s1_lo = sb[lo]; res[i].s1_hi = sb[hi];
        res[i].st = sti; res[i].st_lo = tb[lo]; res[i].st_hi = tb[hi];
    }

    free(id0); free(idb); free(sb); free(tb);
    sobol_design_free(&d);
    *out = res;
    *count = k;
    return 0;
}
