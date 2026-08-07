/*
 * stats.c — small statistics helpers shared by the analysis tools.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "doe.h"
#include <math.h>

double doe_mean(const double *x, size_t n) {
    if (n == 0) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; i++) s += x[i];
    return s / (double)n;
}

double doe_variance(const double *x, size_t n) {
    if (n < 2) return 0.0;
    double m = doe_mean(x, n);
    double s = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = x[i] - m;
        s += d * d;
    }
    return s / (double)(n - 1);   /* sample variance */
}

double doe_std(const double *x, size_t n) {
    return sqrt(doe_variance(x, n));
}

/* ---- quantiles ---------------------------------------------------------- */

static int cmp_asc(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

double doe_quantile(double *x, size_t n, double q) {
    if (!x || n == 0) return 0.0;
    qsort(x, n, sizeof *x, cmp_asc);
    if (q <= 0.0) return x[0];
    if (q >= 1.0) return x[n - 1];
    /* Linear interpolation between order statistics (type 7, as R and numpy
     * default to), so results match what people compare against. */
    double h = ((double)n - 1.0) * q;
    size_t lo = (size_t)h;
    size_t hi = (lo + 1 < n) ? lo + 1 : n - 1;
    double frac = h - (double)lo;
    return x[lo] + frac * (x[hi] - x[lo]);
}

double doe_median(double *x, size_t n) { return doe_quantile(x, n, 0.5); }

/* ---- ranks -------------------------------------------------------------- */

/* Ranks of one column, ties averaged. O(n^2); n here is a design size. */
static void rank_column(double *v, size_t n) {
    double *out = malloc(n * sizeof *out);
    if (!out) return;
    for (size_t i = 0; i < n; i++) {
        double r = 1.0;
        size_t ties = 0;
        for (size_t j = 0; j < n; j++) {
            if (v[j] < v[i]) r += 1.0;
            if (v[j] == v[i]) ties++;
        }
        out[i] = r + ((double)ties - 1.0) / 2.0;
    }
    memcpy(v, out, n * sizeof *out);
    free(out);
}

void doe_rank_transform(double *X, double *y, size_t n, size_t k) {
    double *col = malloc(n * sizeof *col);
    if (!col) return;
    for (size_t j = 0; j < k; j++) {
        for (size_t i = 0; i < n; i++) col[i] = X[i * k + j];
        rank_column(col, n);
        for (size_t i = 0; i < n; i++) X[i * k + j] = col[i];
    }
    free(col);
    if (y) rank_column(y, n);
}

/* ---- ordinary least squares --------------------------------------------- */

int doe_ols_src(const double *X, const double *y, size_t n, size_t k,
                double *coef_out, double *r2_out, char *err) {
    if (!X || !y || !coef_out || n == 0 || k == 0) {
        snprintf(err, DOE_ERR_SIZE, "null or empty input to doe_ols_src");
        return -1;
    }
    if (n <= k) {
        snprintf(err, DOE_ERR_SIZE,
                 "need more runs than factors for a regression: %zu runs, %zu factors",
                 n, k);
        return -1;
    }

    /* Column means and standard deviations. A constant column carries no
     * information and makes the normal equations singular, so refuse by name
     * rather than return a meaningless coefficient. */
    double *mx = calloc(k, sizeof *mx), *sx = calloc(k, sizeof *sx);
    if (!mx || !sx) { free(mx); free(sx);
                      snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }
    for (size_t j = 0; j < k; j++) {
        double m = 0.0;
        for (size_t i = 0; i < n; i++) m += X[i * k + j];
        m /= (double)n;
        double v = 0.0;
        for (size_t i = 0; i < n; i++) { double d = X[i * k + j] - m; v += d * d; }
        v /= (double)(n - 1);
        mx[j] = m; sx[j] = sqrt(v);
        if (!(sx[j] > 0.0)) {
            snprintf(err, DOE_ERR_SIZE,
                     "factor column %zu is constant across every run; it cannot "
                     "have a coefficient", j);
            free(mx); free(sx);
            return -1;
        }
    }

    double my = doe_mean(y, n);
    double sy = doe_std(y, n);
    if (!(sy > 0.0)) {
        snprintf(err, DOE_ERR_SIZE,
                 "the response is constant across every run; a regression on it "
                 "is undefined (check the model actually depends on the factors)");
        free(mx); free(sx);
        return -1;
    }

    /* Normal equations on the standardized data: (Z'Z) b = Z'w. */
    double *A = calloc(k * k, sizeof *A);
    double *b = calloc(k, sizeof *b);
    if (!A || !b) { free(A); free(b); free(mx); free(sx);
                    snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }

    for (size_t i = 0; i < n; i++) {
        double w = (y[i] - my) / sy;
        for (size_t p = 0; p < k; p++) {
            double zp = (X[i * k + p] - mx[p]) / sx[p];
            b[p] += zp * w;
            for (size_t q = p; q < k; q++) {
                double zq = (X[i * k + q] - mx[q]) / sx[q];
                A[p * k + q] += zp * zq;
            }
        }
    }
    for (size_t p = 0; p < k; p++)
        for (size_t q = 0; q < p; q++) A[p * k + q] = A[q * k + p];

    /* Gaussian elimination with partial pivoting. */
    int rc = 0;
    for (size_t c = 0; c < k; c++) {
        size_t piv = c;
        double best = fabs(A[c * k + c]);
        for (size_t r = c + 1; r < k; r++) {
            double v = fabs(A[r * k + c]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) {
            snprintf(err, DOE_ERR_SIZE,
                     "design is rank-deficient at factor %zu: two or more factors "
                     "move together, so their effects cannot be separated", c);
            rc = -1; break;
        }
        if (piv != c) {
            for (size_t j = 0; j < k; j++) {
                double t = A[c * k + j]; A[c * k + j] = A[piv * k + j]; A[piv * k + j] = t;
            }
            double t = b[c]; b[c] = b[piv]; b[piv] = t;
        }
        for (size_t r = c + 1; r < k; r++) {
            double f = A[r * k + c] / A[c * k + c];
            if (f == 0.0) continue;
            for (size_t j = c; j < k; j++) A[r * k + j] -= f * A[c * k + j];
            b[r] -= f * b[c];
        }
    }
    if (rc == 0) {
        for (size_t ci = k; ci-- > 0; ) {
            double sum = b[ci];
            for (size_t j = ci + 1; j < k; j++) sum -= A[ci * k + j] * coef_out[j];
            coef_out[ci] = sum / A[ci * k + ci];
        }

        /* R^2 on the standardized scale: 1 - SSE/SST, and SST is n-1 there. */
        double sse = 0.0;
        for (size_t i = 0; i < n; i++) {
            double pred = 0.0;
            for (size_t j = 0; j < k; j++)
                pred += coef_out[j] * (X[i * k + j] - mx[j]) / sx[j];
            double resid = (y[i] - my) / sy - pred;
            sse += resid * resid;
        }
        double sst = (double)(n - 1);
        if (r2_out) *r2_out = 1.0 - sse / sst;
    }

    free(A); free(b); free(mx); free(sx);
    return rc;
}
