#ifndef SOBOL_H
#define SOBOL_H

/*
 * sobol — variance-based sensitivity indices via Saltelli sampling.
 *
 * Draws two independent N x k samples A, B and the k cross-matrices A_B^(i)
 * (A with column i taken from B). Evaluating the model on all N*(k+2) points
 * gives, per factor:
 *   Si   (first-order) = Var(E[Y|Xi]) / Var(Y)            [Saltelli 2010]
 *   STi  (total)       = involves Xi in any interaction    [Jansen 1999]
 * with bootstrap confidence intervals. STi - Si is the interaction share.
 *
 * Like morris, the design is a pure function of (factors, N, seed): analyze
 * regenerates it and needs only the responses.
 */

#include "doe.h"

typedef struct {
    size_t  k;        /* factor count   */
    size_t  n;        /* base samples N */
    size_t  npoints;  /* N * (k + 2)    */
    double *A;        /* N * k, row-major, in [0,1) */
    double *B;        /* N * k, row-major, in [0,1) */
} sobol_design_t;

typedef struct {
    char   name[DOE_MAX_NAME];
    double s1, s1_lo, s1_hi;   /* first-order index + bootstrap CI */
    double st, st_lo, st_hi;   /* total-order index + bootstrap CI */
} sobol_index_t;

int    sobol_design_build(const doe_space_t *space, sobol_design_t *d, char *err);
void   sobol_design_free(sobol_design_t *d);
size_t sobol_npoints(const doe_space_t *space);

/* Fill u_out[k] (in [0,1)) for global point `idx`. Block layout:
 *   [0,N)            -> A
 *   [N,2N)           -> B
 *   [(2+i)N,(3+i)N)  -> A_B^(i)  for factor i in 0..k-1   */
void sobol_point(const sobol_design_t *d, size_t idx, double *u_out);

/* Compute Si/STi (+ CIs). `responses` is indexed by run_id-1 (length nresp
 * must be >= npoints). Returns indices in factor order; caller frees. */
int sobol_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                  sobol_index_t **out, size_t *count, char *err);

#endif /* SOBOL_H */
