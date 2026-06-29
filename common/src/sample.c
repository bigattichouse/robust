/*
 * sample.c — sampling primitives.
 *
 *   doe_sample_lhs   : Latin Hypercube — stratified, one sample per stratum per
 *                      dimension. Used by Sobol's Saltelli cross-sampling (M3).
 *   doe_sample_sobol : Sobol low-discrepancy sequence (Joe-Kuo) — stub, M5.
 */

#include "doe.h"

#include <stdlib.h>

int doe_sample_lhs(doe_rng_t *rng, size_t n, size_t k, double *out) {
    if (n == 0 || k == 0) return -1;

    size_t *perm = malloc(n * sizeof *perm);
    if (!perm) return -1;

    for (size_t j = 0; j < k; j++) {
        for (size_t i = 0; i < n; i++) perm[i] = i;
        /* Fisher-Yates shuffle of the stratum order for this dimension */
        for (size_t i = n; i > 1; i--) {
            size_t a = (size_t)(doe_rng_uniform(rng) * (double)i);
            if (a >= i) a = i - 1;
            size_t t = perm[i - 1]; perm[i - 1] = perm[a]; perm[a] = t;
        }
        /* place one point uniformly within each stratum */
        for (size_t i = 0; i < n; i++) {
            double jitter = doe_rng_uniform(rng);
            out[i * k + j] = ((double)perm[i] + jitter) / (double)n;
        }
    }

    free(perm);
    return 0;
}

int doe_sample_sobol(size_t n, size_t k, double *out) {
    (void)n; (void)k; (void)out;
    return -1;   /* TODO(M5): Sobol low-discrepancy sequence */
}
