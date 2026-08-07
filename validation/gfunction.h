#ifndef VALIDATION_GFUNCTION_H
#define VALIDATION_GFUNCTION_H

/*
 * gfunction.h — the Sobol' g-function and its closed-form sensitivity
 * indices, the standard analytic benchmark for screening methods.
 *
 * SOURCES
 *   [S93]  Sobol', I.M. (1993). "Sensitivity estimates for nonlinear
 *          mathematical models." Mathematical Modelling and Computational
 *          Experiments 1(4), 407-414.  -- the function.
 *   [SS95] Saltelli, A., Sobol', I.M. (1995). "About the use of rank
 *          transformation in sensitivity analysis of model output."
 *          Reliability Engineering and System Safety 50, 225-239.
 *          -- the closed-form partial variances used here.
 *   [C07]  Campolongo, F., Cariboni, J., Saltelli, A. (2007). "An effective
 *          screening design for sensitivity analysis of large models."
 *          Environmental Modelling & Software 22(10), 1509-1518.
 *          Local copy: sources/pdf/campolongo-2007-morris-screening.pdf
 *          -- Sec. 3.3 p.1512 states the definition reproduced here, and
 *          uses this benchmark for the group screening results in Table 1.
 *
 * Definition ([C07] Sec. 3.3, p.1512), with X_i ~ U[0,1] independent:
 *
 *     g(X) = prod_i g_i(X_i),   g_i(X_i) = (|4*X_i - 2| + a_i) / (1 + a_i)
 *
 * Smaller a_i => more important X_i. The function is non-monotonic in every
 * factor (the absolute value folds each axis at X_i = 0.5), which is exactly
 * why it is the right benchmark for a method that must not assume monotonicity.
 *
 * Closed forms [SS95]:
 *     V_i    = (1/3) / (1 + a_i)^2
 *     V      = prod_i (1 + V_i) - 1
 *     S_i    = V_i / V
 *     S_Ti   = V_i * prod_{j!=i} (1 + V_j) / V
 * and for a group u (this is the natural extension, and is what [C07]
 * Table 1's "S_T group analytical" column reports):
 *     S_T(u) = 1 - [ prod_{j not in u} (1 + V_j) - 1 ] / V
 *     S(u)   =     [ prod_{j in u}     (1 + V_j) - 1 ] / V
 */

#include <stddef.h>

/* Evaluate g at x[k] with parameters a[k]. */
double gf_eval(const double *x, const double *a, size_t k);

/* Closed-form partial variances. V_i[] must hold k doubles. */
void gf_partial_var(const double *a, size_t k, double *V_i, double *V_tot);

/* Closed-form first-order and total index for a single factor. */
double gf_first_index(const double *V_i, size_t k, double V_tot, size_t i);
double gf_total_index(const double *V_i, size_t k, double V_tot, size_t i);

/* Closed-form group indices. `members` is a 0/1 mask of length k. */
double gf_group_first_index(const double *V_i, size_t k, double V_tot,
                            const int *members);
double gf_group_total_index(const double *V_i, size_t k, double V_tot,
                            const int *members);

/*
 * Second-order index for the pair (i, j). The g-function is a PRODUCT of
 * one-dimensional terms, so its variance decomposition factorises exactly:
 *     V_ij = V_i * V_j      and      S_ij = V_i * V_j / V
 * which makes it an exact reference for a second-order estimator, not just a
 * benchmark. Source: Saltelli & Sobol' (1995).
 */
double gf_second_index(const double *V_i, size_t k, double V_tot,
                       size_t i, size_t j);

#endif /* VALIDATION_GFUNCTION_H */
