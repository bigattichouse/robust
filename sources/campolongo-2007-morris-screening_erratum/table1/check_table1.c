/*
 * check_table1.c — an independent check of Table 1 in:
 *
 *   Campolongo, F., Cariboni, J., Saltelli, A. (2007).
 *   "An effective screening design for sensitivity analysis of large models."
 *   Environmental Modelling & Software 22(10), 1509-1518.
 *   doi:10.1016/j.envsoft.2006.10.004
 *
 * Self-contained: C99 + libm, no dependencies, no input files, no network.
 *
 *     cc -O2 -std=c99 -o check_table1 check_table1.c -lm && ./check_table1
 *
 * WHAT THIS CHECKS
 * ----------------
 * Table 1 (p.1512) reports, for three test cases on the Sobol' g-function,
 * the group Morris mu* alongside an analytical group total-order Sobol index
 * ("S_T group analytical"). This program recomputes that analytical column
 * two independent ways and compares against the printed values.
 *
 * RESULT: 8 of the 9 printed values reproduce exactly. One does not --
 * test case 3, group v = {X3, X5, X9}, printed as 0.393. Both independent
 * routes give 0.417.
 *
 * The two routes are:
 *
 *   1. The closed form for the g-function's partial variances,
 *          V_i = (1/3) / (1 + a_i)^2,   V = prod_i (1 + V_i) - 1,
 *      from which a group's total index is one minus the share of variance
 *      involving only factors outside the group:
 *          S_T(u) = 1 - [ prod_{j not in u} (1 + V_j) - 1 ] / V
 *      Source: Saltelli, A. & Sobol', I.M. (1995), Reliability Engineering
 *      and System Safety 50, 225-239; the same closed form the paper's own
 *      "analytical" column is derived from.
 *
 *   2. A Monte Carlo estimate that uses no closed form at all:
 *          S_T(u) = ( 1/(2N) sum_n [ f(A_n) - f(A_B^u_n) ]^2 ) / V(Y)
 *      where A and B are independent uniform samples and A_B^u is A with the
 *      columns of group u taken from B.
 *      Source: Jansen, M.J.W. (1999), "Analysis of variance designs for model
 *      output", Computer Physics Communications 117, 35-43.
 *
 * Route 2 depends on neither the closed form nor the paper, so agreement
 * between the two is what makes this a finding rather than an assertion.
 *
 * The program also rules out two innocent explanations for 0.393: that it
 * might be the group's FIRST-order index, and that it might be the total
 * index of some other 3-factor subset (i.e. a mislabelled group).
 *
 * The g-function (paper Sec. 3.3, p.1512), X_i ~ U[0,1] independent:
 *     g(X) = prod_i g_i(X_i),   g_i(X_i) = (|4*X_i - 2| + a_i) / (1 + a_i)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define K 9                 /* factors in every Table 1 test case */
#define MC_SAMPLES 2000000  /* Monte Carlo sample size            */

/* ------------------------------------------------------------------------
 * PRNG — xoshiro256** seeded through splitmix64.
 * David Blackman and Sebastiano Vigna (2018), public domain.
 * http://prng.di.unimi.it/
 * Inlined so this file has no dependencies and the numbers below are
 * reproducible bit-for-bit on any C99 platform.
 * --------------------------------------------------------------------- */

static uint64_t rng_state[4];

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void rng_seed(uint64_t seed) {
    uint64_t s = seed;
    for (int i = 0; i < 4; i++) rng_state[i] = splitmix64(&s);
}

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t rng_next(void) {
    uint64_t *s = rng_state;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* uniform double in [0,1) */
static double rng_uniform(void) {
    return (double)(rng_next() >> 11) * 0x1.0p-53;
}

/* ------------------------------------------------------------------------
 * The g-function and its closed-form indices.
 * --------------------------------------------------------------------- */

static double g_eval(const double *x, const double *a, int k) {
    double y = 1.0;
    for (int i = 0; i < k; i++)
        y *= (fabs(4.0 * x[i] - 2.0) + a[i]) / (1.0 + a[i]);
    return y;
}

static void partial_var(const double *a, int k, double *V_i, double *V_tot) {
    double prod = 1.0;
    for (int i = 0; i < k; i++) {
        V_i[i] = (1.0 / 3.0) / ((1.0 + a[i]) * (1.0 + a[i]));
        prod *= (1.0 + V_i[i]);
    }
    *V_tot = prod - 1.0;
}

/* S_T(u) = 1 - [ prod_{j not in u} (1 + V_j) - 1 ] / V */
static double group_total_index(const double *V_i, int k, double V_tot,
                                const int *members) {
    double prod_outside = 1.0;
    for (int j = 0; j < k; j++)
        if (!members[j]) prod_outside *= (1.0 + V_i[j]);
    return 1.0 - (prod_outside - 1.0) / V_tot;
}

/* S(u) = [ prod_{j in u} (1 + V_j) - 1 ] / V  — first order, for diagnostics */
static double group_first_index(const double *V_i, int k, double V_tot,
                                const int *members) {
    double prod_inside = 1.0;
    for (int j = 0; j < k; j++)
        if (members[j]) prod_inside *= (1.0 + V_i[j]);
    return (prod_inside - 1.0) / V_tot;
}

/* Jansen (1999) Monte Carlo group total index. No closed form used. */
static double mc_group_total_index(const double *a, int k, const int *members,
                                   long N, uint64_t seed) {
    rng_seed(seed);

    double *A = malloc((size_t)N * k * sizeof *A);
    double *B = malloc((size_t)N * k * sizeof *B);
    double *yA = malloc((size_t)N * sizeof *yA);
    if (!A || !B || !yA) { fprintf(stderr, "out of memory\n"); exit(2); }

    for (long i = 0; i < N * k; i++) A[i] = rng_uniform();
    for (long i = 0; i < N * k; i++) B[i] = rng_uniform();

    double mean = 0.0;
    for (long n = 0; n < N; n++) {
        yA[n] = g_eval(&A[n * k], a, k);
        mean += yA[n];
    }
    mean /= (double)N;

    double V = 0.0;
    for (long n = 0; n < N; n++) { double d = yA[n] - mean; V += d * d; }
    V /= (double)(N - 1);

    double acc = 0.0, row[K];
    for (long n = 0; n < N; n++) {
        memcpy(row, &A[n * k], (size_t)k * sizeof *row);
        for (int j = 0; j < k; j++)
            if (members[j]) row[j] = B[n * k + j];
        double d = yA[n] - g_eval(row, a, k);
        acc += d * d;
    }

    free(A); free(B); free(yA);
    return acc / (2.0 * (double)N) / V;
}

/* ------------------------------------------------------------------------
 * Table 1 as printed (p.1512).
 * --------------------------------------------------------------------- */

typedef struct {
    const char *label;
    double a[K];
    int    members[3][K];       /* groups u, v, w as 0/1 masks   */
    const char *gname[3];
    double printed_mustar[3];   /* "Morris group mu*" column     */
    double printed_st[3];       /* "S_T group analytical" column */
} test_case_t;

static const test_case_t CASES[3] = {
    {   /* Table 1, test case 1 */
        "test case 1",
        {0.02, 0.03, 0.05, 11, 12.5, 13, 34, 35, 37},
        { {1,1,1,0,0,0,0,0,0},      /* u = {X1,X2,X3} */
          {0,0,0,1,1,1,0,0,0},      /* v = {X4,X5,X6} */
          {0,0,0,0,0,0,1,1,1} },    /* w = {X7,X8,X9} */
        {"u {X1,X2,X3}", "v {X4,X5,X6}", "w {X7,X8,X9}"},
        {7.948, 1.058, 0.708},
        {0.995, 0.010, 0.001},
    },
    {   /* Table 1, test case 2 */
        "test case 2",
        {0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 34, 35, 37},
        { {1,0,1,0,1,0,0,0,0},      /* u = {X1,X3,X5} */
          {0,1,0,1,0,1,0,0,0},      /* v = {X2,X4,X6} */
          {0,0,0,0,0,0,1,1,1} },    /* w = {X7,X8,X9} */
        {"u {X1,X3,X5}", "v {X2,X4,X6}", "w {X7,X8,X9}"},
        {42.339, 32.656, 2.735},
        {0.694, 0.686, 0.001},
    },
    {   /* Table 1, test case 3 */
        "test case 3",
        {0.02, 0.03, 0.05, 11, 12.5, 13, 34, 35, 37},
        { {1,0,0,1,0,0,0,1,0},      /* u = {X1,X4,X8} */
          {0,0,1,0,1,0,0,0,1},      /* v = {X3,X5,X9} */
          {0,1,0,0,0,1,1,0,0} },    /* w = {X2,X6,X7} */
        {"u {X1,X4,X8}", "v {X3,X5,X9}", "w {X2,X6,X7}"},
        {8.108, 7.083, 6.364},
        {0.436, 0.393, 0.429},
    },
};

/* Values printed to 3 decimals, so agreement is judged at that resolution. */
#define TOL 0.0005

int main(void) {
    printf("=====================================================================\n");
    printf(" Independent check of Table 1 (p.1512) in\n");
    printf("   Campolongo, Cariboni & Saltelli (2007), Env. Modelling & Software\n");
    printf("   22(10) 1509-1518, doi:10.1016/j.envsoft.2006.10.004\n");
    printf("=====================================================================\n\n");
    printf("Recomputing the \"S_T group analytical\" column from the g-function's\n");
    printf("closed-form partial variances (Saltelli & Sobol' 1995).\n\n");

    int mismatches = 0;
    int mm_case = -1, mm_grp = -1;

    for (int c = 0; c < 3; c++) {
        const test_case_t *tc = &CASES[c];
        double V_i[K], V_tot;
        partial_var(tc->a, K, V_i, &V_tot);

        printf("%s   a = {", tc->label);
        for (int i = 0; i < K; i++)
            printf("%s%g", i ? ", " : "", tc->a[i]);
        printf("}\n");
        printf("  %-14s %14s %14s %10s\n",
               "group", "S_T recomputed", "S_T printed", "");
        for (int g = 0; g < 3; g++) {
            double st = group_total_index(V_i, K, V_tot, tc->members[g]);
            int ok = fabs(st - tc->printed_st[g]) <= TOL;
            printf("  %-14s %14.5f %14.3f   %s\n",
                   tc->gname[g], st, tc->printed_st[g],
                   ok ? "agrees" : "<-- DIFFERS");
            if (!ok) { mismatches++; mm_case = c; mm_grp = g; }
        }
        printf("\n");
    }

    printf("---------------------------------------------------------------------\n");
    printf("%d of 9 printed values reproduce; %d %s not.\n\n",
           9 - mismatches, mismatches, mismatches == 1 ? "does" : "do");

    if (mismatches != 1) {
        printf("Expected exactly one mismatch. Environment or transcription differs.\n");
        return 1;
    }

    /* ---- Independent confirmation of the one disagreement ------------- */

    const test_case_t *tc = &CASES[mm_case];
    const int *grp = tc->members[mm_grp];
    double V_i[K], V_tot;
    partial_var(tc->a, K, V_i, &V_tot);

    printf("Confirming %s, group %s by Monte Carlo (Jansen 1999),\n",
           tc->label, tc->gname[mm_grp]);
    printf("which uses no closed form and no value from the paper.\n");
    printf("N = %d samples.\n\n", MC_SAMPLES);

    printf("  %-24s %12s %12s %12s\n",
           "group", "closed form", "Monte Carlo", "printed");
    double cf_bad = 0.0, mc_bad = 0.0;
    for (int g = 0; g < 3; g++) {
        double cf = group_total_index(V_i, K, V_tot, tc->members[g]);
        double mc = mc_group_total_index(tc->a, K, tc->members[g],
                                         MC_SAMPLES, 20260806u + (unsigned)g);
        printf("  %-24s %12.5f %12.5f %12.3f%s\n",
               tc->gname[g], cf, mc, tc->printed_st[g],
               g == mm_grp ? "   <-- DIFFERS" : "");
        /* Keep the disagreeing group's numbers so the conclusion below quotes
         * exactly what this table showed, rather than a fresh estimate. */
        if (g == mm_grp) { cf_bad = cf; mc_bad = mc; }
    }

    printf("\n  The two independent routes agree with each other to %.5f\n",
           fabs(cf_bad - mc_bad));
    printf("  and differ from the printed value by %.3f.\n\n", fabs(cf_bad - tc->printed_st[mm_grp]));

    /* ---- Rule out the innocent explanations --------------------------- */

    printf("Ruling out other readings of the printed %.3f:\n", tc->printed_st[mm_grp]);

    double s_first = group_first_index(V_i, K, V_tot, grp);
    printf("  - first-order index S(v) for the same group : %.5f  (not it)\n", s_first);

    int found = 0;
    for (int i = 0; i < K; i++)
      for (int j = i + 1; j < K; j++)
        for (int m = j + 1; m < K; m++) {
            int mask[K] = {0}; mask[i] = mask[j] = mask[m] = 1;
            double s = group_total_index(V_i, K, V_tot, mask);
            if (fabs(s - tc->printed_st[mm_grp]) < 0.002) {
                printf("  - S_T of {X%d,X%d,X%d} = %.5f  <-- matches\n",
                       i + 1, j + 1, m + 1, s);
                found++;
            }
        }
    if (!found)
        printf("  - no 3-factor subset of this a-vector has S_T = %.3f, so it is\n"
               "    not a mislabelled group either (all %d subsets checked)\n",
               tc->printed_st[mm_grp], K * (K - 1) * (K - 2) / 6);

    printf("\n---------------------------------------------------------------------\n");
    printf("CONCLUSION\n");
    printf("  %s, group %s\n", tc->label, tc->gname[mm_grp]);
    printf("    printed in Table 1 : %.3f\n", tc->printed_st[mm_grp]);
    printf("    closed form        : %.5f\n", cf_bad);
    printf("    Monte Carlo        : %.5f\n", mc_bad);
    printf("  The other two groups in the same row, and all six values in test\n");
    printf("  cases 1 and 2, reproduce exactly. This looks like a typo in the\n");
    printf("  printed table rather than a different definition.\n");
    printf("---------------------------------------------------------------------\n");
    return 0;
}
