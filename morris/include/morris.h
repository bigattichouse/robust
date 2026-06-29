#ifndef MORRIS_H
#define MORRIS_H

/*
 * morris — randomized elementary-effects screening (Morris, 1991).
 *
 * Builds r trajectories of k+1 points each on a p-level grid in [0,1]^k; one
 * factor moves by +/-Delta between consecutive points. Per factor, the
 * elementary effects EE = dy/Delta across trajectories give:
 *   mu*  = mean|EE|  -> importance
 *   sigma= std(EE)   -> nonlinearity / interaction
 *
 * The design is a pure function of (factors, r, p, seed), so analyze
 * regenerates the identical design and needs only the responses.
 */

#include "doe.h"

typedef struct {
    size_t  k;             /* factor count                       */
    size_t  r;             /* trajectories                       */
    size_t  npoints;       /* r * (k + 1)                        */
    double *u;             /* npoints * k, row-major, in [0,1]   */
    size_t *moved_factor;  /* r * k : factor changed at each step */
    double *moved_delta;   /* r * k : signed step (+/-Delta)      */
} morris_design_t;

typedef struct {
    char   name[DOE_MAX_NAME];
    double mu;        /* mean elementary effect            */
    double mu_star;   /* mean |elementary effect| (importance) */
    double sigma;     /* std of elementary effects (interaction flag) */
} morris_effect_t;

/* Build the trajectory design from a parsed .space (uses space->seed,
 * ->trajectories, ->grid_levels). Returns 0 on success, -1 on error. */
int  morris_design_build(const doe_space_t *space, morris_design_t *d, char *err);
void morris_design_free(morris_design_t *d);

/* Number of model runs a design needs: r * (k + 1). */
size_t morris_npoints(const doe_space_t *space);

/* Compute elementary-effects statistics. `responses` is indexed by run_id-1
 * (length nresp must be >= npoints). Returns effects in factor order; caller
 * frees with free(). Returns 0 on success, -1 on error. */
int  morris_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                    morris_effect_t **out, size_t *count, char *err);

#endif /* MORRIS_H */
