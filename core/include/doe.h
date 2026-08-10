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
#define DOE_MAX_FACTORS       1024
#define DOE_MAX_GROUPS          64
/*
 * How many factors may carry a categorical level list. Level strings are 95%
 * of a factor's storage (DOE_MAX_LEVELS * DOE_MAX_VALUE = 2 KB each) and only
 * categorical factors use them, so they live in a separate pool rather than
 * inline in every factor. That is what makes DOE_MAX_FACTORS = 1024 fit in a
 * ~300 KB struct instead of 2.1 MB, while keeping doe_space_t trivially
 * copyable -- no pointers, no ownership, no way to leak one.
 *
 * A design with more than this many *categorical* factors is a different kind
 * of problem than high-dimensional screening, which is continuous.
 */
#define DOE_MAX_CATEGORICAL     64
#define DOE_MAX_LEVELS   32
#define DOE_MAX_NAME     64
#define DOE_MAX_VALUE    64

/* Resource caps — reject absurd .space parameters before they reach an
 * allocation (hardening: see SECURITY.md H1). */
#define DOE_MAX_SAMPLES       1048576u   /* Sobol N  (2^20) */
#define DOE_MAX_TRAJECTORIES    10000u   /* Morris r        */
#define DOE_MAX_GRID_LEVELS        64u   /* Morris p        */
/* Largest .space file doe_space_parse_file will read into memory. A file with
 * the maximum 1024 factors is ~100 KB; this leaves ~40x headroom and stops a
 * directory (whose ftell reports LONG_MAX) reaching malloc. */
#define DOE_MAX_SPACE_BYTES  4194304u    /* 4 MiB           */

/* Overflow-checked size_t multiply: returns 1 and sets *out = a*b, or 0 if the
 * product would overflow. Use before every count-based allocation. */
static inline int doe_size_mul_ok(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > ((size_t)-1) / a) return 0;
    if (out) *out = a * b;
    return 1;
}

/*
 * Coverage in forked children.
 *
 * runner.c forks and then either execs (replacing the process image) or
 * _exit()s (which skips the atexit handler gcov installs). Either way the
 * child's counters are never written, so every line it executed reads as
 * uncovered no matter how thoroughly it ran -- runner.c sat at 52% for exactly
 * this reason, with the child-side code showing zero despite tests that could
 * only pass if it had run.
 *
 * __gcov_dump() writes the counters explicitly. Calling it in the child right
 * before exec or _exit attributes that work correctly; gcov merges the child's
 * .gcda into the parent's. Compiled in only for `make coverage`, so ordinary
 * and sanitizer builds are byte-for-byte unaffected.
 */
#ifdef DOE_COVERAGE
extern void __gcov_dump(void);
#  define DOE_GCOV_DUMP() __gcov_dump()
#else
#  define DOE_GCOV_DUMP() ((void)0)
#endif

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
    double      lo, hi;         /* LINEAR / LOG                              */
    int         level_slot;     /* CATEGORICAL: index into doe_space_t.levels,
                                 * -1 for continuous factors                 */
    size_t      level_count;    /* CATEGORICAL                               */
} doe_factor_t;

/*
 * A named set of factors moved together, for group screening
 * (`morris --groups`). Groups must PARTITION the factors: every factor in
 * exactly one group. Overlap would move a factor twice in a single step,
 * making the group effect ill-defined; an uncovered factor would silently
 * never be screened. See spec/morris-groups.bp.
 */
typedef struct {
    char   name[DOE_MAX_NAME];
    bool   members[DOE_MAX_FACTORS];   /* mask over doe_space_t.factors */
    size_t member_count;
} doe_group_t;

/*
 * Which sampler fills the Saltelli design (`sampling:` in the .space).
 *
 * SOBOL is the default because Saltelli et al. (2010) §7 conclusion 3 names
 * quasi-random sampling one of its four best practices, and `make validate`
 * check E measures the difference on the g-function rather than taking that on
 * trust. LHS remains selectable: it is what shipped before M5, so it keeps old
 * designs reproducible, and it is the comparison arm for that measurement.
 *
 * Zero is SOBOL deliberately -- a memset-and-fill doe_space_t gets the
 * recommended sampler, not the fallback.
 */
typedef enum {
    DOE_SAMPLING_SOBOL = 0,   /* Joe-Kuo low-discrepancy sequence */
    DOE_SAMPLING_LHS          /* Latin Hypercube, seeded from `seed:` */
} doe_sampling_t;

typedef struct {
    doe_factor_t factors[DOE_MAX_FACTORS];
    size_t       factor_count;
    /* Level strings for categorical factors only; see DOE_MAX_CATEGORICAL. */
    char         levels[DOE_MAX_CATEGORICAL][DOE_MAX_LEVELS][DOE_MAX_VALUE];
    size_t       categorical_count;
    /* Optional `groups:` section. group_count == 0 means per-factor
     * screening, which is the default and leaves every tool unchanged. */
    doe_group_t  groups[DOE_MAX_GROUPS];
    size_t       group_count;
    uint64_t     seed;
    /* method parameters — each tool reads the ones it needs */
    size_t       trajectories;   /* Morris r  (default 10) */
    size_t       grid_levels;    /* Morris p  (default 4)  */
    size_t       samples;        /* Sobol  N  (default 1024) */
    bool         second_order;   /* Sobol second-order indices */
    doe_sampling_t sampling;     /* Sobol sampler (default DOE_SAMPLING_SOBOL) */
} doe_space_t;

/* Parse a .space definition. Returns 0 on success, -1 on error (err filled). */
int doe_space_parse(const char *content, doe_space_t *space, char *err);
int doe_space_parse_file(const char *path, doe_space_t *space, char *err);

/* Map u in [0,1) to a factor's real value (LINEAR/LOG). */
double doe_factor_scale(const doe_factor_t *f, double u);

/*
 * Map u in [0,1) to factor `idx`'s value as a string (numeric for LINEAR/LOG,
 * level label for CATEGORICAL). Writes into buf and returns buf.
 *
 * Takes the space rather than the factor because level strings live in the
 * space's pool. That also means no factor holds a pointer, so a doe_space_t
 * can still be copied by assignment without aliasing anything.
 */
const char *doe_factor_value(const doe_space_t *space, size_t idx,
                             double u, char *buf, size_t buf_size);

/* ============================================================================
 * Stats
 * ============================================================================ */

double doe_mean(const double *x, size_t n);
double doe_median(double *x, size_t n);            /* reorders x in place */
double doe_quantile(double *x, size_t n, double q); /* reorders x in place */

/*
 * Ordinary least squares on a standardized design.
 *
 * X is n*k row-major and y is n long; both are standardized internally
 * (each column and y centred and scaled to unit standard deviation), so the
 * returned coefficients are the STANDARDIZED regression coefficients -- the
 * SRC of sensitivity analysis, directly comparable across factors regardless
 * of their units.
 *
 * Solves the normal equations by Gaussian elimination with partial pivoting.
 * r2_out receives the coefficient of determination, which doubles as a trust
 * diagnostic: near 1 means the linear story suffices and the ranking is
 * reliable; low means it does not, and variance-based indices were needed.
 *
 * Returns 0, or -1 with err filled (rank-deficient, constant column,
 * constant response, n <= k).
 */
int doe_ols_src(const double *X, const double *y, size_t n, size_t k,
                double *coef_out, double *r2_out, char *err);

/* Replace each column of X and the vector y with their ranks (1..n, ties
 * averaged). Feeding the result to doe_ols_src gives SRRC. */
void doe_rank_transform(double *X, double *y, size_t n, size_t k);
double doe_variance(const double *x, size_t n);   /* sample variance (n-1) */
double doe_std(const double *x, size_t n);

/* ============================================================================
 * JSON helpers
 * ============================================================================ */

/* Escape a string for embedding in a JSON document. Caller frees (doe_free). */
char *doe_json_escape(const char *s);
/* Escape a string for embedding in HTML text. Caller frees (doe_free). */
char *doe_html_escape(const char *s);
void  doe_free(void *p);

/* ============================================================================
 * Sampling — provisional signatures, implemented at M3/M5 (see DESIGN.md).
 * ============================================================================ */

/* Latin Hypercube: fill out[n*k] (row-major) with a sample in [0,1). */
int doe_sample_lhs(doe_rng_t *rng, size_t n, size_t k, double *out);

/*
 * Sobol low-discrepancy sequence, Joe-Kuo direction numbers (M5).
 *
 * Fills out[n*k] (row-major) with points 0..n-1 of the k-dimensional sequence,
 * each coordinate in [0,1). Point 0 is the origin: the sequence is not
 * advanced past it, because its uniformity guarantee is stated over aligned
 * blocks of 2^m points (Joe-Kuo notes §4; Saltelli et al. 2010 §5.1
 * consideration 1).
 *
 * Takes no doe_rng_t: a quasi-random sequence is DETERMINISTIC. The same n and
 * k always give the same points, so a design is regenerable from the .space
 * file with or without its `seed:` -- which for `sampling: sobol` affects only
 * the bootstrap, not the design.
 *
 * Returns 0, or -1 if n or k is 0, or if the dimensions requested exceed
 * DOE_SOBOL_MAX_DIM. It writes NOTHING to `out` when it fails, so a caller
 * that ignores the return value reads uninitialised memory -- deliberately,
 * because a sanitizer catches that immediately whereas plausible-looking zeros
 * would sail through and quietly corrupt a design. Check the return value.
 */
int doe_sample_sobol(size_t n, size_t k, double *out);

/*
 * The same sequence, restricted to dimensions dim0 .. dim0+k-1.
 *
 * This exists for one reason. Saltelli et al. 2010 §5.1 p.263 requires the
 * Saltelli design's A and B to be "the left half" and "the right part" of a
 * single 2k-dimensional quasi-random sequence. Two k-dimensional draws are
 * NOT a substitute: a QR sequence is deterministic, so a second draw either
 * repeats the first exactly or, if continued, is correlated with it by
 * construction. Calling this with dim0 = 0 and dim0 = k over the same n gives
 * the two halves without materialising the n-by-2k matrix.
 *
 * Consideration 2 of that section also matters here: uniformity degrades as
 * the column index grows, so A must take the LEADING dimensions and B the
 * trailing ones, not the reverse.
 */
int doe_sample_sobol_dims(size_t n, size_t dim0, size_t k, double *out);

/* Dimensions the vendored Joe-Kuo table covers. `sobol` needs 2k of them for
 * k factors, hence DOE_SOBOL_MAX_FACTORS. The cap is a quality limit, not a
 * storage one: Joe & Kuo's D(6) set satisfies Sobol' Property A only up to
 * dimension 1111 (`make validate` check H reproduces the boundary and confirms
 * it holds across all 1024 shipped), so every dimension here is inside that
 * region. */
#define DOE_SOBOL_MAX_DIM     1024
#define DOE_SOBOL_MAX_FACTORS (DOE_SOBOL_MAX_DIM / 2)

/* ============================================================================
 * Run loop — provisional signature, implemented at M2 (lifted from taguchi).
 * ============================================================================ */

/* Returns the value string for design cell (row, col). */
typedef const char *(*doe_value_fn)(void *ctx, size_t row, size_t col);

/* For each of `rows` design points, export <prefix>_<factor>=<value> env vars
 * and run `script` once. Returns 0 on success, -1 on error (err filled). */
int doe_run(const doe_space_t *space, const char *prefix, const char *script,
            size_t rows, doe_value_fn get_value, void *ctx, char *err);

/* Like doe_run, but capture each child's stdout and parse it as a double into
 * responses[row] (the script prints one number). Used by the orchestrator. */
int doe_run_capture(const doe_space_t *space, const char *prefix, const char *script,
                    size_t rows, doe_value_fn get_value, void *ctx,
                    double *responses, char *err);

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
