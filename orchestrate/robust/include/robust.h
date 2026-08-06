#ifndef ROBUST_H
#define ROBUST_H

/*
 * robust — the funnel orchestrator. Chains the sensitivity-analysis stages:
 *
 *   Morris screen  -> drop low-mu* factors -> Sobol attribute (survivors)
 *
 * The orchestration is written against an injectable `robust_run_fn` evaluator,
 * so the whole process is testable in-process (analytic functions) as well as
 * driven by an external model script (the CLI's shell evaluator).
 */

#include "doe.h"
#include "morris.h"
#include "sobol.h"

/* Keep factors whose mu* >= fraction * max(mu*). */
#define ROBUST_KEEP_FRACTION 0.1

/* Fill responses[0..npoints) for the given design points (u: npoints*factor_count,
 * row-major, in [0,1)) over `space`. Returns 0 on success, -1 on error. */
typedef int (*robust_run_fn)(void *ctx, const doe_space_t *space,
                             const double *u, size_t npoints,
                             double *responses, char *err);

typedef struct {
    size_t           k;             /* original factor count             */
    morris_effect_t *effects;       /* k, factor order (owned)           */
    int             *keep;          /* k flags (owned): 1 = survivor     */
    size_t           n_survivors;
    doe_space_t      subspace;      /* survivor-only space               */
    sobol_index_t   *indices;       /* n_survivors, factor order (owned) */
    size_t           n_indices;
    double           keep_fraction;
} robust_result_t;

void robust_result_free(robust_result_t *r);

/* Stage 1 — Morris screen: build design, evaluate via `run`, analyze, and set
 * the keep mask (at least the most important factor is always kept). */
int robust_screen(const doe_space_t *space, robust_run_fn run, void *ctx,
                  double keep_fraction, morris_effect_t **effects,
                  int *keep, size_t *n_survivors, char *err);

/* Build a survivor-only subspace (copies seed and method params). */
int robust_subspace(const doe_space_t *space, const int *keep,
                    doe_space_t *sub, char *err);

/* Stage 2 — Sobol attribution on the survivors; dropped factors are held at the
 * midpoint (u = 0.5) so the user's model still receives every factor. */
int robust_attribute(const doe_space_t *space, const doe_space_t *sub, const int *keep,
                     robust_run_fn run, void *ctx,
                     sobol_index_t **indices, size_t *n, char *err);

/* Full funnel: screen -> subspace -> attribute. On error the partially filled
 * result is still safe to pass to robust_result_free. */
int robust_funnel(const doe_space_t *space, robust_run_fn run, void *ctx,
                  double keep_fraction, robust_result_t *result, char *err);

/* Reports — self-contained, no external resources. */
int robust_write_json(const robust_result_t *r, const char *path, char *err);
int robust_write_html(const robust_result_t *r, const char *path, char *err);
/* Emit a taguchi .tgu for the survivors (3 levels for continuous factors). */
int robust_write_tgu(const robust_result_t *r, const char *path, char *err);

#endif /* ROBUST_H */
