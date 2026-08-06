#ifndef PARETO_H
#define PARETO_H

/*
 * pareto — non-dominated frontier over a multi-metric results CSV.
 *
 * Two modes over one dominance core:
 *   FILTER  stateless: results CSV in, non-dominated rows out, same dialect.
 *   STORE   stateful: a .front file accumulates a frontier across batches.
 *
 * Dominance (Deb 2001, Sec. 2.4): a dominates b iff a is no worse than b on
 * every objective and strictly better on at least one.
 *
 * A .front file is deliberately *itself* a valid results CSV — its metadata
 * lives in '#' comment lines, which common/src/csv.c already skips. So every
 * downstream tool reads a frontier unchanged.
 *
 * Spec: spec/pareto.bp
 */

#include "doe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resource caps — SECURITY.md H1. */
#define PARETO_MAX_OBJECTIVES   16
#define PARETO_MAX_ROWS    1048576u
#define PARETO_WARN_ROWS     65536u   /* warn about O(n^2) cost above this */
#define PARETO_MAX_LINE       8192    /* matches csv.c's line limit        */
#define PARETO_MAX_SOURCE       64

typedef enum { PARETO_MAX = 0, PARETO_MIN } pareto_sense_t;

typedef struct {
    char           name[DOE_MAX_NAME];
    pareto_sense_t sense;
    int            column;          /* resolved index in the results CSV */
} pareto_objective_t;

typedef struct {
    double  values[PARETO_MAX_OBJECTIVES];
    char   *row;                          /* original CSV line, owned */
    char    source[PARETO_MAX_SOURCE];    /* batch label it arrived from */
    unsigned long long run_id;
} pareto_point_t;

typedef struct {
    char     source[PARETO_MAX_SOURCE];
    char     when[32];                    /* ISO-8601 UTC */
    size_t   rows_in;
    size_t   admitted;    /* arrivals that joined the front            */
    size_t   evicted;     /* incumbents newly dominated and removed    */
    size_t   rejected;    /* arrivals dominated on arrival             */
    size_t   duplicate;   /* arrivals already present (same run_id and
                           * same objective values) — a re-merge, not a
                           * tie. Invariant: rows_in = admitted +
                           * rejected + duplicate. */
} pareto_merge_t;

typedef struct {
    pareto_objective_t objectives[PARETO_MAX_OBJECTIVES];
    size_t             objective_count;
    char              *columns;           /* header line, owned */
    pareto_point_t    *points;
    size_t             point_count, point_cap;
    pareto_merge_t    *history;
    size_t             history_count, history_cap;
} pareto_front_t;

/* ---- dominance core ---------------------------------------------------- */

/* Is `x` strictly better than `y` under `sense`? */
int pareto_better(double x, double y, pareto_sense_t sense);

/* Weak dominance with strict improvement somewhere. Irreflexive, asymmetric,
 * transitive. Points equal on every objective dominate each other in NEITHER
 * direction, so both survive: two settings with identical measured
 * performance are distinct operating points the objectives do not separate. */
int pareto_dominates(const pareto_point_t *a, const pareto_point_t *b,
                     const pareto_objective_t *objs, size_t nobj);

/* Mark the non-dominated members of pts[0..n). keep[i] set to 1 or 0.
 * O(n^2), deterministic, allocation-free. */
void pareto_non_dominated(const pareto_point_t *pts, size_t n,
                          const pareto_objective_t *objs, size_t nobj,
                          unsigned char *keep);

/* ---- objectives -------------------------------------------------------- */

/* Append an objective. Returns 0, or -1 on cap/duplicate (err filled). */
int pareto_add_objective(pareto_objective_t *objs, size_t *nobj,
                         const char *name, pareto_sense_t sense, char *err);

/* ---- results CSV ------------------------------------------------------- */

/* Read a results CSV (path, or "-" for stdin) into a point array. Resolves
 * each objective's column from the header and fills objs[].column.
 * On success `out` and `count` receive freshly allocated memory; free it with
 * pareto_points_free. Returns 0, or -1 with err filled. */
int pareto_read_csv(const char *path, pareto_objective_t *objs, size_t nobj,
                    const char *source_label,
                    pareto_point_t **out, size_t *count, char **header_out,
                    char *err);

void pareto_points_free(pareto_point_t *pts, size_t n);

/* ---- frontier store ---------------------------------------------------- */

void pareto_front_init(pareto_front_t *f);
void pareto_front_free(pareto_front_t *f);

/* Parse a .front file. Returns 0, or -1 with err filled. */
int pareto_front_load(const char *path, pareto_front_t *f, char *err);

/* Write a .front atomically: temp file in the same directory, then rename(2).
 * A crash mid-write must never truncate a frontier that may represent weeks
 * of runs. Returns 0, or -1 with err filled (original left untouched). */
int pareto_front_save(const char *path, const pareto_front_t *f, char *err);

/* Fold `pts` into the front. Recomputes the non-dominated set over the union,
 * so merge order never matters. Records a pareto_merge_t in the history.
 * Returns 0, or -1 with err filled. */
int pareto_front_merge(pareto_front_t *f, pareto_point_t *pts, size_t n,
                       const char *source_label, pareto_merge_t *rec, char *err);

#ifdef __cplusplus
}
#endif

#endif /* PARETO_H */
