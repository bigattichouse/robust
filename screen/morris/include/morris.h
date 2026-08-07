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

/* ============================================================================
 * Group screening — `morris --groups`. See spec/morris-groups.bp.
 *
 * Screens GROUPS of factors at r*(G+1) evaluations instead of r*(k+1), by
 * moving every member of a group together and taking the ABSOLUTE value of
 * the group's effect:
 *
 *     |d_u(X)| = | y(X~) - y(X) | / Delta
 *
 * Source: Campolongo, Cariboni & Saltelli (2007), Environmental Modelling &
 * Software 22(10) 1509-1518, Sec. 3.3 p.1512. Reproduced against that paper's
 * Table 1 in validation/validate.c check B (`make validate`).
 *
 * The absolute value is the whole point: members of one group may move in
 * opposite directions, and taking |.| of the group's response change means a
 * group holding two important factors with opposing signs still registers.
 * That is the silent false negative sequential bifurcation suffers, designed
 * out rather than tested for.
 * ============================================================================ */

typedef struct {
    size_t  k;             /* factor count                        */
    size_t  G;             /* group count                         */
    size_t  r;             /* trajectories                        */
    size_t  npoints;       /* r * (G + 1)                         */
    double *u;             /* npoints * k, row-major, in [0,1]    */
    size_t *moved_group;   /* r * G : group changed at each step  */
    double  delta;         /* the step size, needed by analyze    */
} morris_group_design_t;

typedef struct {
    char   name[DOE_MAX_NAME];
    double mu_star;        /* mean |group elementary effect| — importance   */
    double sigma;          /* std of |d_u| — spread, NOT the per-factor
                            * interaction signal; see the spec              */
    size_t member_count;
} morris_group_effect_t;

/* Number of model runs a group design needs: r * (G + 1). */
size_t morris_group_npoints(const doe_space_t *space);

/* Build the group trajectory design. Requires space->group_count >= 2.
 * Returns 0 on success, -1 on error (err filled). */
int  morris_group_design_build(const doe_space_t *space,
                               morris_group_design_t *d, char *err);
void morris_group_design_free(morris_group_design_t *d);

/* Absolute group elementary effects. `responses` is indexed by run_id-1.
 * Returns effects in group order; caller frees with free(). */
int  morris_group_analyze(const doe_space_t *space, const double *responses,
                          size_t nresp, morris_group_effect_t **out,
                          size_t *count, char *err);

/* Compute elementary-effects statistics. `responses` is indexed by run_id-1
 * (length nresp must be >= npoints). Returns effects in factor order; caller
 * frees with free(). Returns 0 on success, -1 on error. */
int  morris_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                    morris_effect_t **out, size_t *count, char *err);

#endif /* MORRIS_H */
