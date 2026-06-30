/*
 * robust CLI — the funnel orchestrator.
 *
 *   robust funnel <file.space> <script> [--keep-fraction F]
 *                 [--html out.html] [--json out.json] [--tgu out.tgu]
 *   robust screen <file.space> <script> [--keep-fraction F]
 *
 * <script> is the model: it reads ROBUST_<factor> env vars and prints one
 * numeric response to stdout.
 */

#include "robust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> <file.space> <script> [OPTIONS]\n"
        "\n"
        "Commands:\n"
        "  funnel <file.space> <script>   Morris screen -> Sobol on survivors\n"
        "  screen <file.space> <script>   Morris screen only (keep/drop list)\n"
        "\n"
        "Options:\n"
        "  --keep-fraction F   keep factors with mu* >= F*max(mu*) (default %.2f)\n"
        "  --html PATH         write the HTML dashboard (funnel only)\n"
        "  --json PATH         write JSON results (funnel only)\n"
        "  --tgu  PATH         write a taguchi .tgu for the survivors (funnel only)\n"
        "\n"
        "The <script> reads ROBUST_<factor> env vars and prints one number.\n",
        prog, ROBUST_KEEP_FRACTION);
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

/* ---- shell evaluator: run the model script, capture its stdout ---- */
typedef struct {
    const doe_space_t *space;
    const double      *u;
    size_t             k;
    char               buf[DOE_MAX_VALUE];
} val_ctx_t;

static const char *shell_value(void *v, size_t row, size_t col) {
    val_ctx_t *c = (val_ctx_t *)v;
    return doe_factor_value(&c->space->factors[col], c->u[row * c->k + col], c->buf, sizeof c->buf);
}

typedef struct { const char *script; } shell_ctx_t;

static int shell_run(void *ctx, const doe_space_t *space, const double *u,
                     size_t npoints, double *responses, char *err) {
    shell_ctx_t *c = (shell_ctx_t *)ctx;
    val_ctx_t vc = { .space = space, .u = u, .k = space->factor_count };
    return doe_run_capture(space, "ROBUST", c->script, npoints, shell_value, &vc, responses, err);
}

/* ---- option parsing ---- */
typedef struct {
    double keep_fraction;
    const char *html, *json, *tgu;
} opts_t;

static void parse_opts(int argc, char **argv, int start, opts_t *o) {
    o->keep_fraction = ROBUST_KEEP_FRACTION;
    o->html = o->json = o->tgu = NULL;
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], "--keep-fraction") == 0 && i + 1 < argc) o->keep_fraction = atof(argv[++i]);
        else if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) o->html = argv[++i];
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) o->json = argv[++i];
        else if (strcmp(argv[i], "--tgu") == 0 && i + 1 < argc) o->tgu = argv[++i];
    }
}

static void print_morris(const robust_result_t *r) {
    printf("Morris screening (%zu factors):\n", r->k);
    printf("  %-18s %10s %10s   %s\n", "factor", "mu*", "sigma", "status");
    for (size_t i = 0; i < r->k; i++) {
        printf("  %-18s %10.4g %10.4g   %s\n", r->effects[i].name,
               r->effects[i].mu_star, r->effects[i].sigma, r->keep[i] ? "KEEP" : "drop");
    }
}

static int cmd_funnel(const char *path, const char *script, const opts_t *o) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    shell_ctx_t sctx = { .script = script };
    robust_result_t r;
    char err[DOE_ERR_SIZE];

    printf("Running funnel on %zu factors (model: %s)...\n", sp.factor_count, script);
    if (robust_funnel(&sp, shell_run, &sctx, o->keep_fraction, &r, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        robust_result_free(&r);
        return 1;
    }

    print_morris(&r);
    printf("\nSobol attribution on %zu survivor(s):\n", r.n_survivors);
    printf("  %-18s %8s %8s   %s\n", "factor", "S1", "ST", "interaction (ST-S1)");
    for (size_t i = 0; i < r.n_indices; i++) {
        const sobol_index_t *x = &r.indices[i];
        printf("  %-18s %8.3f %8.3f   %.3f\n", x->name, x->s1, x->st, x->st - x->s1);
    }

    int rc = 0;
    if (o->html && robust_write_html(&r, o->html, err) != 0) { fprintf(stderr, "Error: %s\n", err); rc = 1; }
    else if (o->html) printf("\nWrote HTML report: %s\n", o->html);
    if (o->json && robust_write_json(&r, o->json, err) != 0) { fprintf(stderr, "Error: %s\n", err); rc = 1; }
    else if (o->json) printf("Wrote JSON: %s\n", o->json);
    if (o->tgu && robust_write_tgu(&r, o->tgu, err) != 0) { fprintf(stderr, "Error: %s\n", err); rc = 1; }
    else if (o->tgu) printf("Wrote taguchi array: %s\n", o->tgu);

    robust_result_free(&r);
    return rc;
}

static int cmd_screen(const char *path, const char *script, const opts_t *o) {
    doe_space_t sp;
    if (load_space(path, &sp) != 0) return 1;

    shell_ctx_t sctx = { .script = script };
    char err[DOE_ERR_SIZE];
    morris_effect_t *eff = NULL;
    size_t nsurv = 0;
    int *keep = malloc(sp.factor_count * sizeof *keep);
    if (!keep) { fprintf(stderr, "Error: out of memory\n"); return 1; }

    if (robust_screen(&sp, shell_run, &sctx, o->keep_fraction, &eff, keep, &nsurv, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(keep);
        return 1;
    }

    robust_result_t r = { .k = sp.factor_count, .effects = eff, .keep = keep,
                          .n_survivors = nsurv, .keep_fraction = o->keep_fraction };
    print_morris(&r);
    printf("\n%zu of %zu factors kept.\n", nsurv, sp.factor_count);

    free(eff);
    free(keep);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("robust (robust toolkit) %d.%d.%d\n",
               DOE_VERSION_MAJOR, DOE_VERSION_MINOR, DOE_VERSION_PATCH);
        return 0;
    }
    if (argc < 4) { usage(argv[0]); return 1; }

    const char *cmd = argv[1];
    const char *path = argv[2];
    const char *script = argv[3];
    opts_t o;
    parse_opts(argc, argv, 4, &o);

    if (strcmp(cmd, "funnel") == 0) return cmd_funnel(path, script, &o);
    if (strcmp(cmd, "screen") == 0) return cmd_screen(path, script, &o);

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
