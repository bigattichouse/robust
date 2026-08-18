/*
 * rsm — response surface methodology (EXPANSION.md E4).
 *
 * The stage after screening and attribution. Those answer "which factors
 * matter"; this answers "what setting is best", by fitting a QUADRATIC surface
 * over the two or three survivors and solving for its stationary point.
 *
 *   rsm sample  model.space                  -> a central composite design
 *   rsm analyze model.space results.csv      -> fit, optimum, and a verdict
 *
 * A quadratic is the smallest model that can have an interior optimum, which
 * is the whole reason for the stage: a linear fit always points at a corner,
 * so it can rank factors but never locate a peak.
 *
 * The design is a CENTRAL COMPOSITE: the 2^k factorial corners (which estimate
 * the interactions), 2k axial points at +/-alpha (which estimate the pure
 * quadratic terms -- corners alone cannot, since every coordinate is +/-1 and
 * x^2 is 1 everywhere), and centre replicates. alpha = (2^k)^(1/4) makes it
 * rotatable: prediction variance depends on distance from the centre and not
 * on direction, so the fit is equally trustworthy whichever way the optimum
 * turns out to lie.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RSM_MAX_FACTORS 3        /* k=3 is 6 corners of terms; beyond that a
                                  * CCD costs more than the stage is worth */
#define RSM_CENTRE_RUNS 3
#define RSM_JSON_SCHEMA 1

/* ---- linear algebra, small and explicit ---------------------------------
 *
 * Everything here is at most 10x10 (k=3 gives 10 quadratic terms), so a plain
 * Gaussian elimination with partial pivoting is the right tool: no iteration,
 * no library, and a singular system is detected rather than approximated.
 */
static int solve(double *A, double *b, size_t n) {
    for (size_t col = 0; col < n; col++) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; r++)
            if (fabs(A[r * n + col]) > fabs(A[piv * n + col])) piv = r;

        if (fabs(A[piv * n + col]) < 1e-12) return -1;      /* singular */

        if (piv != col) {
            for (size_t c = 0; c < n; c++) {
                double t = A[col * n + c]; A[col * n + c] = A[piv * n + c]; A[piv * n + c] = t;
            }
            double t = b[col]; b[col] = b[piv]; b[piv] = t;
        }
        for (size_t r = col + 1; r < n; r++) {
            double f = A[r * n + col] / A[col * n + col];
            if (f == 0.0) continue;
            for (size_t c = col; c < n; c++) A[r * n + c] -= f * A[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for (size_t i = n; i-- > 0; ) {
        double acc = b[i];
        for (size_t c = i + 1; c < n; c++) acc -= A[i * n + c] * b[c];
        b[i] = acc / A[i * n + i];
    }
    return 0;
}

/* ---- the design --------------------------------------------------------- */

/* Coded points in [-1,1] plus axial reach; caller maps to real values. */
static size_t ccd_points(size_t k, double *out, size_t cap) {
    size_t corners = (size_t)1 << k;
    double alpha = pow((double)corners, 0.25);
    size_t n = 0;

    for (size_t i = 0; i < corners; i++) {
        if (n >= cap) return 0;
        for (size_t f = 0; f < k; f++)
            out[n * k + f] = (i & ((size_t)1 << f)) ? 1.0 : -1.0;
        n++;
    }
    for (size_t f = 0; f < k; f++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (n >= cap) return 0;
            for (size_t g = 0; g < k; g++) out[n * k + g] = 0.0;
            out[n * k + f] = sign * alpha;
            n++;
        }
    }
    for (size_t c = 0; c < RSM_CENTRE_RUNS; c++) {
        if (n >= cap) return 0;
        for (size_t f = 0; f < k; f++) out[n * k + f] = 0.0;
        n++;
    }
    return n;
}

/* Coded [-alpha, alpha] -> the factor's real range. The corners sit at the
 * range's edges, so the axial points reach beyond it -- that is what alpha
 * means, and the .space bounds have to be the region you can actually run. */
static double decode(const doe_space_t *sp, size_t f, double coded, double alpha) {
    double u = 0.5 + 0.5 * (coded / alpha);
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    char buf[DOE_MAX_VALUE];
    return strtod(doe_factor_value(sp, f, u, buf, sizeof buf), NULL);
}

/* ---- the quadratic model ------------------------------------------------
 *
 * Terms, in this order: 1, x_i, x_i^2, x_i x_j (i<j).
 */
static size_t term_count(size_t k) { return 1 + k + k + k * (k - 1) / 2; }

static void terms_of(const double *x, size_t k, double *row) {
    size_t t = 0;
    row[t++] = 1.0;
    for (size_t i = 0; i < k; i++) row[t++] = x[i];
    for (size_t i = 0; i < k; i++) row[t++] = x[i] * x[i];
    for (size_t i = 0; i < k; i++)
        for (size_t j = i + 1; j < k; j++) row[t++] = x[i] * x[j];
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f);
    b[got] = '\0';
    return b;
}

static int load_space(const char *path, doe_space_t *sp) {
    char *c = read_all(path);
    if (!c) { fprintf(stderr, "Error: cannot open '%s'\n", path); return -1; }
    char err[DOE_ERR_SIZE];
    int rc = doe_space_parse(c, sp, err);
    free(c);
    if (rc != 0) { fprintf(stderr, "Error parsing %s: %s\n", path, err); return -1; }
    if (sp->factor_count < 2 || sp->factor_count > RSM_MAX_FACTORS) {
        fprintf(stderr, "Error: rsm needs 2 or %d factors, got %zu. Screen first;\n"
                        "a response surface over everything is the full factorial\n"
                        "this toolkit exists to avoid.\n",
                RSM_MAX_FACTORS, sp->factor_count);
        return -1;
    }
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s sample  <file.space>\n"
        "       %s analyze <file.space> <results.csv> [--metric NAME] [--minimize] [--json]\n"
        "\n"
        "  Response surface methodology: fits a quadratic over 2-3 factors and\n"
        "  solves for the stationary point -- the setting the surface says is\n"
        "  best, which a linear fit can never locate.\n"
        "\n"
        "  sample   central composite design (corners, axial points, centres)\n"
        "  analyze  fit, stationary point, and whether it is a max, min or saddle\n",
        prog, prog);
}

static int cmd_sample(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    size_t k = sp.factor_count;
    double alpha = pow((double)((size_t)1 << k), 0.25);

    double pts[64 * RSM_MAX_FACTORS];
    size_t n = ccd_points(k, pts, 64);
    if (n == 0) { fprintf(stderr, "Error: design too large\n"); return 1; }

    printf("run_id");
    for (size_t f = 0; f < k; f++) printf(",%s", sp.factors[f].name);
    printf("\n");
    for (size_t i = 0; i < n; i++) {
        printf("%zu", i + 1);
        for (size_t f = 0; f < k; f++)
            printf(",%.10g", decode(&sp, f, pts[i * k + f], alpha));
        printf("\n");
    }
    return 0;
}

static int cmd_analyze(const char *path, const char *csv, const char *metric,
                       int minimize, int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    size_t k = sp.factor_count;
    double alpha = pow((double)((size_t)1 << k), 0.25);

    double pts[64 * RSM_MAX_FACTORS];
    size_t n = ccd_points(k, pts, 64);
    if (n == 0) { fprintf(stderr, "Error: design too large\n"); return 1; }

    double *y = malloc(n * sizeof *y);
    if (!y) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    char err[DOE_ERR_SIZE];
    /* NaN-fills, and refuses a run the file repeats -- a duplicated row used to
     * overwrite the earlier value silently. */
    if (doe_csv_read_design(csv, metric, y, n, err) != 0) {
        fprintf(stderr, "Error reading results: %s\n", err);
        free(y);
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isfinite(y[i])) {
            fprintf(stderr, "Error: missing or non-finite response for run %zu.\n"
                            "The design has %zu runs; a quadratic fit needs all of them.\n",
                    i + 1, n);
            free(y);
            return 1;
        }
    }

    /* Normal equations for the quadratic model. */
    size_t p = term_count(k);
    double *A = calloc(p * p, sizeof *A);
    double *b = calloc(p, sizeof *b);
    double row[16];
    if (!A || !b) { fprintf(stderr, "Error: out of memory\n"); return 1; }

    for (size_t i = 0; i < n; i++) {
        terms_of(&pts[i * k], k, row);
        for (size_t r = 0; r < p; r++) {
            b[r] += row[r] * y[i];
            for (size_t c = 0; c < p; c++) A[r * p + c] += row[r] * row[c];
        }
    }
    if (solve(A, b, p) != 0) {
        fprintf(stderr, "Error: the fit is rank-deficient -- this design cannot\n"
                        "identify a quadratic in %zu factors. That usually means the\n"
                        "results were not produced by `rsm sample`.\n", k);
        free(A); free(b); free(y);
        return 1;
    }
    /* b now holds the coefficients, in terms_of order. */
    double b0 = b[0];
    const double *lin = &b[1];
    const double *quad = &b[1 + k];
    const double *cross = &b[1 + 2 * k];

    /*
     * Stationary point: grad y = 0.
     *
     *   dy/dx_i = lin_i + 2*quad_i*x_i + sum_{j!=i} cross_ij * x_j = 0
     *
     * so 2B x = -lin with B_ii = quad_i and B_ij = cross_ij / 2.
     */
    double B[RSM_MAX_FACTORS * RSM_MAX_FACTORS] = {0};
    for (size_t i = 0; i < k; i++) B[i * k + i] = quad[i];
    {
        size_t t = 0;
        for (size_t i = 0; i < k; i++)
            for (size_t j = i + 1; j < k; j++) {
                B[i * k + j] = cross[t] / 2.0;
                B[j * k + i] = cross[t] / 2.0;
                t++;
            }
    }

    double M[RSM_MAX_FACTORS * RSM_MAX_FACTORS], xs[RSM_MAX_FACTORS];
    for (size_t i = 0; i < k * k; i++) M[i] = 2.0 * B[i];
    for (size_t i = 0; i < k; i++) xs[i] = -lin[i];

    int flat = (solve(M, xs, k) != 0);

    /*
     * What KIND of stationary point, from the definiteness of B. Sylvester's
     * criterion on the leading principal minors settles it without an
     * eigensolver: all positive => positive definite => a minimum; alternating
     * from negative => negative definite => a maximum; anything else is a
     * saddle, and a saddle means there is no interior optimum to report.
     */
    const char *kind = "saddle";
    if (!flat) {
        double d[RSM_MAX_FACTORS];
        for (size_t m = 1; m <= k; m++) {
            double sub[RSM_MAX_FACTORS * RSM_MAX_FACTORS], rhs[RSM_MAX_FACTORS];
            for (size_t i = 0; i < m; i++) {
                for (size_t j = 0; j < m; j++) sub[i * m + j] = B[i * k + j];
                rhs[i] = 0.0;
            }
            /* determinant by the same elimination, tracking the pivots */
            double det = 1.0;
            for (size_t col = 0; col < m; col++) {
                size_t piv = col;
                for (size_t r = col + 1; r < m; r++)
                    if (fabs(sub[r * m + col]) > fabs(sub[piv * m + col])) piv = r;
                if (fabs(sub[piv * m + col]) < 1e-12) { det = 0.0; break; }
                if (piv != col) {
                    for (size_t c = 0; c < m; c++) {
                        double t = sub[col * m + c]; sub[col * m + c] = sub[piv * m + c]; sub[piv * m + c] = t;
                    }
                    det = -det;
                }
                det *= sub[col * m + col];
                for (size_t r = col + 1; r < m; r++) {
                    double f = sub[r * m + col] / sub[col * m + col];
                    for (size_t c = col; c < m; c++) sub[r * m + c] -= f * sub[col * m + c];
                }
            }
            (void)rhs;
            d[m - 1] = det;
        }
        int pos = 1, neg = 1;
        for (size_t m = 0; m < k; m++) {
            if (!(d[m] > 0)) pos = 0;
            double want = ((m % 2) == 0) ? -1.0 : 1.0;
            if (!(d[m] * want > 0)) neg = 0;
        }
        if (pos)      kind = "minimum";
        else if (neg) kind = "maximum";
    }

    /* Predicted response at the stationary point. */
    double y_s = b0;
    if (!flat) {
        terms_of(xs, k, row);
        y_s = 0.0;
        for (size_t t = 0; t < p; t++) y_s += b[t] * row[t];
    }

    int wanted = minimize ? (strcmp(kind, "minimum") == 0)
                          : (strcmp(kind, "maximum") == 0);
    int inside = 1;
    for (size_t i = 0; i < k; i++) if (fabs(xs[i]) > alpha) inside = 0;

    if (as_json) {
        char nb[DOE_JSON_NUM], sb[DOE_JSON_STR(DOE_MAX_NAME)];
        printf("{\n  \"tool\": \"rsm\",\n  \"command\": \"analyze\",\n");
        printf("  \"schema\": %d,\n", RSM_JSON_SCHEMA);
        printf("  \"metric\": %s,\n", doe_json_string(metric, sb, sizeof sb));
        printf("  \"objective\": \"%s\",\n", minimize ? "minimize" : "maximize");
        printf("  \"runs\": %zu,\n", n);
        printf("  \"stationary_point_kind\": \"%s\",\n", flat ? "none" : kind);
        printf("  \"is_the_optimum_sought\": %s,\n", (!flat && wanted) ? "true" : "false");
        printf("  \"within_design_region\": %s,\n", (!flat && inside) ? "true" : "false");
        printf("  \"predicted\": %s,\n",
               flat ? "null" : doe_json_number(y_s, nb, sizeof nb));
        printf("  \"settings\": [\n");
        for (size_t i = 0; i < k; i++) {
            printf("    {\"factor\": %s, \"coded\": %s",
                   doe_json_string(sp.factors[i].name, sb, sizeof sb),
                   flat ? "null" : doe_json_number(xs[i], nb, sizeof nb));
            if (!flat) {
                char b2[DOE_JSON_NUM];
                printf(", \"value\": %s",
                       doe_json_number(decode(&sp, i, xs[i], alpha), b2, sizeof b2));
            } else {
                printf(", \"value\": null");
            }
            printf("}%s\n", i + 1 < k ? "," : "");
        }
        printf("  ]\n}\n");
    } else {
        printf("Response surface for '%s' (%s), %zu runs\n\n",
               metric, minimize ? "minimizing" : "maximizing", n);
        if (flat) {
            printf("No stationary point: the fitted surface has no turning point in\n"
                   "these factors -- it is a plane, or close enough that the quadratic\n"
                   "terms cannot be told from zero. The optimum is on a boundary, so\n"
                   "widen the ranges or use `grid`.\n");
        } else {
            printf("Stationary point is a %s%s\n", kind,
                   inside ? "" : "  (OUTSIDE the design region)");
            printf("Predicted %s: %.6g\n\n", metric, y_s);
            printf("%-20s %12s %14s\n", "factor", "coded", "value");
            printf("%-20s %12s %14s\n", "------", "-----", "-----");
            for (size_t i = 0; i < k; i++)
                printf("%-20s %12.4g %14.6g\n", sp.factors[i].name, xs[i],
                       decode(&sp, i, xs[i], alpha));

            if (!wanted)
                printf("\nThis is a %s and you asked to %s. The surface turns the wrong\n"
                       "way: the best setting is on the edge of the region, not inside\n"
                       "it. Move the ranges toward the direction that improves and\n"
                       "re-run.\n", kind, minimize ? "minimize" : "maximize");
            if (!inside)
                printf("\nThe stationary point lies outside the region actually run, so\n"
                       "the value above is EXTRAPOLATION. Re-centre the ranges on it and\n"
                       "run again before believing it.\n");
        }
    }

    free(A); free(b); free(y);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "sample") == 0) return cmd_sample(argv[2]);

    if (strcmp(cmd, "analyze") == 0) {
        if (argc < 4) { fprintf(stderr, "analyze needs a results.csv\n"); return 2; }
        const char *metric = "response";
        int minimize = 0, as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric = argv[++i];
            else if (strcmp(argv[i], "--minimize") == 0) minimize = 1;
            else if (strcmp(argv[i], "--json") == 0) as_json = 1;
            else { fprintf(stderr, "Error: unknown option '%s'\n", argv[i]); usage(argv[0]); return 2; }
        }
        return cmd_analyze(argv[2], argv[3], metric, minimize, as_json);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 2;
}
