/*
 * stats.c — small statistics helpers shared by the analysis tools.
 */

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
