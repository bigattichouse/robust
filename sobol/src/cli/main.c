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
        "  analyze  <file.space> <results.csv> [--metric NAME]\n"
        "                                        First/total indices Si, STi (+ CIs)\n"
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
    return doe_factor_value(&c->space->factors[col], c->u[col], c->buf, sizeof c->buf);
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
            printf(",%s", doe_factor_value(&sp.factors[c], u[c], buf, sizeof buf));
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

static int cmd_analyze(const char *path, const char *csv, const char *metric) {
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
    free(responses);

    printf("Sobol indices (metric: %s) — N=%zu, %zu runs\n\n", metric, sp.samples, np);
    printf("%-18s %18s %18s   %s\n", "Factor", "S1 [95% CI]", "ST [95% CI]", "interaction");
    printf("%-18s %18s %18s   %s\n", "------", "-----------", "-----------", "-----------");
    double sum_s1 = 0.0;
    for (size_t i = 0; i < count; i++) {
        char s1c[40], stc[40];
        snprintf(s1c, sizeof s1c, "%.3f[%.2f,%.2f]", idx[i].s1, idx[i].s1_lo, idx[i].s1_hi);
        snprintf(stc, sizeof stc, "%.3f[%.2f,%.2f]", idx[i].st, idx[i].st_lo, idx[i].st_hi);
        printf("%-18s %18s %18s   %.3f\n", idx[i].name, s1c, stc, idx[i].st - idx[i].s1);
        sum_s1 += idx[i].s1;
    }
    printf("\nSum of first-order Si = %.3f", sum_s1);
    if (sum_s1 > 0.9) printf("  (~1 => additive; OA/Taguchi ranking trustworthy)\n");
    else              printf("  (<1 => interactions present)\n");
    printf("ST ~ 0 => freeze the factor; ST - S1 large => acts through interactions.\n");

    free(idx);
    return 0;
}

static int cmd_validate(const char *path) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;
    char err[DOE_ERR_SIZE];
    sobol_design_t d;
    if (sobol_design_build(&sp, &d, err) != 0) { fprintf(stderr, "Invalid: %s\n", err); return 1; }
    printf("Valid: %zu factors, %zu runs (N=%zu base samples)\n",
           sp.factor_count, d.npoints, sp.samples);
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
