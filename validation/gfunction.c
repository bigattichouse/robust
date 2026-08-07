/*
 * gfunction.c — see gfunction.h for the definition and its sources.
 */

#include "gfunction.h"

#include <math.h>

double gf_eval(const double *x, const double *a, size_t k) {
    double y = 1.0;
    for (size_t i = 0; i < k; i++) {
        y *= (fabs(4.0 * x[i] - 2.0) + a[i]) / (1.0 + a[i]);
    }
    return y;
}

void gf_partial_var(const double *a, size_t k, double *V_i, double *V_tot) {
    double prod = 1.0;
    for (size_t i = 0; i < k; i++) {
        V_i[i] = (1.0 / 3.0) / ((1.0 + a[i]) * (1.0 + a[i]));
        prod *= (1.0 + V_i[i]);
    }
    *V_tot = prod - 1.0;
}

double gf_first_index(const double *V_i, size_t k, double V_tot, size_t i) {
    (void)k;
    return V_i[i] / V_tot;
}

double gf_total_index(const double *V_i, size_t k, double V_tot, size_t i) {
    double prod_others = 1.0;
    for (size_t j = 0; j < k; j++) if (j != i) prod_others *= (1.0 + V_i[j]);
    return V_i[i] * prod_others / V_tot;
}

double gf_group_first_index(const double *V_i, size_t k, double V_tot,
                            const int *members) {
    double prod_inside = 1.0;
    for (size_t j = 0; j < k; j++) if (members[j]) prod_inside *= (1.0 + V_i[j]);
    return (prod_inside - 1.0) / V_tot;
}

double gf_group_total_index(const double *V_i, size_t k, double V_tot,
                            const int *members) {
    double prod_outside = 1.0;
    for (size_t j = 0; j < k; j++) if (!members[j]) prod_outside *= (1.0 + V_i[j]);
    return 1.0 - (prod_outside - 1.0) / V_tot;
}

double gf_second_index(const double *V_i, size_t k, double V_tot,
                       size_t i, size_t j) {
    (void)k;
    return V_i[i] * V_i[j] / V_tot;
}
