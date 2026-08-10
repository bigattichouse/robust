/*
 * sobol CLI — sample | generate | run | analyze | validate
 */

#include "sobol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> <file.space> [ARGS]\n"
        "\n"
        "Commands:\n"
        "  sample   <file.space>                 Print the Saltelli design as CSV\n"
        "  generate <file.space>                 Show the design structure\n"
        "  run      <file.space> <script>        Run <script> once per point (SOBOL_* env)\n"
        "  analyze  <file.space> <results.csv> [--metric NAME] [--json]\n"
        "                                        First/total indices Si, STi (+ CIs);\n"
        "                                        --json for machines (stable contract)\n"
        "  converge <file.space> <script> --target-ci W [--max-samples N]\n"
        "                                        Double samples until every S1/ST 95%% CI\n"
        "                                        is narrower than W (or the cap)\n"
        "  validate <file.space>                 Check the .space definition\n"
        "  --version                             Show version\n",
        prog);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("open"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/*
 * Which sampler produced a design is not recoverable from the numbers, so say
 * it. `sampling:` changes the design and therefore the indices; a run whose
 * output does not record it cannot be reproduced from the results alone.
 */
static const char *sampler_name(const doe_space_t *sp) {
    return sp->sampling == DOE_SAMPLING_SOBOL
         ? "Joe-Kuo quasi-random (sampling: sobol)"
         : "Latin Hypercube (sampling: lhs)";
}

/*
 * A quasi-random sequence is uniform over ALIGNED BLOCKS of 2^m points
 * (Saltelli et al. 2010 Sec. 5.1 consideration 1), so an N that is not a power
 * of two lands mid-block and gives up part of the guarantee. That is not a
 * theoretical worry: `make validate` check G measures N=20000 producing 4.5x
 * the error of N=16384 while costing 22% more runs. A note, not a warning --
 * the design is still valid and still beats LHS.
 */
static void note_sample_count(const doe_space_t *sp) {
    if (sp->sampling != DOE_SAMPLING_SOBOL) return;
    size_t n = sp->samples;
    if (n == 0 || (n & (n - 1)) == 0) return;      /* already a power of two */
    size_t below = 1;
    while (below * 2 < n) below *= 2;
    fprintf(stderr,
        "\nNote: samples = %zu is not a power of two. A quasi-random sequence is\n"
        "  uniform over aligned blocks of 2^m points, so %zu can be both cheaper\n"
        "  and more accurate than %zu. See `make validate` check G.\n\n",
        n, below, n);
}

static int load_space(const char *path, doe_space_t *space) {
    char *content = read_file(path);
    if (!content) return -1;
    char err[DOE_ERR_SIZE];
    int rc = doe_space_parse(content, space, err);
    free(content);
    if (rc != 0) { fprintf(stderr, "Error parsing %s: %s\n", path, err); return -1; }
    return 0;
}

/* ---- run callback (caches the current row's u-vector) ---- */
typedef struct {
    const doe_space_t   *space;
    const sobol_design_t *d;
    long   last_row;
    double u[DOE_MAX_FACTORS];
    char   buf[DOE_MAX_VALUE];
} run_ctx_t;

static const char *run_value(void *vctx, size_t row, size_t col) {
    run_ctx_t *c = (run_ctx_t *)vctx;
    if ((long)row != c->last_row) {
        sobol_point(c->d, row, c->u);
        c->last_row = (long)row;
    }
    return doe_factor_value(c->space, col, c->u[col], c->buf, sizeof c->buf);
}

static int cmd_sample(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    printf("run_id");
    for (size_t c = 0; c < sp.factor_count; c++) printf(",%s", sp.factors[c].name);
    printf("\n");

    double u[DOE_MAX_FACTORS];
    char buf[DOE_MAX_VALUE];
    for (size_t i = 0; i < d.npoints; i++) {
        sobol_point(&d, i, u);
        printf("%zu", i + 1);
        for (size_t c = 0; c < sp.factor_count; c++) {
            printf(",%s", doe_factor_value(&sp, c, u[c], buf, sizeof buf));
        }
        printf("\n");
    }
    sobol_design_free(&d);
    return 0;
}

static int cmd_generate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    printf("Saltelli design: N=%zu base samples, k=%zu factors -> %zu runs\n",
           d.n, d.k, d.npoints);
    printf("Sampler: %s\n", sampler_name(&sp));
    note_sample_count(&sp);
    printf("  rows [%zu,%zu)   block A\n", (size_t)0, d.n);
    printf("  rows [%zu,%zu)   block B\n", d.n, 2 * d.n);
    for (size_t i = 0; i < d.k; i++) {
        printf("  rows [%zu,%zu)   A_B(%s)\n",
               (2 + i) * d.n, (3 + i) * d.n, sp.factors[i].name);
    }
    printf("Use 'sample' for the full design matrix.\n");
    sobol_design_free(&d);
    return 0;
}

static int cmd_run(const char *path, const char *script) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    run_ctx_t ctx = { .space = &sp, .d = &d, .last_row = -1 };
    printf("Running %zu points with '%s'...\n", d.npoints, script);
    int rc = doe_run(&sp, "SOBOL", script, d.npoints, run_value, &ctx, err);
    if (rc != 0) fprintf(stderr, "Error: %s\n", err);
    sobol_design_free(&d);
    return rc == 0 ? 0 : 1;
}

/*
 * The machine-readable contract. See the note in screen/morris/src/cli/main.c:
 * `analyze` grew a CI column rendered glued to its value ("0.812[0.79,0.83]")
 * and that broke every consumer splitting the row on whitespace, silently.
 * The tables here are a display; these keys are the interface. Additions are
 * free, renames and removals need a schema bump.
 */
#define SOBOL_JSON_SCHEMA 1
#define JSTR(s) doe_json_string((s), (char[DOE_JSON_STR(DOE_MAX_NAME)]){0}, \
                                DOE_JSON_STR(DOE_MAX_NAME))
#define JNUM(v) doe_json_number((v), (char[DOE_JSON_NUM]){0}, DOE_JSON_NUM)

/* argv-supplied, so unbounded and user-chosen: it gets the allocating escape
 * rather than the DOE_MAX_NAME-sized one. */
static void print_metric_field(const char *metric) {
    char *m = doe_json_escape(metric);
    printf("  \"metric\": \"%s\",\n", m ? m : "");
    doe_free(m);
}

static void print_indices_json(const doe_space_t *sp, const char *metric, size_t np,
                               const sobol_index_t *idx, size_t count, double sum_s1,
                               const sobol_pair_t *pairs, size_t npairs) {
    printf("{\n");
    printf("  \"tool\": \"sobol\",\n");
    printf("  \"command\": \"analyze\",\n");
    printf("  \"schema\": %d,\n", SOBOL_JSON_SCHEMA);
    print_metric_field(metric);
    printf("  \"sampler\": \"%s\",\n",
           sp->sampling == DOE_SAMPLING_SOBOL ? "sobol" : "lhs");
    printf("  \"samples\": %zu,\n", sp->samples);
    printf("  \"runs\": %zu,\n", np);
    printf("  \"factor_count\": %zu,\n", count);
    printf("  \"sum_first_order\": %s,\n", JNUM(sum_s1));
    printf("  \"additive\": %s,\n", sum_s1 > 0.9 ? "true" : "false");
    printf("  \"indices\": [\n");
    for (size_t i = 0; i < count; i++) {
        printf("    {\"factor\": %s, \"s1\": %s, \"s1_lo\": %s, \"s1_hi\": %s, "
               "\"st\": %s, \"st_lo\": %s, \"st_hi\": %s, \"interaction\": %s}%s\n",
               JSTR(idx[i].name),
               JNUM(idx[i].s1), JNUM(idx[i].s1_lo), JNUM(idx[i].s1_hi),
               JNUM(idx[i].st), JNUM(idx[i].st_lo), JNUM(idx[i].st_hi),
               JNUM(idx[i].st - idx[i].s1),
               i + 1 < count ? "," : "");
    }
    printf("  ]");

    if (!pairs) {
        printf(",\n  \"second_order\": null\n");
    } else {
        /* Every pair, not the top ten the table stops at: truncating a machine
         * format leaves the consumer unable to tell a short list from a
         * complete one. */
        printf(",\n  \"second_order\": [\n");
        for (size_t i = 0; i < npairs; i++) {
            printf("    {\"a\": %s, \"b\": %s, \"s2\": %s, \"closed\": %s}%s\n",
                   JSTR(pairs[i].a), JSTR(pairs[i].b),
                   JNUM(pairs[i].s2), JNUM(pairs[i].closed),
                   i + 1 < npairs ? "," : "");
        }
        printf("  ]\n");
    }
    printf("}\n");
}

static int cmd_analyze(const char *path, const char *csv, const char *metric,
                       int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    size_t np = sobol_npoints(&sp);
    double *responses = malloc(np * sizeof *responses);
    if (!responses) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    for (size_t i = 0; i < np; i++) responses[i] = 0.0 / 0.0;   /* NaN = missing */

    char err[DOE_ERR_SIZE];
    size_t got = 0;
    if (doe_csv_read_metric(csv, metric, responses, np, &got, err) != 0) {
        fprintf(stderr, "Error reading results: %s\n", err);
        free(responses);
        return 1;
    }

    sobol_index_t *idx = NULL;
    size_t count = 0;
    if (sobol_analyze(&sp, responses, np, &idx, &count, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(responses);
        return 1;
    }

    double sum_s1 = 0.0;
    for (size_t i = 0; i < count; i++) sum_s1 += idx[i].s1;

    /* Computed before either renderer runs, so --json can carry the pairs
     * rather than only the table showing them. */
    sobol_pair_t *pairs = NULL;
    size_t np2 = 0;
    if (sp.second_order) {
        if (sobol_analyze_pairs(&sp, responses, np, &pairs, &np2, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            pairs = NULL; np2 = 0;
        } else {
            /* Rank by interaction magnitude: the pairs worth resolving first. */
            for (size_t i = 0; i < np2; i++)
                for (size_t j = i + 1; j < np2; j++)
                    if (pairs[j].s2 > pairs[i].s2) {
                        sobol_pair_t t = pairs[i]; pairs[i] = pairs[j]; pairs[j] = t;
                    }
        }
    }
    free(responses);

    if (as_json) {
        print_indices_json(&sp, metric, np, idx, count, sum_s1, pairs, np2);
        free(pairs);
        free(idx);
        return 0;
    }

    printf("Sobol indices (metric: %s) — N=%zu, %zu runs\n", metric, sp.samples, np);
    printf("Sampler: %s\n\n", sampler_name(&sp));
    /*
     * Each index and its interval are separate columns, and each interval is a
     * single space-free token. Printed glued together ("0.812[0.79,0.83]") the
     * value will not parse as a number for anything splitting the row on
     * whitespace; printed as "[0.79, 0.83]" the interval would split in two and
     * shift every column after it. See the note in morris's analyze.
     */
    printf("%-18s %10s %14s %10s %14s   %s\n",
           "Factor", "S1", "[95% CI]", "ST", "[95% CI]", "interaction");
    printf("%-18s %10s %14s %10s %14s   %s\n",
           "------", "--", "--------", "--", "--------", "-----------");
    for (size_t i = 0; i < count; i++) {
        char s1c[40], stc[40];
        snprintf(s1c, sizeof s1c, "[%.2f,%.2f]", idx[i].s1_lo, idx[i].s1_hi);
        snprintf(stc, sizeof stc, "[%.2f,%.2f]", idx[i].st_lo, idx[i].st_hi);
        printf("%-18s %10.3f %14s %10.3f %14s   %.3f\n",
               idx[i].name, idx[i].s1, s1c, idx[i].st, stc, idx[i].st - idx[i].s1);
    }
    printf("\nSum of first-order Si = %.3f", sum_s1);
    if (sum_s1 > 0.9) printf("  (~1 => additive; OA/Taguchi ranking trustworthy)\n");
    else              printf("  (<1 => interactions present)\n");
    printf("ST ~ 0 => freeze the factor; ST - S1 large => acts through interactions.\n");

    if (pairs) {
        printf("\nSecond-order interactions (%zu pairs)\n\n", np2);
        printf("%-30s %10s %10s\n", "pair", "S2", "closed");
        printf("%-30s %10s %10s\n", "----", "--", "------");
        for (size_t i = 0; i < np2 && i < 10; i++) {
            char lbl[2 * DOE_MAX_NAME + 8];
            snprintf(lbl, sizeof lbl, "%s x %s", pairs[i].a, pairs[i].b);
            printf("%-30s %10.4f %10.4f\n", lbl, pairs[i].s2, pairs[i].closed);
        }
        if (np2 > 10) printf("... %zu more, smaller (--json carries them all)\n", np2 - 10);
        printf("\nS2 is the interaction ALONE: what the pair explains beyond each\n"
               "factor acting separately. It answers the question ST - S1 can only\n"
               "raise -- which two. Resolve the top pair exactly with:\n"
               "  grid <space> <script> --factors %s,%s\n",
               np2 ? pairs[0].a : "A", np2 ? pairs[0].b : "B");
    }
    free(pairs);
    free(idx);
    return 0;
}

/* ---- converge: spend runs until the indices resolve (E2) ---- */

/*
 * `samples:` is the guess sobol asks a .space author to make, and getting it
 * wrong is quiet: a variance share with an interval spanning half the range
 * still prints as a number. This doubles N until every S1 and ST interval is
 * narrower than the target.
 *
 * Doubling suits this design specifically. A quasi-random sequence is uniform
 * over ALIGNED blocks of 2^m points, so starting from a power of two and
 * doubling stays aligned -- the property note_sample_count() warns about
 * losing is preserved for free.
 */
static int cmd_converge(const char *path, const char *script,
                        double target, size_t max_n, int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    size_t n = sp.samples > 0 ? sp.samples : 64;
    size_t cap = max_n ? max_n : DOE_MAX_SAMPLES;
    if (cap > DOE_MAX_SAMPLES) cap = DOE_MAX_SAMPLES;

    size_t total_evals = 0, rounds = 0;
    int converged = 0;
    double widest = 0.0;
    char err[DOE_ERR_SIZE];

    FILE *out = as_json ? stderr : stdout;
    fprintf(out, "Converging on a %.6g-wide 95%% CI (cap N=%zu)\n", target, cap);
    fprintf(out, "Sampler: %s\n\n", sampler_name(&sp));
    fprintf(out, "%12s %12s %14s\n", "N", "runs", "widest CI");

    if (as_json) {
        printf("{\n  \"tool\": \"sobol\",\n  \"command\": \"converge\",\n");
        printf("  \"schema\": %d,\n", SOBOL_JSON_SCHEMA);
        printf("  \"sampler\": \"%s\",\n",
               sp.sampling == DOE_SAMPLING_SOBOL ? "sobol" : "lhs");
        printf("  \"target_ci\": %s,\n", JNUM(target));
        printf("  \"rounds\": [\n");
    }

    for (;;) {
        doe_space_t at = sp;
        at.samples = n;

        sobol_design_t d;
        if (sobol_design_build(&at, &d, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            return 1;
        }

        double *y = malloc(d.npoints * sizeof *y);
        if (!y) { fprintf(stderr, "Error: out of memory\n"); sobol_design_free(&d); return 1; }

        run_ctx_t ctx = { .space = &at, .d = &d, .last_row = -1 };
        if (doe_run_capture(&at, "SOBOL", script, d.npoints, run_value, &ctx, y, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            free(y); sobol_design_free(&d); return 1;
        }
        total_evals += d.npoints;
        rounds++;

        sobol_index_t *idx = NULL;
        size_t count = 0;
        if (sobol_analyze(&at, y, d.npoints, &idx, &count, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            free(y); sobol_design_free(&d); return 1;
        }

        /* Both indices: ST can stay wide while S1 has settled, and a total
         * index you cannot bound is exactly the one you must not act on. */
        widest = 0.0;
        for (size_t i = 0; i < count; i++) {
            double w1 = idx[i].s1_hi - idx[i].s1_lo;
            double wt = idx[i].st_hi - idx[i].st_lo;
            if (w1 > widest) widest = w1;
            if (wt > widest) widest = wt;
        }

        fprintf(out, "%12zu %12zu %14.6g\n", n, d.npoints, widest);
        if (as_json)
            printf("    {\"samples\": %zu, \"runs\": %zu, \"widest_ci\": %s}%s\n",
                   n, d.npoints, JNUM(widest),
                   (widest <= target || n * 2 > cap) ? "" : ",");

        free(idx); free(y); sobol_design_free(&d);

        if (widest <= target) { converged = 1; break; }
        if (n * 2 > cap) break;
        n *= 2;
    }

    if (as_json) {
        printf("  ],\n");
        printf("  \"converged\": %s,\n", converged ? "true" : "false");
        printf("  \"samples\": %zu,\n", n);
        printf("  \"widest_ci\": %s,\n", JNUM(widest));
        printf("  \"evaluations\": %zu,\n", total_evals);
        printf("  \"rounds_run\": %zu,\n", rounds);
        printf("  \"cap\": %zu\n}\n", cap);
    }

    if (converged) {
        fprintf(out, "\nConverged at N=%zu, %zu evaluations total.\n"
                     "Write `samples: %zu` into the .space.\n", n, total_evals, n);
        if (sp.sampling != DOE_SAMPLING_SOBOL)
            fprintf(out,
                "\nNote: sampling is LHS, whose draw depends on the RNG stream and not\n"
                "on N alone, so re-running at this N reproduces the design only because\n"
                "the seed is fixed in the .space. Keep the seed with the number.\n");
        return 0;
    }

    fprintf(stderr,
        "\nDid NOT converge: the widest CI is still %.6g at N=%zu, after %zu\n"
        "evaluations. Raise --max-samples, accept a wider target, or take it as\n"
        "the finding -- variance shares this poorly determined should not be\n"
        "ranked, let alone acted on.\n", widest, cap, total_evals);
    return 1;
}

static int cmd_validate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Invalid: %s\n", err); return 1; }
    printf("Valid: %zu factors, %zu runs (N=%zu base samples)\n",
           sp.factor_count, d.npoints, sp.samples);
    printf("Sampler: %s\n", sampler_name(&sp));
    note_sample_count(&sp);
    sobol_design_free(&d);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("sobol (robust toolkit) %d.%d.%d\n",
               DOE_VERSION_MAJOR, DOE_VERSION_MINOR, DOE_VERSION_PATCH);
        return 0;
    }
    if (argc < 3) { usage(argv[0]); return 1; }

    const char *cmd = argv[1];
    const char *path = argv[2];

    if (strcmp(cmd, "sample") == 0)   return cmd_sample(path);
    if (strcmp(cmd, "generate") == 0) return cmd_generate(path);
    if (strcmp(cmd, "validate") == 0) return cmd_validate(path);
    if (strcmp(cmd, "run") == 0) {
        if (argc < 4) { fprintf(stderr, "run needs a script argument\n"); return 1; }
        return cmd_run(path, argv[3]);
    }
    if (strcmp(cmd, "converge") == 0) {
        if (argc < 4) { fprintf(stderr, "converge needs a script argument\n"); return 1; }
        double target = 0.0;
        size_t max_n = 0;
        int as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--target-ci") == 0 && i + 1 < argc) {
                target = strtod(argv[++i], NULL);
                if (!(target > 0.0)) { fprintf(stderr, "--target-ci must be > 0\n"); return 1; }
            } else if (strcmp(argv[i], "--max-samples") == 0 && i + 1 < argc) {
                max_n = (size_t)strtoul(argv[++i], NULL, 10);
                if (max_n < 1) { fprintf(stderr, "--max-samples must be >= 1\n"); return 1; }
            } else if (strcmp(argv[i], "--json") == 0) as_json = 1;
            else {
                fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
                usage(argv[0]); return 1;
            }
        }
        if (!(target > 0.0)) {
            fprintf(stderr, "converge needs --target-ci W (the CI width to reach)\n");
            return 1;
        }
        return cmd_converge(path, argv[3], target, max_n, as_json);
    }
    if (strcmp(cmd, "analyze") == 0) {
        if (argc < 4) { fprintf(stderr, "analyze needs a results.csv argument\n"); return 1; }
        const char *metric = "response";
        int as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric = argv[++i];
            else if (strcmp(argv[i], "--json") == 0) as_json = 1;
            else {
                /* Rejected, not ignored: a caller asking for an option this
                 * build does not have must not get the human table and exit 0. */
                fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
        return cmd_analyze(path, argv[3], metric, as_json);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
