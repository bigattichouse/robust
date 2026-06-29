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
typedef struct {
    const doe_space_t   *space;
    const morris_design_t *d;
    char buf[DOE_MAX_VALUE];
} run_ctx_t;

static const char *run_value(void *vctx, size_t row, size_t col) {
    run_ctx_t *c = (run_ctx_t *)vctx;
    double u = c->d->u[row * c->d->k + col];
    return doe_factor_value(&c->space->factors[col], u, c->buf, sizeof c->buf);
}

/* ---- commands ---- */

static int cmd_sample(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    morris_design_t d;
    if (morris_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

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
    morris_design_free(&d);
    return 0;
}

static int cmd_generate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    morris_design_t d;
    if (morris_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    printf("%zu points (%zu trajectories x (%zu factors + 1)):\n",
           d.npoints, d.r, d.k);
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
    morris_design_free(&d);
    return 0;
}

static int cmd_run(const char *path, const char *script) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    morris_design_t d;
    if (morris_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Error: %s\n", err); return 1; }

    run_ctx_t ctx = { .space = &sp, .d = &d };
    printf("Running %zu points with '%s'...\n", d.npoints, script);
    int rc = doe_run(&sp, "MORRIS", script, d.npoints, run_value, &ctx, err);
    if (rc != 0) fprintf(stderr, "Error: %s\n", err);
    morris_design_free(&d);
    return rc == 0 ? 0 : 1;
}

static int cmp_mu_star(const void *a, const void *b) {
    const morris_effect_t *x = a, *y = b;
    if (x->mu_star < y->mu_star) return 1;
    if (x->mu_star > y->mu_star) return -1;
    return 0;
}

static int cmd_analyze(const char *path, const char *csv, const char *metric) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

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
