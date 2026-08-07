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
        "  analyze  <file.space> <results.csv> [--metric NAME]\n"
        "                                        Elementary effects: mu*, sigma\n"
        "\n"
        "A `groups:` section in the .space switches every command to group\n"
        "screening: r*(G+1) runs instead of r*(k+1), ranked by the absolute\n"
        "group effect. The file decides, so the design and the analysis cannot\n"
        "disagree.\n"
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
    return doe_factor_value(&c->space->factors[col], u, c->buf, sizeof c->buf);
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
            printf(",%s", doe_factor_value(&sp.factors[c], d.u[i * d.k + c], buf, sizeof buf));
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
                   doe_factor_value(&sp.factors[c], d.u[i * d.k + c], buf, sizeof buf));
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

static int cmp_group_mu_star(const void *a, const void *b) {
    const morris_group_effect_t *x = a, *y = b;
    if (x->mu_star < y->mu_star) return 1;
    if (x->mu_star > y->mu_star) return -1;
    return 0;
}

/* Group screening: r*(G+1) runs, ranked by the absolute group effect. */
static int cmd_analyze_groups(const doe_space_t *sp, const char *csv, const char *metric) {
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

    printf("\nRanked by mu* (importance), the mean ABSOLUTE group effect, so a group\n"
           "holding factors with opposing signs still registers. 'spread' is the\n"
           "variability of that absolute effect, NOT the per-factor interaction flag.\n"
           "Split the survivors and re-run to localise within a group.\n");
    free(eff);
    return 0;
}

static int cmd_analyze(const char *path, const char *csv, const char *metric) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    /* The .space decides; see the note on design_view_t. */
    if (sp.group_count > 0) return cmd_analyze_groups(&sp, csv, metric);

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

    printf("Morris elementary effects (metric: %s) — %zu trajectories\n\n",
           metric, sp.trajectories);
    printf("%-20s %12s %12s   %s\n", "Factor", "mu*", "sigma", "note");
    printf("%-20s %12s %12s   %s\n", "------", "----", "-----", "----");
    for (size_t i = 0; i < count; i++) {
        const char *note = (eff[i].sigma >= 0.5 * eff[i].mu_star && eff[i].mu_star > 0)
                           ? "interacting/nonlinear" : "";
        printf("%-20s %12.4g %12.4g   %s\n",
               eff[i].name, eff[i].mu_star, eff[i].sigma, note);
    }
    printf("\nRanked by mu* (importance). sigma >= mu*/2 flags interaction/nonlinearity;\n"
           "factors at the bottom with small mu* are screening drop candidates.\n");

    /*
     * Every mu* being exactly zero is legal -- it means no factor moved the
     * output anywhere in the space -- but in practice it almost always means
     * the model script ignores its environment, prints a constant, or the
     * ranges are too narrow to move it. Reporting a table of zeros without
     * comment invites someone to conclude "nothing matters" from what is
     * really a broken harness. Note it on stderr so piped output is unchanged.
     */
    int all_zero = 1;
    for (size_t i = 0; i < count; i++)
        if (eff[i].mu_star != 0.0) { all_zero = 0; break; }
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
    if (strcmp(cmd, "analyze") == 0) {
        if (argc < 4) { fprintf(stderr, "analyze needs a results.csv argument\n"); return 1; }
        const char *metric = "response";
        for (int i = 4; i < argc - 1; i++) {
            if (strcmp(argv[i], "--metric") == 0) { metric = argv[i + 1]; break; }
        }
        return cmd_analyze(path, argv[3], metric);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
