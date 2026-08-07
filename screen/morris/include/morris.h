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
    /* 95% bootstrap interval on mu*, resampling TRAJECTORIES (the independent
     * unit) rather than individual effects. Without it a keep/drop line is
     * drawn on point estimates, and validation check C measured that such a
     * line is only as trustworthy as the gap it falls in. */
    double mu_star_lo, mu_star_hi;
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

/* ============================================================================
 * Recursive splitting ("sequential bifurcation with an estimator that cannot
 * sign-cancel"). Screen a partition, drop the groups the keep rule rejects,
 * split each survivor in two, repeat. spec/morris-groups.bp.
 *
 * The loop takes an evaluation callback rather than running anything itself:
 * the math paths in this library do no I/O, and it makes the whole thing
 * testable in-process against a synthetic model with planted factors.
 * ============================================================================ */

/* Evaluate the model at every design point. `u` is npoints*k row-major in
 * [0,1]. Fill responses[0..npoints). Return 0, or -1 with err filled. */
typedef int (*morris_eval_fn)(void *ctx, const doe_space_t *space,
                              const double *u, size_t npoints, size_t k,
                              double *responses, char *err);

typedef struct {
    double keep_share;      /* keep top groups until cumulative mu*-share >=
                             * this (0 < s <= 1). 0 means use 0.9.            */
    size_t max_rounds;      /* 0 means ceil(log2(k)) + 1                      */
    size_t initial_groups;  /* 0 means use the .space `groups:` section, or
                             * split into ~sqrt(k) groups if it has none      */
} morris_bifurcate_opts_t;

typedef struct {
    char   group[DOE_MAX_NAME];
    size_t round;
    double mu_star;
    size_t member_count;
    int    kept;
} morris_bifurcate_step_t;

typedef struct {
    bool   survivors[DOE_MAX_FACTORS];  /* mask over space->factors */
    size_t survivor_count;
    size_t rounds_run;
    size_t evaluations;                 /* actually spent                    */
    size_t predicted_max;               /* worst-case upper bound, known
                                         * BEFORE the first evaluation       */
    morris_bifurcate_step_t *trace;     /* caller frees with free()          */
    size_t trace_count;
    int    stopped_on_tie;              /* the cut fell inside a near-tie    */
    int    low_trajectories;            /* r below MIN_TRAJECTORIES while a
                                         * group held >1 factor: measurable
                                         * false-negative risk, see morris.c */
} morris_bifurcate_result_t;

/*
 * Below this many trajectories, group screening can DROP AN IMPORTANT FACTOR.
 * Measured on 1024 factors with 8 important ones at alternating signs, 20
 * seeds per row, false negatives out of 8:
 *
 *   r=10 : 2 missed when opposing pairs share a group, 0 when spread apart
 *   r=20 : 0        r=40 : 0        r=80 : 0
 *
 * Taking the absolute value of the group effect stops cancellation from
 * biasing the MEAN to zero, but it does not stop it happening in individual
 * trajectories. With too few of them the estimate is noisy enough for a group
 * holding equal-and-opposite factors to fall below the keep cut. r >= 20 was
 * clean in every configuration tried.
 */
#define MORRIS_BIFURCATE_MIN_TRAJECTORIES 20

/* Worst-case evaluation count: every group survives every round. Computable
 * without running anything, which is the point -- a screening method whose
 * cost you cannot predict is not usable for planning an expensive experiment. */
size_t morris_bifurcate_budget(const doe_space_t *space,
                               const morris_bifurcate_opts_t *opts);

int  morris_bifurcate(const doe_space_t *space,
                      const morris_bifurcate_opts_t *opts,
                      morris_eval_fn eval, void *ctx,
                      morris_bifurcate_result_t *out, char *err);
void morris_bifurcate_free(morris_bifurcate_result_t *r);

/* Compute elementary-effects statistics. `responses` is indexed by run_id-1
 * (length nresp must be >= npoints). Returns effects in factor order; caller
 * frees with free(). Returns 0 on success, -1 on error. */
int  morris_analyze(const doe_space_t *space, const double *responses, size_t nresp,
                    morris_effect_t **out, size_t *count, char *err);

#endif /* MORRIS_H */
