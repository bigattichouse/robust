#ifndef DOE_H
#define DOE_H

/*
 * libdoe — shared core for the robust design-of-experiments toolkit.
 *
 * Holds the pieces every tool (morris, sobol, robust, ...) needs: a seedable,
 * portable PRNG; the .space factor-definition format + factor scaling; sampling
 * primitives; the fork/env run-loop; results CSV parsing; JSON helpers; stats.
 *
 * Pure C99, no I/O in the math paths. Mirrors the taguchi library conventions
 * (opaque-ish handles, error_buf pattern, caller-frees strings).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DOE_VERSION_MAJOR 0
#define DOE_VERSION_MINOR 1
#define DOE_VERSION_PATCH 0

#define DOE_ERR_SIZE    256
#define DOE_MAX_FACTORS  64
#define DOE_MAX_LEVELS   32
#define DOE_MAX_NAME     64
#define DOE_MAX_VALUE    64

/* ============================================================================
 * PRNG — xoshiro256** seeded by splitmix64.
 * Deterministic and platform-independent: the same seed yields the same stream
 * everywhere, so any design can be regenerated from the .space file alone.
 * ============================================================================ */

typedef struct { uint64_t s[4]; } doe_rng_t;

void     doe_rng_seed(doe_rng_t *rng, uint64_t seed);
uint64_t doe_rng_next(doe_rng_t *rng);
double   doe_rng_uniform(doe_rng_t *rng);   /* uniform double in [0, 1) */

/* ============================================================================
 * Factor space (.space format)
 * ============================================================================ */

typedef enum {
    DOE_LINEAR = 0,   /* continuous range [lo, hi]                         */
    DOE_LOG,          /* log-scaled range [lo, hi], lo > 0                 */
    DOE_CATEGORICAL   /* enumerated levels (ordinal grid for screening)    */
} doe_scale_t;

typedef struct {
    char        name[DOE_MAX_NAME];
    doe_scale_t scale;
    double      lo, hi;                                /* LINEAR / LOG */
    char        levels[DOE_MAX_LEVELS][DOE_MAX_VALUE]; /* CATEGORICAL  */
    size_t      level_count;                           /* CATEGORICAL  */
} doe_factor_t;

typedef struct {
    doe_factor_t factors[DOE_MAX_FACTORS];
    size_t       factor_count;
    uint64_t     seed;
    /* method parameters — each tool reads the ones it needs */
    size_t       trajectories;   /* Morris r  (default 10) */
    size_t       grid_levels;    /* Morris p  (default 4)  */
    size_t       samples;        /* Sobol  N  (default 1024) */
    bool         second_order;   /* Sobol second-order indices */
} doe_space_t;

/* Parse a .space definition. Returns 0 on success, -1 on error (err filled). */
int doe_space_parse(const char *content, doe_space_t *space, char *err);
int doe_space_parse_file(const char *path, doe_space_t *space, char *err);

/* Map u in [0,1) to a factor's real value (LINEAR/LOG). */
double doe_factor_scale(const doe_factor_t *f, double u);

/* Map u in [0,1) to a factor's value as a string (numeric for LINEAR/LOG,
 * level label for CATEGORICAL). Writes into buf and returns buf. */
const char *doe_factor_value(const doe_factor_t *f, double u, char *buf, size_t buf_size);

/* ============================================================================
 * Stats
 * ============================================================================ */

double doe_mean(const double *x, size_t n);
double doe_variance(const double *x, size_t n);   /* sample variance (n-1) */
double doe_std(const double *x, size_t n);

/* ============================================================================
 * JSON helpers
 * ============================================================================ */

/* Escape a string for embedding in a JSON document. Caller frees (doe_free). */
char *doe_json_escape(const char *s);
void  doe_free(void *p);

/* ============================================================================
 * Sampling — provisional signatures, implemented at M3/M5 (see DESIGN.md).
 * ============================================================================ */

/* Latin Hypercube: fill out[n*k] (row-major) with a sample in [0,1). */
int doe_sample_lhs(doe_rng_t *rng, size_t n, size_t k, double *out);
/* Sobol low-discrepancy sequence (Joe-Kuo direction numbers). */
int doe_sample_sobol(size_t n, size_t k, double *out);

/* ============================================================================
 * Run loop — provisional signature, implemented at M2 (lifted from taguchi).
 * ============================================================================ */

/* Returns the value string for design cell (row, col). */
typedef const char *(*doe_value_fn)(void *ctx, size_t row, size_t col);

/* For each of `rows` design points, export <prefix>_<factor>=<value> env vars
 * and run `script` once. Returns 0 on success, -1 on error (err filled). */
int doe_run(const doe_space_t *space, const char *prefix, const char *script,
            size_t rows, doe_value_fn get_value, void *ctx, char *err);

/* ============================================================================
 * Results CSV — provisional signature, implemented at M2.
 * ============================================================================ */

/* Read the `metric` column keyed by run_id (1-based) into responses[].
 * Returns 0 on success, -1 on error (err filled). */
int doe_csv_read_metric(const char *path, const char *metric,
                        double *responses, size_t max_rows,
                        size_t *count_out, char *err);

#ifdef __cplusplus
}
#endif

#endif /* DOE_H */
