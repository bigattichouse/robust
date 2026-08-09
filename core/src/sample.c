/*
 * sample.c — sampling primitives.
 *
 *   doe_sample_lhs   : Latin Hypercube — stratified, one sample per stratum per
 *                      dimension. Used by Sobol's Saltelli cross-sampling (M3).
 *   doe_sample_sobol : Sobol low-discrepancy sequence, Joe-Kuo direction
 *                      numbers (M5).
 */

#include "doe.h"
#include "sobol_dirnum.h"

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

/* ============================================================================
 * Sobol low-discrepancy sequence — Joe-Kuo direction numbers.
 *
 * Follows Joe & Kuo, "Notes on generating Sobol sequences" (2008), and is
 * bit-for-bit identical to their reference sobol.cc, which is what
 * test_doe.c's reference vectors pin. Two equations from those notes:
 *
 *   (2)  m_k = 2 a_1 m_{k-1} ^ ... ^ 2^{s-1} a_{s-1} m_{k-s+1}
 *                ^ 2^s m_{k-s} ^ m_{k-s}
 *        scaled into 32-bit words as V[i] = m_i << (32 - i), the recurrence
 *        becomes V[i] = V[i-s] ^ (V[i-s] >> s) ^ (a_q ? V[i-q] : 0).
 *
 *   (5)  x_0 = 0,  x_i = x_{i-1} ^ V[c_{i-1}]
 *        the Antonov-Saleev Gray-code form, where c_i is the index of the
 *        first zero bit from the right of i. Same sequence as the direct form
 *        (4), reordered within each block of 2^m points, so the uniformity
 *        properties are identical and it costs one XOR per point.
 *
 * The origin is included as point 0. Two sources agree on not dropping it:
 * the Joe-Kuo notes §4 ("we are less persuaded by such recommendation") and
 * Saltelli et al. 2010 §5.1 consideration 1, which ties uniformity to the
 * space being filled every 2^m points and calls skipping rows a mistake.
 * Dropping the leading points would break exactly that alignment.
 * ============================================================================ */

/* The public limit and the generated table must agree. C99 has no
 * _Static_assert, so this is the negative-array-size idiom. */
typedef char doe_sobol_table_matches_header[
    (DOE_SOBOL_TABLE_DIM == DOE_SOBOL_MAX_DIM) ? 1 : -1];

/* Direction numbers V[1..L] for one dimension, scaled by 2^32. */
static void sobol_dirvec(size_t dim, unsigned L, uint32_t *V) {
    if (dim == 0) {
        /* Dimension 1 is not in the table: every m_i = 1, so V[i] = 2^-i. */
        for (unsigned i = 1; i <= L; i++) V[i] = 1u << (32 - i);
        return;
    }

    size_t t = dim - 1;                       /* table entry for dimension dim+1 */
    unsigned s = doe_sobol_s[t];
    unsigned a = doe_sobol_a[t];
    const uint16_t *m = &doe_sobol_m[doe_sobol_m_off[t]];   /* m[0] is m_1 */

    unsigned lim = (L < s) ? L : s;
    for (unsigned i = 1; i <= lim; i++) V[i] = (uint32_t)m[i - 1] << (32 - i);
    for (unsigned i = s + 1; i <= L; i++) {
        uint32_t v = V[i - s] ^ (V[i - s] >> s);
        for (unsigned q = 1; q <= s - 1; q++)
            if ((a >> (s - 1 - q)) & 1u) v ^= V[i - q];
        V[i] = v;
    }
}

int doe_sample_sobol_dims(size_t n, size_t dim0, size_t k, double *out) {
    if (n == 0 || k == 0 || !out) return -1;
    /* Overflow, not just a range check: dim0 + k must not wrap. */
    if (dim0 > DOE_SOBOL_MAX_DIM || k > DOE_SOBOL_MAX_DIM - dim0) return -1;

    /* L = ceil(log2 n): enough direction numbers for points 0..n-1. The
     * largest Gray-code index used is c(n-2), whose trailing-ones run is at
     * most L-1, so V[1..L] is exactly enough -- never L+1. */
    unsigned L = 0;
    for (size_t p = 1; p < n; p <<= 1) {
        if (++L > 32) return -1;     /* beyond a 32-bit word; n is absurd */
    }

    for (size_t j = 0; j < k; j++) {
        uint32_t V[33];
        sobol_dirvec(dim0 + j, L, V);

        uint32_t x = 0;
        out[j] = 0.0;                            /* point 0 is the origin */
        for (size_t i = 1; i < n; i++) {
            /* c = index of the first zero bit from the right of (i-1), 1-based */
            unsigned c = 1;
            for (size_t v = i - 1; v & 1u; v >>= 1) c++;
            x ^= V[c];
            out[i * k + j] = (double)x / 4294967296.0;   /* 2^32 */
        }
    }
    return 0;
}

int doe_sample_sobol(size_t n, size_t k, double *out) {
    return doe_sample_sobol_dims(n, 0, k, out);
}
