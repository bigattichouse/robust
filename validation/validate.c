/*
 * validate.c — reproduces published sensitivity-analysis results against
 * closed-form ground truth, using this repo's own PRNG and Morris math.
 *
 * This suite exists because EXPANSION_NOTE.md carried three load-bearing
 * claims taken from secondary sources. The house rule (EXPANSION.md) is
 * "validation against a closed-form reference before it ships", and that
 * rule applies to claims about methods just as much as to code.
 *
 * SOURCES
 *   [C07]  Campolongo, F., Cariboni, J., Saltelli, A. (2007). "An effective
 *          screening design for sensitivity analysis of large models."
 *          Environmental Modelling & Software 22(10), 1509-1518.
 *          sources/pdf/campolongo-2007-morris-screening.pdf
 *   [M91]  Morris, M.D. (1991). "Factorial sampling plans for preliminary
 *          computational experiments." Technometrics 33, 161-174.
 *   [J99]  Jansen, M.J.W. (1999). "Analysis of variance designs for model
 *          output." Computer Physics Communications 117, 35-43.
 *   [SS95] Saltelli & Sobol' (1995). Rel. Eng. & System Safety 50, 225-239.
 *
 * WHAT IS TESTED
 *   A. mu* as a proxy for the total-order index S_T ([C07] Sec. 4, Sec. 6).
 *      Separates two questions the note conflated: does mu* rank like S_T
 *      (yes), and is there a usable constant of proportionality (no).
 *   B. Group mu* ([C07] Sec. 3.3, Table 1) -- group screening at r*(G+1)
 *      cost with no monotonicity or sign assumption.
 *   C. Ranking fidelity vs trajectory budget -- where mu* can and cannot be
 *      trusted, and why that is a property of the index gap, not the budget.
 *   D. An erratum in [C07] Table 1, confirmed by two independent routes.
 */

#include "gfunction.h"
#include "doe.h"
#include "morris.h"
#include "sobol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXK 32

static int failures = 0;

static void report(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " — " : "", detail ? detail : "");
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ stats */

static void rank_of(const double *x, size_t n, double *rank) {
    for (size_t i = 0; i < n; i++) {
        double r = 1.0; size_t ties = 0;
        for (size_t j = 0; j < n; j++) {
            if (x[j] < x[i]) r += 1.0;
            if (x[j] == x[i]) ties++;
        }
        rank[i] = r + ((double)ties - 1.0) / 2.0;
    }
}

static double pearson(const double *x, const double *y, size_t n) {
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; i++) { mx += x[i]; my += y[i]; }
    mx /= (double)n; my /= (double)n;
    double sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < n; i++) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
    }
    return sxy / sqrt(sxx * syy);
}

static double spearman(const double *x, const double *y, size_t n) {
    double rx[MAXK], ry[MAXK];
    rank_of(x, n, rx); rank_of(y, n, ry);
    return pearson(rx, ry, n);
}

/* Do the top-m sets coincide (as sets, ignoring order within)? */
static int topm_agrees(const double *a1, const double *a2, size_t n, size_t m) {
    int u1[MAXK] = {0}, u2[MAXK] = {0};
    for (int pass = 0; pass < 2; pass++) {
        const double *key = pass ? a2 : a1;
        int *u = pass ? u2 : u1;
        for (size_t r = 0; r < m; r++) {
            size_t best = 0; double bv = -1e300;
            for (size_t i = 0; i < n; i++)
                if (!u[i] && key[i] > bv) { bv = key[i]; best = i; }
            u[best] = 1;
        }
    }
    for (size_t i = 0; i < n; i++) if (u1[i] != u2[i]) return 0;
    return 1;
}

/* ------------------------------------------------- Morris mu*, per factor */

static size_t rng_below(doe_rng_t *rng, size_t n) {
    size_t x = (size_t)(doe_rng_uniform(rng) * (double)n);
    return x >= n ? n - 1 : x;
}

/*
 * Per-factor mu* on the g-function. Trajectory construction mirrors
 * morris/src/lib/morris.c exactly (grid spacing 1/(p-1), Delta = p/(2(p-1)),
 * base drawn from the lower half so every point stays in [0,1]) [M91].
 */
static void mustar_per_factor(const double *a, size_t k, size_t r, size_t p,
                              uint64_t seed, double *mustar) {
    const double step  = 1.0 / (double)(p - 1);
    const double Delta = (double)p / (2.0 * (double)(p - 1));
    const size_t nbase = p / 2;
    doe_rng_t rng; doe_rng_seed(&rng, seed);

    double acc[MAXK] = {0};
    double x[MAXK]; size_t perm[MAXK]; double base[MAXK]; int dir[MAXK];

    for (size_t t = 0; t < r; t++) {
        for (size_t i = 0; i < k; i++) perm[i] = i;
        for (size_t i = k; i > 1; i--) {
            size_t j = rng_below(&rng, i);
            size_t tmp = perm[i-1]; perm[i-1] = perm[j]; perm[j] = tmp;
        }
        for (size_t s = 0; s < k; s++) {
            base[s] = (double)rng_below(&rng, nbase) * step;
            dir[s]  = (doe_rng_uniform(&rng) < 0.5) ? -1 : +1;
        }
        for (size_t s = 0; s < k; s++) {
            size_t j = perm[s];
            x[j] = (dir[s] > 0) ? base[s] : base[s] + Delta;
        }
        double y_prev = gf_eval(x, a, k);
        for (size_t s = 0; s < k; s++) {
            size_t j = perm[s];
            double delta = (double)dir[s] * Delta;
            x[j] += delta;
            double y_cur = gf_eval(x, a, k);
            acc[j] += fabs((y_cur - y_prev) / delta);
            y_prev = y_cur;
        }
    }
    for (size_t i = 0; i < k; i++) mustar[i] = acc[i] / (double)r;
}

/* -------------------------------------------------- Group mu* — prototype
 *
 * [C07] Sec. 3.3, p.1512. For a group u, the absolute group elementary
 * effect at X is
 *
 *     |d_u(X)| = | y(X~) - y(X) | / Delta
 *
 * where X~ has EVERY factor of u moved by +/-Delta. Factors within a group
 * may move in opposite directions; taking the absolute value of the group
 * effect is what makes that safe. This is the property that matters most for
 * screening: a group containing offsetting important factors still registers,
 * so the method cannot produce the silent sign-cancellation false negative
 * that sequential bifurcation is vulnerable to.
 *
 * Cost is r*(G+1) evaluations for G groups, against r*(k+1) for k factors.
 *
 * This is the reference implementation for the planned `morris --groups`;
 * see spec/morris-groups.bp.
 */
typedef struct {
    const char *name;
    int members[MAXK];      /* 0/1 mask over the k factors */
} group_t;

static size_t group_mustar(const double *a, size_t k,
                           const group_t *groups, size_t G,
                           size_t r, size_t p, uint64_t seed,
                           double *mustar_out) {
    const double step  = 1.0 / (double)(p - 1);
    const double Delta = (double)p / (2.0 * (double)(p - 1));
    const size_t nbase = p / 2;
    doe_rng_t rng; doe_rng_seed(&rng, seed);

    double *acc = calloc(G, sizeof *acc);
    size_t runs = 0;
    double x[MAXK]; size_t perm[MAXK]; double base[MAXK]; int dir[MAXK];

    for (size_t t = 0; t < r; t++) {
        for (size_t i = 0; i < G; i++) perm[i] = i;
        for (size_t i = G; i > 1; i--) {
            size_t j = rng_below(&rng, i);
            size_t tmp = perm[i-1]; perm[i-1] = perm[j]; perm[j] = tmp;
        }
        for (size_t i = 0; i < k; i++) {
            base[i] = (double)rng_below(&rng, nbase) * step;
            dir[i]  = (doe_rng_uniform(&rng) < 0.5) ? -1 : +1;
            x[i] = (dir[i] > 0) ? base[i] : base[i] + Delta;
        }
        double y_prev = gf_eval(x, a, k);
        runs++;
        for (size_t s = 0; s < G; s++) {
            const group_t *gr = &groups[perm[s]];
            for (size_t i = 0; i < k; i++)
                if (gr->members[i]) x[i] += (double)dir[i] * Delta;
            double y_cur = gf_eval(x, a, k);
            runs++;
            acc[perm[s]] += fabs(y_cur - y_prev) / Delta;
            y_prev = y_cur;
        }
    }
    for (size_t i = 0; i < G; i++) mustar_out[i] = acc[i] / (double)r;
    free(acc);
    return runs;
}

/* ------------------------------------- Monte Carlo group S_T (Jansen [J99])
 *
 *   S_T(u) = ( 1/(2N) sum_n [ f(A_n) - f(A_B^u_n) ]^2 ) / V(Y)
 *
 * where A_B^u is A with the columns of group u taken from B. Depends on
 * neither the closed form nor any published value: an independent referee.
 */
static double mc_group_st(const double *a, size_t k, const int *members,
                          size_t N, uint64_t seed) {
    doe_rng_t rng; doe_rng_seed(&rng, seed);
    double *A = malloc(N * k * sizeof *A);
    double *B = malloc(N * k * sizeof *B);
    for (size_t i = 0; i < N * k; i++) A[i] = doe_rng_uniform(&rng);
    for (size_t i = 0; i < N * k; i++) B[i] = doe_rng_uniform(&rng);

    double *yA = malloc(N * sizeof *yA);
    double mean = 0.0;
    for (size_t n = 0; n < N; n++) { yA[n] = gf_eval(&A[n*k], a, k); mean += yA[n]; }
    mean /= (double)N;
    double V = 0.0;
    for (size_t n = 0; n < N; n++) { double d = yA[n] - mean; V += d * d; }
    V /= (double)(N - 1);

    double acc = 0.0, row[MAXK];
    for (size_t n = 0; n < N; n++) {
        memcpy(row, &A[n*k], k * sizeof *row);
        for (size_t j = 0; j < k; j++) if (members[j]) row[j] = B[n*k + j];
        double d = yA[n] - gf_eval(row, a, k);
        acc += d * d;
    }
    free(A); free(B); free(yA);
    return acc / (2.0 * (double)N) / V;
}

/* ===================================================================== A */

static void claim_a(void) {
    /* [C07] Table 2, p.1514 — the 12-factor g-function case of Fig. 7. */
    static const double a[12] = {
        0.001, 89.9, 5.54, 42.10, 0.78, 1.26,
        0.04,  0.79, 74.51, 4.32, 82.51, 41.62
    };
    const size_t k = 12;

    printf("A. mu* as a proxy for S_T   [C07] Sec.4 p.1513, Sec.6 p.1517\n");
    printf("   g-function, k=12, a from [C07] Table 2. r=10 => 130 runs,\n");
    printf("   the same budget as [C07] Fig. 7 (N=130).\n\n");

    /* Drive the repo's real morris library, not a local copy, so this also
     * pins the shipped implementation. */
    char spec[4096];
    int off = snprintf(spec, sizeof spec, "factors:\n");
    for (size_t i = 0; i < k; i++)
        off += snprintf(spec + off, sizeof spec - (size_t)off,
                        "  x%zu: 0.0, 1.0\n", i + 1);
    snprintf(spec + off, sizeof spec - (size_t)off,
             "seed: 2026\ntrajectories: 10\ngrid_levels: 4\n");

    doe_space_t sp; char err[DOE_ERR_SIZE];
    if (doe_space_parse(spec, &sp, err) != 0) { printf("  parse: %s\n", err); failures++; return; }

    morris_design_t d;
    if (morris_design_build(&sp, &d, err) != 0) { printf("  design: %s\n", err); failures++; return; }
    double *y = malloc(d.npoints * sizeof *y);
    for (size_t i = 0; i < d.npoints; i++) y[i] = gf_eval(&d.u[i * d.k], a, k);

    morris_effect_t *eff = NULL; size_t n = 0;
    if (morris_analyze(&sp, y, d.npoints, &eff, &n, err) != 0) {
        printf("  analyze: %s\n", err); failures++; free(y); morris_design_free(&d); return;
    }

    double V_i[12], V_tot;
    gf_partial_var(a, k, V_i, &V_tot);

    double mustar[12], st[12];
    printf("   %-5s %9s %10s %11s %11s\n", "fac", "a_i", "mu*", "S_i", "S_T");
    for (size_t i = 0; i < k; i++) {
        mustar[i] = eff[i].mu_star;
        st[i] = gf_total_index(V_i, k, V_tot, i);
        printf("   %-5s %9.3f %10.4f %11.5f %11.5f\n", eff[i].name, a[i],
               mustar[i], gf_first_index(V_i, k, V_tot, i), st[i]);
    }

    double rho = spearman(mustar, st, k);
    double lo = 1e300, hi = -1e300;
    for (size_t i = 0; i < k; i++) {
        double ratio = mustar[i] / st[i];
        if (ratio < lo) lo = ratio;
        if (ratio > hi) hi = ratio;
    }
    printf("\n   Spearman(mu*, S_T) = %.4f    ratio mu*/S_T spread = %.1fx\n\n",
           rho, hi / lo);

    char buf[160];
    snprintf(buf, sizeof buf, "Spearman %.3f >= 0.85", rho);
    report("mu* ranks like S_T", rho >= 0.85, buf);

    snprintf(buf, sizeof buf,
             "ratio varies %.0fx across factors, so no constant recovers S_T's value", hi / lo);
    report("mu* does NOT give S_T's magnitude", hi / lo > 10.0, buf);

    free(y); free(eff); morris_design_free(&d);
    printf("\n");
}

/* ===================================================================== B */

/*
 * `expected_st` is what the analytic column SHOULD read. It equals [C07]'s
 * printed value everywhere except test case 3 group v, where the printed
 * 0.393 is an erratum: check D confirms 0.417 by closed form and by an
 * independent Monte Carlo. Encoding the correction here rather than the
 * printed value keeps the suite green against the truth instead of green
 * against a typo -- and if anyone ever "fixes" our closed form to match the
 * paper, this is where it will fail.
 */
static void one_group_case(const char *label, const double *a, size_t k,
                           const group_t *groups, size_t G,
                           const double *published_mu,
                           const double *published_st,
                           const double *expected_st) {
    double V_i[MAXK], V_tot;
    gf_partial_var(a, k, V_i, &V_tot);

    double mustar[8], st[8];
    size_t runs = group_mustar(a, k, groups, G, 10, 4, 2026, mustar);

    printf("   %s — %zu runs (per-factor would need %zu)\n",
           label, runs, 10 * (k + 1));
    printf("   %-4s %11s %11s %13s %11s\n",
           "grp", "mu* ours", "mu* [C07]", "S_T closed", "S_T [C07]");
    for (size_t i = 0; i < G; i++) {
        st[i] = gf_group_total_index(V_i, k, V_tot, groups[i].members);
        int corrected = fabs(expected_st[i] - published_st[i]) > 1e-9;
        printf("   %-4s %11.3f %11.3f %13.5f %11.3f%s\n",
               groups[i].name, mustar[i], published_mu[i], st[i], published_st[i],
               corrected ? "  (printed value is an erratum; see D)" : "");
    }

    /* Closed form must reproduce the corrected analytic column. */
    int analytic_ok = 1; size_t bad = 0;
    for (size_t i = 0; i < G; i++)
        if (fabs(st[i] - expected_st[i]) > 0.005) { analytic_ok = 0; bad = i; }

    /*
     * Ranking assertion, tie-aware. Finding C measured that mu* cannot
     * resolve a boundary that falls inside a near-tie in the true index, and
     * no budget fixes it. So only assert the order of pairs that are actually
     * separated -- here, by more than 5% in true S_T. [C07] p.1513 makes the
     * same concession about test case 3: "the estimated ranking is not exact,
     * a similar level of importance among the three groups has been
     * successfully ascertained."
     */
    int order_ok = 1, pairs_tested = 0, pairs_tied = 0;
    for (size_t i = 0; i < G; i++)
        for (size_t j = i + 1; j < G; j++) {
            double hi = st[i] > st[j] ? st[i] : st[j];
            double lo = st[i] > st[j] ? st[j] : st[i];
            if (lo > 0.0 && hi / lo < 1.05) { pairs_tied++; continue; }
            pairs_tested++;
            if ((mustar[i] > mustar[j]) != (st[i] > st[j])) order_ok = 0;
        }

    char buf[200];
    if (analytic_ok) {
        report("closed form reproduces the analytic column", 1,
               "all groups within 0.005 of ground truth");
    } else {
        snprintf(buf, sizeof buf, "group %s: ours %.5f vs expected %.5f",
                 groups[bad].name, st[bad], expected_st[bad]);
        report("closed form reproduces the analytic column", 0, buf);
    }
    snprintf(buf, sizeof buf, "%d separated pair%s ordered correctly, %d tie%s skipped",
             pairs_tested, pairs_tested == 1 ? "" : "s",
             pairs_tied, pairs_tied == 1 ? "" : "s");
    report("group mu* orders every well-separated pair", order_ok, buf);
    printf("\n");
}

static void claim_b(void) {
    printf("B. Group mu*   [C07] Sec.3.3 p.1512, Table 1 p.1512\n");
    printf("   mu* is a stochastic estimate under a different RNG and a\n");
    printf("   different (improved) sampling strategy than [C07] used, so\n");
    printf("   agreement is expected in ORDER, not digit-for-digit.\n\n");

    {   /* Table 1, test case 1 */
        static const double a[9] = {0.02, 0.03, 0.05, 11, 12.5, 13, 34, 35, 37};
        static const group_t g[3] = {
            {"u", {1,1,1,0,0,0,0,0,0}}, {"v", {0,0,0,1,1,1,0,0,0}},
            {"w", {0,0,0,0,0,0,1,1,1}},
        };
        static const double pmu[3] = {7.948, 1.058, 0.708};
        static const double pst[3] = {0.995, 0.010, 0.001};
        one_group_case("test case 1", a, 9, g, 3, pmu, pst, pst);
    }
    {   /* Table 1, test case 2 */
        static const double a[9] = {0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 34, 35, 37};
        static const group_t g[3] = {
            {"u", {1,0,1,0,1,0,0,0,0}}, {"v", {0,1,0,1,0,1,0,0,0}},
            {"w", {0,0,0,0,0,0,1,1,1}},
        };
        static const double pmu[3] = {42.339, 32.656, 2.735};
        static const double pst[3] = {0.694, 0.686, 0.001};
        one_group_case("test case 2", a, 9, g, 3, pmu, pst, pst);
    }
    {   /* Table 1, test case 3 — each group mixes important and unimportant */
        static const double a[9] = {0.02, 0.03, 0.05, 11, 12.5, 13, 34, 35, 37};
        static const group_t g[3] = {
            {"u", {1,0,0,1,0,0,0,1,0}}, {"v", {0,0,1,0,1,0,0,0,1}},
            {"w", {0,1,0,0,0,1,1,0,0}},
        };
        static const double pmu[3] = {8.108, 7.083, 6.364};
        static const double pst[3] = {0.436, 0.393, 0.429};   /* as printed */
        static const double est[3] = {0.436, 0.417, 0.429};   /* v corrected */
        one_group_case("test case 3", a, 9, g, 3, pmu, pst, est);
    }
}

/* ===================================================================== C */

static void claim_c(void) {
    static const double a[12] = {
        0.001, 89.9, 5.54, 42.10, 0.78, 1.26,
        0.04,  0.79, 74.51, 4.32, 82.51, 41.62
    };
    const size_t k = 12;
    double V_i[12], V_tot;
    gf_partial_var(a, k, V_i, &V_tot);
    double st[12];
    for (size_t i = 0; i < k; i++) st[i] = gf_total_index(V_i, k, V_tot, i);

    printf("C. Ranking fidelity vs trajectory budget\n");
    printf("   'top-m ok' = the set of m factors mu* keeps equals the set the\n");
    printf("   analytic S_T keeps. 40 independent seeds per row.\n\n");
    printf("   %5s %7s %11s %10s %10s\n", "r", "runs", "spearman", "top-5 ok", "top-3 ok");

    const size_t rs[] = {5, 10, 20, 50, 100, 200};
    int ok5_at_20 = 0;
    for (size_t ri = 0; ri < sizeof rs / sizeof *rs; ri++) {
        size_t r = rs[ri];
        double rho_sum = 0.0; int ok5 = 0, ok3 = 0;
        const int trials = 40;
        for (int s = 0; s < trials; s++) {
            double mu[12];
            mustar_per_factor(a, k, r, 4, 1000u + (unsigned)s * 7919u, mu);
            rho_sum += spearman(mu, st, k);
            ok5 += topm_agrees(mu, st, k, 5);
            ok3 += topm_agrees(mu, st, k, 3);
        }
        printf("   %5zu %7zu %11.4f %9d%% %9d%%\n", r, r * (k + 1),
               rho_sum / trials, ok5 * 100 / trials, ok3 * 100 / trials);
        if (r == 20) ok5_at_20 = ok5 * 100 / trials;
    }

    /* Why top-3 never converges: the cut lands inside a near-tie. */
    double sorted[12];
    memcpy(sorted, st, sizeof sorted);
    for (size_t i = 0; i < k; i++)
        for (size_t j = i + 1; j < k; j++)
            if (sorted[j] > sorted[i]) { double t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    double gap3 = sorted[2] / sorted[3];
    double gap5 = sorted[4] / sorted[5];
    printf("\n   gap in true S_T at the top-3 cut: %.3fx   at the top-5 cut: %.2fx\n",
           gap3, gap5);
    printf("   The top-3 boundary falls inside a %.1f%% tie, which no budget\n",
           (gap3 - 1.0) * 100.0);
    printf("   can resolve; the top-5 boundary falls in a %.1fx gap.\n\n", gap5);

    char buf[160];
    snprintf(buf, sizeof buf, "%d%% at r=20 (260 runs)", ok5_at_20);
    report("top-5 keep/drop is reliable once the cut sits in a gap", ok5_at_20 >= 95, buf);
    snprintf(buf, sizeof buf, "%.1f%% gap at the cut", (gap3 - 1.0) * 100.0);
    report("top-3 unreliability is a tie, not a sampling failure", gap3 < 1.05, buf);
    printf("\n");
}

/* ===================================================================== D */

static void claim_d(void) {
    /* [C07] Table 1, test case 3 */
    static const double a[9] = {0.02, 0.03, 0.05, 11, 12.5, 13, 34, 35, 37};
    static const int u[9] = {1,0,0,1,0,0,0,1,0};
    static const int v[9] = {0,0,1,0,1,0,0,0,1};
    static const int w[9] = {0,1,0,0,0,1,1,0,0};
    static const double published[3] = {0.436, 0.393, 0.429};
    const int *grp[3] = {u, v, w};
    const char *nm[3] = {"u {X1,X4,X8}", "v {X3,X5,X9}", "w {X2,X6,X7}"};

    double V_i[9], V_tot;
    gf_partial_var(a, 9, V_i, &V_tot);

    printf("D. Erratum check on [C07] Table 1, test case 3\n");
    printf("   Monte Carlo (Jansen [J99], N=2,000,000) is independent of both\n");
    printf("   the closed form and the paper.\n\n");
    printf("   %-14s %12s %12s %12s\n", "group", "closed form", "Monte Carlo", "[C07]");

    double mc_v = 0.0, cf_v = 0.0;
    for (int i = 0; i < 3; i++) {
        double exact = gf_group_total_index(V_i, 9, V_tot, grp[i]);
        double mc = mc_group_st(a, 9, grp[i], 2000000, 20260806u + (unsigned)i);
        printf("   %-14s %12.5f %12.5f %12.3f%s\n", nm[i], exact, mc, published[i],
               fabs(mc - published[i]) > 0.01 ? "  <-- disagrees" : "");
        if (i == 1) { mc_v = mc; cf_v = exact; }
    }

    /* What is 0.393, if not S_T(v)? Search every 3-subset for a match, and
     * check the first-order group index, to give the authors something
     * specific rather than "your number looks wrong". */
    printf("\n   What could the printed 0.393 be?\n");
    double s_first = gf_group_first_index(V_i, 9, V_tot, v);
    printf("     first-order S(v) for the same group : %.5f\n", s_first);

    int found = 0;
    for (int i = 0; i < 9 && found < 4; i++)
      for (int j = i+1; j < 9 && found < 4; j++)
        for (int m = j+1; m < 9 && found < 4; m++) {
            int mask[9] = {0}; mask[i]=mask[j]=mask[m]=1;
            double s = gf_group_total_index(V_i, 9, V_tot, mask);
            if (fabs(s - 0.393) < 0.002) {
                printf("     S_T of {X%d,X%d,X%d}%*s: %.5f  <-- matches 0.393\n",
                       i+1, j+1, m+1, 16, "", s);
                found++;
            }
        }
    if (!found) printf("     no 3-subset of this a-vector has S_T = 0.393\n");

    printf("\n");
    char buf[200];
    snprintf(buf, sizeof buf,
             "closed form %.5f and Monte Carlo %.5f agree; [C07] prints 0.393",
             cf_v, mc_v);
    report("two independent routes agree against the printed value",
          fabs(cf_v - mc_v) < 0.002 && fabs(cf_v - 0.393) > 0.01, buf);
    printf("\n");
}

/* ===================================================================== E
 *
 * The `sobol` tool's estimators against the g-function closed form.
 *
 * Source: Saltelli, A., Annoni, P., Azzini, I., Campolongo, F., Ratto, M.,
 *   Tarantola, S. (2010). "Variance based sensitivity analysis of model
 *   output. Design and estimator for the total sensitivity index."
 *   Computer Physics Communications 181(2), 259-270.
 *   Local copy: sources/pdf/saltelli-2010-total-index-estimator.pdf
 *
 * What sobol.c implements, checked against that paper:
 *
 *   S_i   = 1/N sum_j f(B)_j ( f(A_B^(i))_j - f(A)_j )  / V
 *           = Table 2 formula (b), p.262. Sec. 5.1 p.263 explicitly
 *           recommends this one: "we would advice practitioners to use
 *           formula (b) in Table 2 for S_i".
 *
 *   S_Ti  = 1/(2N) sum_j ( f(A)_j - f(A_B^(i))_j )^2    / V
 *           = Table 2 formula (f), p.262, attributed to Jansen 1999.
 *           The Table 2 caption calls it "the best practice so far for
 *           S_Ti", and Sec. 7 conclusion 1 names Jansen's estimator as the
 *           best choice.
 *
 *   A_B^(i) = matrix A with column i taken from B (Table 1, p.260), used in
 *           the A, B, A_B^(i) triplet that Sec. 3 p.261 recommends over the
 *           older B, A, B_A^(i) formulation, at cost N(k+2).
 *
 * The g-function's closed form is the paper's own Appendix A.1 (p.268), the
 * same one gfunction.c implements.
 */

static void claim_e(void) {
    /* 6-factor g-function spanning important (a=0) to inert (a=99). */
    static const double a[6] = {0.0, 0.5, 3.0, 9.0, 99.0, 99.0};
    const size_t k = 6, N = 65536;

    printf("E. sobol estimators vs closed form   [Saltelli 2010] Table 2 (b),(f)\n");
    printf("   g-function, k=6, N=%zu => %zu model runs.\n\n", N, N * (k + 2));

    char spec[2048];
    int off = snprintf(spec, sizeof spec, "factors:\n");
    for (size_t i = 0; i < k; i++)
        off += snprintf(spec + off, sizeof spec - (size_t)off,
                        "  x%zu: 0.0, 1.0\n", i + 1);
    snprintf(spec + off, sizeof spec - (size_t)off,
             "seed: 2026\nsamples: %zu\n", N);

    doe_space_t sp; char err[DOE_ERR_SIZE];
    if (doe_space_parse(spec, &sp, err) != 0) { printf("  parse: %s\n", err); failures++; return; }

    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { printf("  design: %s\n", err); failures++; return; }

    double *y = malloc(d.npoints * sizeof *y);
    double u[MAXK];
    for (size_t i = 0; i < d.npoints; i++) {
        sobol_point(&d, i, u);
        y[i] = gf_eval(u, a, k);
    }

    sobol_index_t *idx = NULL; size_t n = 0;
    if (sobol_analyze(&sp, y, d.npoints, &idx, &n, err) != 0) {
        printf("  analyze: %s\n", err); failures++; free(y); sobol_design_free(&d); return;
    }

    double V_i[6], V_tot;
    gf_partial_var(a, k, V_i, &V_tot);

    printf("   %-5s %8s %10s %10s %9s %10s %10s %9s\n",
           "fac", "a_i", "S_i est", "S_i exact", "err", "S_T est", "S_T exact", "err");
    double worst_s = 0.0, worst_t = 0.0;
    for (size_t i = 0; i < k; i++) {
        double s_ex = gf_first_index(V_i, k, V_tot, i);
        double t_ex = gf_total_index(V_i, k, V_tot, i);
        double es = fabs(idx[i].s1 - s_ex), et = fabs(idx[i].st - t_ex);
        if (es > worst_s) worst_s = es;
        if (et > worst_t) worst_t = et;
        printf("   %-5s %8.1f %10.4f %10.4f %9.4f %10.4f %10.4f %9.4f\n",
               idx[i].name, a[i], idx[i].s1, s_ex, es, idx[i].st, t_ex, et);
    }

    /* Sum(S_i) <= 1 <= Sum(S_T) is the structural check on any correct
     * variance decomposition (Eq. 11, p.260). */
    double sum_s = 0.0, sum_t = 0.0;
    for (size_t i = 0; i < k; i++) { sum_s += idx[i].s1; sum_t += idx[i].st; }
    printf("\n   sum S_i = %.4f (<= 1)      sum S_T = %.4f (>= 1)\n", sum_s, sum_t);
    printf("   worst |error|: S_i %.4f, S_T %.4f\n\n", worst_s, worst_t);

    char buf[160];
    snprintf(buf, sizeof buf, "worst error %.4f over %zu factors", worst_s, k);
    report("S_i matches Table 2 formula (b) closed form", worst_s < 0.02, buf);
    snprintf(buf, sizeof buf, "worst error %.4f over %zu factors", worst_t, k);
    report("S_T matches Table 2 formula (f) closed form", worst_t < 0.02, buf);
    snprintf(buf, sizeof buf, "sum S_i = %.4f, sum S_T = %.4f", sum_s, sum_t);
    report("variance decomposition is structurally sound",
           sum_s <= 1.02 && sum_t >= 0.98, buf);

    free(y); free(idx); sobol_design_free(&d);
    printf("\n");
}

int main(void) {
    printf("=======================================================================\n");
    printf(" validation — published screening results vs closed-form ground truth\n");
    printf(" sources/README.md indexes every paper cited here.\n");
    printf("=======================================================================\n\n");
    claim_a();
    claim_b();
    claim_c();
    claim_d();
    claim_e();
    printf("=======================================================================\n");
    printf(" %s (%d failure%s)\n", failures ? "FAILURES" : "all checks passed",
           failures, failures == 1 ? "" : "s");
    printf("=======================================================================\n");
    return failures ? 1 : 0;
}
