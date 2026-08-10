/*
 * morris CLI — sample | generate | run | analyze | validate
 *
 * Thin wrapper over libdoe + the morris library, mirroring the taguchi CLI.
 */

#include "morris.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> <file.space> [ARGS]\n"
        "\n"
        "Commands:\n"
        "  sample   <file.space>                 Print the design matrix as CSV\n"
        "  generate <file.space>                 List the design points (human-readable)\n"
        "  run      <file.space> <script>        Run <script> once per point (MORRIS_* env)\n"
        "  analyze  <file.space> <results.csv> [--metric NAME] [--keep-share S] [--json]\n"
        "                                        Elementary effects: mu* with 95%% CI,\n"
        "                                        sigma; --keep-share cuts at cumulative\n"
        "                                        mu*-share S (an 80/20 rule);\n"
        "                                        --json for machines (stable contract)\n"
        "\n"
        "A `groups:` section in the .space switches every command to group\n"
        "screening: r*(G+1) runs instead of r*(k+1), ranked by the absolute\n"
        "group effect. The file decides, so the design and the analysis cannot\n"
        "disagree.\n"
        "  converge  <file.space> <script> --target-ci W [--max-trajectories N]\n"
        "                                        Double trajectories until every 95%% CI\n"
        "                                        on mu* is narrower than W (or cap)\n"
        "  bifurcate <file.space> <script> [--keep-share S] [--json]\n"
        "                                        Screen groups, drop, split, repeat\n"
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

static int load_space(const char *path, doe_space_t *space) {
    char *content = read_file(path);
    if (!content) return -1;
    char err[DOE_ERR_SIZE];
    int rc = doe_space_parse(content, space, err);
    free(content);
    if (rc != 0) { fprintf(stderr, "Error parsing %s: %s\n", path, err); return -1; }
    return 0;
}

/* ---- run callback ---- */
struct design_view;
typedef struct {
    const doe_space_t         *space;
    const struct design_view  *view;
    char buf[DOE_MAX_VALUE];
} run_ctx_t;

/*
 * A view over whichever design the .space declares. Group screening is chosen
 * by the presence of a `groups:` section, NOT by a command-line flag: the
 * design and the analysis have to agree, and a flag lets a user generate one
 * and analyse the other, which would silently produce a wrong answer rather
 * than an error. The file decides, so every subcommand agrees automatically.
 */
typedef struct design_view {
    const double *u;
    size_t npoints, k;
    morris_design_t       per;
    morris_group_design_t grp;
    int is_group;
} design_view_t;

static int build_view(const doe_space_t *sp, design_view_t *v, char *err) {
    memset(v, 0, sizeof *v);
    if (sp->group_count > 0) {
        if (morris_group_design_build(sp, &v->grp, err) != 0) return -1;
        v->is_group = 1;
        v->u = v->grp.u; v->npoints = v->grp.npoints; v->k = v->grp.k;
    } else {
        if (morris_design_build(sp, &v->per, err) != 0) return -1;
        v->u = v->per.u; v->npoints = v->per.npoints; v->k = v->per.k;
    }
    return 0;
}

static void free_view(design_view_t *v) {
    if (v->is_group) morris_group_design_free(&v->grp);
    else             morris_design_free(&v->per);
}

static const char *run_value(void *vctx, size_t row, size_t col) {
    run_ctx_t *c = (run_ctx_t *)vctx;
    double u = c->view->u[row * c->view->k + col];
    return doe_factor_value(c->space, col, u, c->buf, sizeof c->buf);
}

/* ---- commands ---- */

static int cmd_sample(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    design_view_t d;
    if (build_view(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    printf("run_id");
    for (size_t c = 0; c < sp.factor_count; c++) printf(",%s", sp.factors[c].name);
    printf("\n");

    char buf[DOE_MAX_VALUE];
    for (size_t i = 0; i < d.npoints; i++) {
        printf("%zu", i + 1);
        for (size_t c = 0; c < sp.factor_count; c++) {
            printf(",%s", doe_factor_value(&sp, c, d.u[i * d.k + c], buf, sizeof buf));
        }
        printf("\n");
    }
    free_view(&d);
    return 0;
}

static int cmd_generate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    design_view_t d;
    if (build_view(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    if (d.is_group)
        printf("%zu points (%zu trajectories x (%zu groups + 1)) over %zu factors:\n",
               d.npoints, d.grp.r, d.grp.G, d.k);
    else
        printf("%zu points (%zu trajectories x (%zu factors + 1)):\n",
               d.npoints, d.per.r, d.k);
    char buf[DOE_MAX_VALUE];
    for (size_t i = 0; i < d.npoints; i++) {
        printf("Point %zu: ", i + 1);
        for (size_t c = 0; c < sp.factor_count; c++) {
            if (c) printf(", ");
            printf("%s=%s", sp.factors[c].name,
                   doe_factor_value(&sp, c, d.u[i * d.k + c], buf, sizeof buf));
        }
        printf("\n");
    }
    free_view(&d);
    return 0;
}

static int cmd_run(const char *path, const char *script) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    design_view_t d;
    if (build_view(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    run_ctx_t ctx = { .space = &sp, .view = &d };
    printf("Running %zu points with '%s'...\n", d.npoints, script);
    int rc = doe_run(&sp, "MORRIS", script, d.npoints, run_value, &ctx, err);
    if (rc != 0) fprintf(stderr, "Error: %s\n", err);
    free_view(&d);
    return rc == 0 ? 0 : 1;
}

static int cmp_mu_star(const void *a, const void *b) {
    const morris_effect_t *x = a, *y = b;
    if (x->mu_star < y->mu_star) return 1;
    if (x->mu_star > y->mu_star) return -1;
    return 0;
}

/*
 * The machine-readable contract.
 *
 * The human tables above are a display, and displays change: the 95% CI was
 * added to `analyze` as a column improvement and, because it was rendered
 * glued to mu* ("215.6[210,221]"), it silently broke every consumer that split
 * the row on whitespace. Those consumers got an empty ranking rather than an
 * error, so a screening stage degraded into a no-op AFTER paying for r*(k+1)
 * real evaluations. Nothing warned, because from inside the repo it read as a
 * formatting tweak.
 *
 * --json is the fix that keeps the fix: it separates what a person reads from
 * what a program parses, so the tables can keep evolving. Treat the key names
 * below as the interface. Add fields freely; renaming or removing one is a
 * breaking change and needs a version bump.
 *
 * stderr diagnostics are deliberately identical in both modes. A warning that
 * only a human sees is how this class of bug happens in the first place.
 *
 * The schema number is bumped when a key is renamed or removed, never for an
 * addition, so a consumer can refuse a document it does not understand instead
 * of parsing it wrongly -- exactly the signal the glued-CI change lacked.
 */
#define MORRIS_JSON_SCHEMA 1

#define JSTR(s) doe_json_string((s), (char[DOE_JSON_STR(DOE_MAX_NAME)]){0}, \
                                DOE_JSON_STR(DOE_MAX_NAME))
#define JNUM(v) doe_json_number((v), (char[DOE_JSON_NUM]){0}, DOE_JSON_NUM)

/*
 * The metric name comes from argv, so unlike a factor name it is not bounded
 * by DOE_MAX_NAME and gets the allocating escape. A silently truncated metric
 * would mislabel the entire document -- and it is a string a user chooses, so
 * it is the one field here that can carry a quote or a backslash.
 */
static void print_metric_field(const char *metric) {
    char *m = doe_json_escape(metric);
    printf("  \"metric\": \"%s\",\n", m ? m : "");
    doe_free(m);
}

static void print_groups_json(const doe_space_t *sp, const char *metric, size_t np,
                              const morris_group_effect_t *eff, size_t count) {
    printf("{\n");
    printf("  \"tool\": \"morris\",\n");
    printf("  \"command\": \"analyze\",\n");
    printf("  \"schema\": %d,\n", MORRIS_JSON_SCHEMA);
    printf("  \"mode\": \"group\",\n");
    print_metric_field(metric);
    printf("  \"trajectories\": %zu,\n", sp->trajectories);
    printf("  \"runs\": %zu,\n", np);
    printf("  \"per_factor_runs\": %zu,\n", sp->trajectories * (sp->factor_count + 1));
    printf("  \"factor_count\": %zu,\n", sp->factor_count);
    printf("  \"group_count\": %zu,\n", sp->group_count);
    printf("  \"groups\": [\n");
    for (size_t i = 0; i < count; i++) {
        printf("    {\"group\": %s, \"mu_star\": %s, \"sigma\": %s, \"members\": %zu}%s\n",
               JSTR(eff[i].name), JNUM(eff[i].mu_star), JNUM(eff[i].sigma),
               eff[i].member_count, i + 1 < count ? "," : "");
    }
    printf("  ],\n");

    /*
     * Group analyze is not given a cut, so the gap it can report is the one at
     * the bottom of the ranking -- the same pair the human warning describes.
     * Reported under the same keys as the per-factor path (spec/morris-groups.bp
     * requires both paths to carry them) with `cut_at` saying where it was taken.
     */
    double gap = -1.0;
    if (count >= 2 && eff[count - 1].mu_star > 0.0)
        gap = eff[count - 2].mu_star / eff[count - 1].mu_star;
    printf("  \"cut_at\": \"bottom\",\n");
    printf("  \"gap_at_cut\": %s,\n", gap >= 0.0 ? JNUM(gap) : "null");
    printf("  \"cut_is_tie\": %s\n", (gap >= 0.0 && gap < 1.05) ? "true" : "false");
    printf("}\n");
}

/* ---- converge: spend runs until the intervals are narrow enough ---- */

/*
 * E2. `trajectories:` is a guess, and the guess is the one number a .space
 * author has no basis for. This spends runs until the answer is resolved
 * instead: double r, re-screen, stop when every bootstrap CI on mu* is
 * narrower than the target.
 *
 * Deterministic. The design is a pure function of (factors, r, p, seed), so
 * the converged r written back into the .space reproduces the run exactly --
 * which is the point of converging rather than eyeballing.
 */
typedef struct {
    const char *script;
    const doe_space_t *sp;
    const double *u;
    size_t k;
    char buf[DOE_MAX_VALUE];
} conv_ctx_t;

static const char *conv_value(void *vctx, size_t row, size_t col) {
    conv_ctx_t *c = (conv_ctx_t *)vctx;
    return doe_factor_value(c->sp, col, c->u[row * c->k + col], c->buf, sizeof c->buf);
}

static int cmd_converge(const char *path, const char *script,
                        double target, size_t max_r, int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    if (sp.group_count > 0) {
        fprintf(stderr, "Error: converge is per-factor; remove the groups: section\n");
        return 1;
    }

    size_t r = sp.trajectories > 0 ? sp.trajectories : 4;
    size_t cap = max_r ? max_r : DOE_MAX_TRAJECTORIES;
    if (cap > DOE_MAX_TRAJECTORIES) cap = DOE_MAX_TRAJECTORIES;

    size_t total_evals = 0, rounds = 0;
    int converged = 0;
    double widest = 0.0;
    char err[DOE_ERR_SIZE];

    /* Progress on stderr so --json owns stdout. */
    FILE *out = as_json ? stderr : stdout;
    fprintf(out, "Converging on a %.6g-wide 95%% CI (cap %zu trajectories)\n\n",
            target, cap);
    fprintf(out, "%12s %12s %14s\n", "trajectories", "runs", "widest CI");

    if (as_json) {
        printf("{\n  \"tool\": \"morris\",\n  \"command\": \"converge\",\n");
        printf("  \"schema\": %d,\n", MORRIS_JSON_SCHEMA);
        printf("  \"target_ci\": %s,\n", JNUM(target));
        printf("  \"rounds\": [\n");
    }

    for (;;) {
        doe_space_t at = sp;
        at.trajectories = r;

        morris_design_t d;
        if (morris_design_build(&at, &d, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            return 1;
        }

        double *y = malloc(d.npoints * sizeof *y);
        if (!y) { fprintf(stderr, "Error: out of memory\n"); morris_design_free(&d); return 1; }

        conv_ctx_t ctx = { .script = script, .sp = &at, .u = d.u, .k = d.k };
        if (doe_run_capture(&at, "MORRIS", script, d.npoints, conv_value, &ctx, y, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            free(y); morris_design_free(&d); return 1;
        }
        total_evals += d.npoints;
        rounds++;

        morris_effect_t *eff = NULL;
        size_t count = 0;
        if (morris_analyze(&at, y, d.npoints, &eff, &count, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            free(y); morris_design_free(&d); return 1;
        }

        widest = 0.0;
        for (size_t i = 0; i < count; i++) {
            double w = eff[i].mu_star_hi - eff[i].mu_star_lo;
            if (w > widest) widest = w;
        }

        fprintf(out, "%12zu %12zu %14.6g\n", r, d.npoints, widest);
        if (as_json)
            printf("    {\"trajectories\": %zu, \"runs\": %zu, \"widest_ci\": %s}%s\n",
                   r, d.npoints, JNUM(widest),
                   (widest <= target || r * 2 > cap) ? "" : ",");

        free(eff); free(y); morris_design_free(&d);

        if (widest <= target) { converged = 1; break; }
        if (r * 2 > cap) break;
        r *= 2;
    }

    if (as_json) {
        printf("  ],\n");
        printf("  \"converged\": %s,\n", converged ? "true" : "false");
        printf("  \"trajectories\": %zu,\n", r);
        printf("  \"widest_ci\": %s,\n", JNUM(widest));
        printf("  \"evaluations\": %zu,\n", total_evals);
        printf("  \"rounds_run\": %zu,\n", rounds);
        printf("  \"cap\": %zu\n}\n", cap);
    }

    if (converged) {
        fprintf(out, "\nConverged at %zu trajectories, %zu evaluations total.\n"
                     "Write `trajectories: %zu` into the .space; the design is a pure\n"
                     "function of (factors, r, grid_levels, seed), so that reproduces\n"
                     "this run exactly.\n", r, total_evals, r);
        return 0;
    }

    /* Capped without converging is a real answer, not a crash: it says the
     * budget you allowed cannot resolve this model. Exit non-zero so a script
     * notices. */
    fprintf(stderr,
        "\nDid NOT converge: the widest CI is still %.6g at the %zu-trajectory cap,\n"
        "after %zu evaluations. Either raise --max-trajectories, accept a wider\n"
        "target, or take it as the finding -- a response this noisy will not give\n"
        "a resolvable ranking at any budget you can afford.\n", widest, cap, total_evals);
    return 1;
}

/* ---- bifurcate: screen, drop, split, repeat ---- */

typedef struct {
    const char        *script;
    const doe_space_t *sp;
    const double      *u;
    size_t             k;
    char               buf[DOE_MAX_VALUE];
} bif_ctx_t;

static const char *bif_value(void *vctx, size_t row, size_t col) {
    bif_ctx_t *c = (bif_ctx_t *)vctx;
    return doe_factor_value(c->sp, col, c->u[row * c->k + col],
                            c->buf, sizeof c->buf);
}

static int bif_eval(void *ctx, const doe_space_t *sp, const double *u,
                    size_t npoints, size_t k, double *responses, char *err) {
    bif_ctx_t *c = (bif_ctx_t *)ctx;
    c->sp = sp; c->u = u; c->k = k;
    return doe_run_capture(sp, "MORRIS", c->script, npoints,
                           bif_value, c, responses, err);
}

/*
 * Bifurcation's output is a DECISION -- which factors survived -- reached by a
 * different route than `analyze`, and it was available only as prose. A
 * consumer driving the cheap path to a survivor set had to scrape it.
 */
static void print_bifurcate_json(const doe_space_t *sp,
                                 const morris_bifurcate_result_t *res,
                                 double keep_share, size_t worst, size_t flat) {
    printf("{\n");
    printf("  \"tool\": \"morris\",\n");
    printf("  \"command\": \"bifurcate\",\n");
    printf("  \"schema\": %d,\n", MORRIS_JSON_SCHEMA);
    printf("  \"trajectories\": %zu,\n", sp->trajectories);
    printf("  \"keep_share\": %s,\n", JNUM(keep_share > 0 ? keep_share : 0.9));
    printf("  \"factor_count\": %zu,\n", sp->factor_count);
    printf("  \"rounds_run\": %zu,\n", res->rounds_run);
    printf("  \"evaluations\": %zu,\n", res->evaluations);
    printf("  \"predicted_max\": %zu,\n", res->predicted_max);
    printf("  \"worst_case_budget\": %zu,\n", worst);
    printf("  \"per_factor_budget\": %zu,\n", flat);
    printf("  \"survivor_count\": %zu,\n", res->survivor_count);
    printf("  \"survivors\": [");
    {
        int first = 1;
        for (size_t f = 0; f < sp->factor_count; f++) {
            if (!res->survivors[f]) continue;
            if (!first) printf(", ");
            first = 0;
            printf("%s", JSTR(sp->factors[f].name));
        }
    }
    printf("],\n");
    printf("  \"dropped\": [");
    {
        int first = 1;
        for (size_t f = 0; f < sp->factor_count; f++) {
            if (res->survivors[f]) continue;
            if (!first) printf(", ");
            first = 0;
            printf("%s", JSTR(sp->factors[f].name));
        }
    }
    printf("],\n");
    printf("  \"trace\": [\n");
    for (size_t i = 0; i < res->trace_count; i++) {
        printf("    {\"round\": %zu, \"group\": %s, \"mu_star\": %s, "
               "\"members\": %zu, \"kept\": %s}%s\n",
               res->trace[i].round + 1, JSTR(res->trace[i].group),
               JNUM(res->trace[i].mu_star), res->trace[i].member_count,
               res->trace[i].kept ? "true" : "false",
               i + 1 < res->trace_count ? "," : "");
    }
    printf("  ],\n");
    /*
     * Both warnings are fields as well as stderr text. `low_trajectories` in
     * particular: below the measured threshold an important factor can be
     * DROPPED silently, and a machine consumer never reads the prose that
     * says so.
     */
    printf("  \"low_trajectories\": %s,\n", res->low_trajectories ? "true" : "false");
    printf("  \"min_trajectories\": %d,\n", MORRIS_BIFURCATE_MIN_TRAJECTORIES);
    printf("  \"stopped_on_tie\": %s\n", res->stopped_on_tie ? "true" : "false");
    printf("}\n");
}

static int cmd_bifurcate(const char *path, const char *script, double keep_share,
                         int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    morris_bifurcate_opts_t opts;
    memset(&opts, 0, sizeof opts);
    opts.keep_share = keep_share;

    /*
     * Cost first. A screening method whose price you learn afterwards is not
     * usable for planning an expensive experiment, which is the entire point
     * of screening. The worst case assumes every group survives and splits
     * every round -- so it can exceed per-factor screening. Bifurcation is a
     * bet that importance is concentrated; when it is not, screening every
     * factor directly is cheaper.
     */
    size_t worst = morris_bifurcate_budget(&sp, &opts);
    size_t flat  = sp.trajectories * (sp.factor_count + 1);
    /* Cost goes to stderr under --json: it is printed before any evaluation
     * runs, so on stdout it would sit above the document. */
    FILE *pre = as_json ? stderr : stdout;
    fprintf(pre, "Bifurcating %zu factors, keep-share %.2f\n", sp.factor_count,
            opts.keep_share > 0 ? opts.keep_share : 0.9);
    fprintf(pre, "  worst-case budget : %zu evaluations\n", worst);
    fprintf(pre, "  per-factor would be: %zu\n\n", flat);

    bif_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.script = script;

    char err[DOE_ERR_SIZE];
    morris_bifurcate_result_t res;
    if (morris_bifurcate(&sp, &opts, bif_eval, &ctx, &res, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    if (as_json) {
        print_bifurcate_json(&sp, &res, opts.keep_share, worst, flat);
        goto diagnostics;
    }

    size_t round = (size_t)-1;
    for (size_t i = 0; i < res.trace_count; i++) {
        if (res.trace[i].round != round) {
            round = res.trace[i].round;
            printf("Round %zu:\n", round + 1);
            printf("  %-20s %12s %8s  %s\n", "group", "mu*", "members", "");
        }
        printf("  %-20s %12.4g %8zu  %s\n",
               res.trace[i].group, res.trace[i].mu_star,
               res.trace[i].member_count,
               res.trace[i].kept ? "keep" : "drop");
    }

    printf("\n%zu survivor(s) of %zu factors, %zu round(s), %zu evaluations "
           "(worst case was %zu):\n",
           res.survivor_count, sp.factor_count, res.rounds_run,
           res.evaluations, res.predicted_max);
    for (size_t f = 0; f < sp.factor_count; f++)
        if (res.survivors[f]) printf("  %s\n", sp.factors[f].name);

diagnostics:
    /* stderr in both modes -- a dropped factor is silent, and the warning
     * saying so must not be the thing that gets dropped. */
    if (res.low_trajectories) {
        fprintf(stderr,
            "\nWARNING: trajectories = %zu, below the %d that group screening\n"
            "needs to be reliable. Measured on 1024 factors with 8 important ones\n"
            "at alternating signs: at r=10, 2 of 8 were MISSED when equal-and-\n"
            "opposite factors shared a group; at r=20 and above, none were.\n"
            "A dropped factor is silent -- raise trajectories and re-run.\n",
            sp.trajectories, MORRIS_BIFURCATE_MIN_TRAJECTORIES);
    }

    if (res.stopped_on_tie) {
        fprintf(stderr,
            "\nNote: stopped early -- the keep/drop cut fell inside a near-tie.\n"
            "Splitting further cannot resolve it (see validation check C), so the\n"
            "tied groups were all kept rather than chosen between arbitrarily.\n");
    }

    morris_bifurcate_free(&res);
    return 0;
}

static int cmp_group_mu_star(const void *a, const void *b) {
    const morris_group_effect_t *x = a, *y = b;
    if (x->mu_star < y->mu_star) return 1;
    if (x->mu_star > y->mu_star) return -1;
    return 0;
}

/* Group screening: r*(G+1) runs, ranked by the absolute group effect. */
static int cmd_analyze_groups(const doe_space_t *sp, const char *csv, const char *metric,
                              int as_json) {
    size_t np = morris_group_npoints(sp);
    double *responses = malloc(np * sizeof *responses);
    if (!responses) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    for (size_t i = 0; i < np; i++) responses[i] = 0.0 / 0.0;

    char err[DOE_ERR_SIZE];
    size_t got = 0;
    if (doe_csv_read_metric(csv, metric, responses, np, &got, err) != 0) {
        fprintf(stderr, "Error reading results: %s\n", err);
        free(responses);
        return 1;
    }

    morris_group_effect_t *eff = NULL;
    size_t count = 0;
    if (morris_group_analyze(sp, responses, np, &eff, &count, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(responses);
        return 1;
    }
    free(responses);

    qsort(eff, count, sizeof *eff, cmp_group_mu_star);

    if (as_json) {
        print_groups_json(sp, metric, np, eff, count);
    } else {
        printf("Morris GROUP effects (metric: %s) — %zu trajectories, %zu groups over %zu factors\n",
               metric, sp->trajectories, sp->group_count, sp->factor_count);
        printf("%zu runs, against %zu for per-factor screening.\n\n",
               np, sp->trajectories * (sp->factor_count + 1));
        printf("%-20s %12s %12s %8s\n", "Group", "mu*", "spread", "members");
        printf("%-20s %12s %12s %8s\n", "-----", "----", "------", "-------");
        for (size_t i = 0; i < count; i++) {
            printf("%-20s %12.4g %12.4g %8zu\n",
                   eff[i].name, eff[i].mu_star, eff[i].sigma, eff[i].member_count);
        }
    }

    /*
     * The cut-gap diagnostic, per EXPANSION.md E1: mu*'s ranking is only as
     * trustworthy as the separation at the point you cut. Measured in
     * validation check C -- a boundary inside a near-tie never resolves, at
     * any trajectory count.
     */
    if (count >= 2) {
        double hi = eff[count - 2].mu_star, lo = eff[count - 1].mu_star;
        if (lo > 0.0 && hi / lo < 1.05) {
            fprintf(stderr,
                "\nWARNING: the bottom two groups differ by only %.1f%%. A keep/drop\n"
                "cut there is not resolvable at any trajectory count -- keep both, or\n"
                "separate them with a method that estimates magnitude.\n",
                (hi / lo - 1.0) * 100.0);
        }
    }

    if (!as_json)
        printf("\nRanked by mu* (importance), the mean ABSOLUTE group effect, so a group\n"
               "holding factors with opposing signs still registers. 'spread' is the\n"
               "variability of that absolute effect, NOT the per-factor interaction flag.\n"
               "Split the survivors and re-run to localise within a group.\n");
    free(eff);
    return 0;
}

/*
 * `keep` is the number of leading factors the --keep-share rule retains, or 0
 * when no rule was asked for. Both renderers take it as an argument rather
 * than recomputing it, so the table and the JSON cannot disagree about where
 * the cut fell -- which is the same failure mode, one level up, as a display
 * and a parser disagreeing about where a column ends.
 */
static void print_effects_json(const doe_space_t *sp, const char *metric, size_t np,
                               const morris_effect_t *eff, size_t count,
                               double total_mu, double keep_share,
                               size_t keep, double cum, int all_zero) {
    printf("{\n");
    printf("  \"tool\": \"morris\",\n");
    printf("  \"command\": \"analyze\",\n");
    printf("  \"schema\": %d,\n", MORRIS_JSON_SCHEMA);
    printf("  \"mode\": \"per-factor\",\n");
    print_metric_field(metric);
    printf("  \"trajectories\": %zu,\n", sp->trajectories);
    printf("  \"runs\": %zu,\n", np);
    printf("  \"factor_count\": %zu,\n", count);
    printf("  \"total_mu_star\": %s,\n", JNUM(total_mu));
    printf("  \"all_zero\": %s,\n", all_zero ? "true" : "false");
    printf("  \"factors\": [\n");
    for (size_t i = 0; i < count; i++) {
        int interacting = eff[i].sigma >= 0.5 * eff[i].mu_star && eff[i].mu_star > 0;
        printf("    {\"factor\": %s, \"rank\": %zu, \"mu\": %s, \"mu_star\": %s, "
               "\"mu_star_lo\": %s, \"mu_star_hi\": %s, \"sigma\": %s, "
               "\"share\": %s, \"interacting\": %s}%s\n",
               JSTR(eff[i].name), i + 1,
               JNUM(eff[i].mu), JNUM(eff[i].mu_star),
               JNUM(eff[i].mu_star_lo), JNUM(eff[i].mu_star_hi), JNUM(eff[i].sigma),
               JNUM(total_mu > 0.0 ? eff[i].mu_star / total_mu : 0.0),
               interacting ? "true" : "false",
               i + 1 < count ? "," : "");
    }
    printf("  ],\n");

    if (keep == 0) {
        /*
         * No cut was requested, so there is no gap to report -- the keys stay
         * present and null rather than vanishing, so a consumer can read them
         * unconditionally. The factor list carries every mu*, so a caller
         * applying its own cut can compute the gap at whichever rank it picks.
         */
        printf("  \"keep\": null,\n");
        printf("  \"cut_at\": null,\n");
        printf("  \"gap_at_cut\": null,\n");
        printf("  \"cut_is_tie\": false\n");
    } else {
        double gap = -1.0;
        int overlap = 0;
        if (keep < count) {
            if (eff[keep].mu_star > 0.0) gap = eff[keep - 1].mu_star / eff[keep].mu_star;
            overlap = eff[keep - 1].mu_star_lo < eff[keep].mu_star_hi;
        }
        printf("  \"keep\": {\n");
        printf("    \"share_requested\": %s,\n", JNUM(keep_share));
        printf("    \"share_achieved\": %s,\n",
               JNUM(total_mu > 0.0 ? cum / total_mu : 0.0));
        printf("    \"count\": %zu,\n", keep);
        printf("    \"factors\": [");
        for (size_t i = 0; i < keep; i++)
            printf("%s%s", i ? ", " : "", JSTR(eff[i].name));
        printf("],\n");
        printf("    \"ci_overlap_at_cut\": %s\n", overlap ? "true" : "false");
        printf("  },\n");
        printf("  \"cut_at\": \"keep-share\",\n");
        printf("  \"gap_at_cut\": %s,\n", gap >= 0.0 ? JNUM(gap) : "null");
        printf("  \"cut_is_tie\": %s\n", (gap >= 0.0 && gap < 1.05) ? "true" : "false");
    }
    printf("}\n");
}

static int cmd_analyze(const char *path, const char *csv, const char *metric,
                       double keep_share, int as_json) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    /* The .space decides; see the note on design_view_t. */
    if (sp.group_count > 0) return cmd_analyze_groups(&sp, csv, metric, as_json);

    size_t np = morris_npoints(&sp);
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

    morris_effect_t *eff = NULL;
    size_t count = 0;
    if (morris_analyze(&sp, responses, np, &eff, &count, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(responses);
        return 1;
    }
    free(responses);

    qsort(eff, count, sizeof *eff, cmp_mu_star);

    double total_mu = 0.0;
    for (size_t i = 0; i < count; i++) total_mu += eff[i].mu_star;

    /*
     * Keep rules. --keep-share is the default recommendation over a fixed
     * count because validation check C measured that a fixed cut is only as
     * trustworthy as the gap it lands in: on the g-function the top-5 cut was
     * 100% correct from r=20 while the top-3 cut never resolved at any budget,
     * because it fell inside a 1% tie. A share-based cut at least lands where
     * the mass runs out rather than at an arbitrary index.
     */
    double cum = 0.0;
    size_t keep = 0;                       /* 0 = no keep rule was asked for */
    if (keep_share > 0.0 && total_mu > 0.0) {
        for (size_t i = 0; i < count; i++) {
            cum += eff[i].mu_star;
            keep = i + 1;
            if (cum / total_mu >= keep_share) break;
        }
    }

    int all_zero = 1;
    for (size_t i = 0; i < count; i++)
        if (eff[i].mu_star != 0.0) { all_zero = 0; break; }

    if (as_json) {
        print_effects_json(&sp, metric, np, eff, count, total_mu, keep_share,
                           keep, cum, all_zero);
    } else {
        printf("Morris elementary effects (metric: %s) — %zu trajectories\n\n",
               metric, sp.trajectories);
        /*
         * mu* and its interval are SEPARATE COLUMNS. They were one -- printed
         * as "215.6[210,221]" -- and a consumer splitting the row on
         * whitespace read that as field 1 and failed to parse it as a number.
         * Every row failed the same way, so the result was an empty ranking
         * rather than a partial one, and the caller took it for "no factors
         * ranked" instead of an error. Keep the interval a single
         * space-free token for the same reason: "[210, 221]" would split in
         * two and shift every column after it.
         */
        printf("%-20s %12s %16s %12s   %s\n",
               "Factor", "mu*", "[95% CI]", "sigma", "note");
        printf("%-20s %12s %16s %12s   %s\n",
               "------", "----", "--------", "-----", "----");
        for (size_t i = 0; i < count; i++) {
            const char *note = (eff[i].sigma >= 0.5 * eff[i].mu_star && eff[i].mu_star > 0)
                               ? "interacting/nonlinear" : "";
            char ci[48];
            snprintf(ci, sizeof ci, "[%.3g,%.3g]", eff[i].mu_star_lo, eff[i].mu_star_hi);
            printf("%-20s %12.4g %16s %12.4g   %s\n",
                   eff[i].name, eff[i].mu_star, ci, eff[i].sigma, note);
        }

        if (keep > 0) {
            printf("\n--keep-share %.2f keeps %zu of %zu factors "
                   "(%.1f%% of total mu*):\n  ", keep_share, keep, count,
                   100.0 * cum / total_mu);
            for (size_t i = 0; i < keep; i++) printf("%s%s", i ? ", " : "", eff[i].name);
            printf("\n");
        }
    }

    /*
     * Diagnostics go to stderr in BOTH modes. A warning that only the
     * human-readable path emits is how a silently-degraded screening run
     * happens: the consumer sees a well-formed document, nothing errors, and
     * the fact that the cut was not resolvable never reaches anyone.
     */
    if (keep > 0 && keep < count) {
        double hi = eff[keep - 1].mu_star, lo = eff[keep].mu_star;
        if (lo > 0.0 && hi / lo < 1.05) {
            fprintf(stderr,
                "\nWARNING: the keep/drop cut falls inside a %.1f%% gap "
                "(%s -> %s).\nThat ranking is not resolvable at any trajectory "
                "count -- keep both, or separate them with a method that\n"
                "estimates magnitude (sobol).\n",
                (hi / lo - 1.0) * 100.0, eff[keep - 1].name, eff[keep].name);
        }
        if (eff[keep - 1].mu_star_lo < eff[keep].mu_star_hi) {
            fprintf(stderr,
                "\nNote: the confidence intervals of '%s' (kept) and '%s' "
                "(dropped)\noverlap, so their order is not established at "
                "this trajectory count.\n",
                eff[keep - 1].name, eff[keep].name);
        }
    }

    if (!as_json)
        printf("\nRanked by mu* (importance). sigma >= mu*/2 flags interaction/nonlinearity;\n"
               "factors at the bottom with small mu* are screening drop candidates.\n");

    /*
     * Every mu* being exactly zero is legal -- it means no factor moved the
     * output anywhere in the space -- but in practice it almost always means
     * the model script ignores its environment, prints a constant, or the
     * ranges are too narrow to move it. Reporting a table of zeros without
     * comment invites someone to conclude "nothing matters" from what is
     * really a broken harness. Note it on stderr so piped output is unchanged;
     * --json also carries it as `all_zero`, because a machine consumer will
     * never see this text and the whole point is that it not be missed.
     */
    if (all_zero && count > 0) {
        fprintf(stderr,
            "\nNote: every mu* is exactly zero -- the response did not change at any\n"
            "design point. That is a valid result only if every factor is genuinely\n"
            "inert. Check first that the model reads MORRIS_<factor> from the\n"
            "environment, that it prints a varying number, and that the ranges in\n"
            "the .space are wide enough to move it.\n");
    }

    free(eff);
    return 0;
}

static int cmd_validate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    morris_design_t d;
    if (morris_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Invalid: %s\n", err); return 1; }
    printf("Valid: %zu factors, %zu runs (%zu trajectories, %zu grid levels)\n",
           sp.factor_count, d.npoints, sp.trajectories, sp.grid_levels);
    morris_design_free(&d);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("morris (robust toolkit) %d.%d.%d\n",
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
        size_t max_r = 0;
        int as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--target-ci") == 0 && i + 1 < argc) {
                target = strtod(argv[++i], NULL);
                if (!(target > 0.0)) {
                    fprintf(stderr, "--target-ci must be > 0\n"); return 1;
                }
            } else if (strcmp(argv[i], "--max-trajectories") == 0 && i + 1 < argc) {
                max_r = (size_t)strtoul(argv[++i], NULL, 10);
                if (max_r < 1) { fprintf(stderr, "--max-trajectories must be >= 1\n"); return 1; }
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
        return cmd_converge(path, argv[3], target, max_r, as_json);
    }
    if (strcmp(cmd, "bifurcate") == 0) {
        if (argc < 4) { fprintf(stderr, "bifurcate needs a script argument\n"); return 1; }
        double keep_share = 0.0;      /* 0 -> library default (0.9) */
        int as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) as_json = 1;
            else if (strcmp(argv[i], "--keep-share") == 0 && i + 1 < argc) {
                keep_share = strtod(argv[++i], NULL);
                if (!(keep_share > 0.0 && keep_share <= 1.0)) {
                    fprintf(stderr, "--keep-share must be in (0, 1]\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
        return cmd_bifurcate(path, argv[3], keep_share, as_json);
    }
    if (strcmp(cmd, "analyze") == 0) {
        if (argc < 4) { fprintf(stderr, "analyze needs a results.csv argument\n"); return 1; }
        const char *metric = "response";
        double keep_share = 0.0;
        int as_json = 0;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric = argv[++i];
            else if (strcmp(argv[i], "--json") == 0) as_json = 1;
            else if (strcmp(argv[i], "--keep-share") == 0 && i + 1 < argc) {
                keep_share = strtod(argv[++i], NULL);
                if (!(keep_share > 0.0 && keep_share <= 1.0)) {
                    fprintf(stderr, "--keep-share must be in (0, 1]\n"); return 1;
                }
            }
            else {
                /*
                 * Unknown options used to be ignored. A caller asking for
                 * --format json against a build that predates it would have
                 * got the human table and exit 0 -- a silent wrong answer, the
                 * same shape of failure this flag exists to prevent.
                 */
                fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
        return cmd_analyze(path, argv[3], metric, keep_share, as_json);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
