/*
 * ofat — one-factor-at-a-time confirmation around a base point.
 *
 * The rule this tool exists to make cheap, from spec/screening-methods.md §4:
 * on a deterministic model, ANY orthogonal-array effect you intend to act on
 * costs exactly two more runs to verify -- always spend them.
 *
 * The failure it targets is real and documented in this repo: a vinegar L27
 * reported acetic_acid_molarity at 3.53 dB when two OFAT runs showed the true
 * main effect was +0.03%. The 3.5 dB was interaction leakage from another
 * column. An OA balances main effects, not interactions, so an effect can be
 * entirely manufactured by the column assignment. Array size does not fix it.
 *
 * What this measures is the effect AT ONE POINT, which is exactly the
 * complaint people make about OFAT -- and exactly what makes it the right
 * confirmation: you are checking a specific claim about a specific setting,
 * not re-screening.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <file.space> <script> --factor NAME [--levels N] [--base lo|mid|hi]\n"
        "\n"
        "  Holds every other factor at the base point and sweeps NAME across\n"
        "  N levels, so the measured range is that factor's true effect there.\n"
        "\n"
        "  --factor NAME  the factor to verify (required)\n"
        "  --levels N     sweep points, 2..16 (default 3: low, mid, high, which\n"
        "                 also shows curvature)\n"
        "  --base lo|mid|hi   where to hold the others (default mid)\n",
        prog);
}

typedef struct {
    const doe_space_t *sp;
    const double      *u;      /* npoints * k */
    size_t             k;
    char               buf[DOE_MAX_VALUE];
} run_ctx_t;

static const char *val_of(void *vctx, size_t row, size_t col) {
    run_ctx_t *c = (run_ctx_t *)vctx;
    return doe_factor_value(c->sp, col, c->u[row * c->k + col], c->buf, sizeof c->buf);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f); b[got] = '\0';
    return b;
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }
    const char *space_path = argv[1], *script = argv[2];
    const char *fname = NULL;
    size_t levels = 3;
    double base_u = 0.5;
    const char *base_name = "mid";

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--factor") == 0 && i + 1 < argc) fname = argv[++i];
        else if (strcmp(argv[i], "--levels") == 0 && i + 1 < argc) {
            long L = strtol(argv[++i], NULL, 10);
            if (L < 2 || L > 16) { fprintf(stderr, "Error: --levels must be 2..16\n"); return 2; }
            levels = (size_t)L;
        } else if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base_name = argv[++i];
            if (strcmp(base_name, "lo") == 0)       base_u = 0.0;
            else if (strcmp(base_name, "mid") == 0) base_u = 0.5;
            else if (strcmp(base_name, "hi") == 0)  base_u = 1.0;
            else { fprintf(stderr, "Error: --base must be lo, mid or hi\n"); return 2; }
        } else { fprintf(stderr, "Error: unknown option '%s'\n", argv[i]); usage(argv[0]); return 2; }
    }
    if (!fname) { fprintf(stderr, "Error: --factor is required\n"); usage(argv[0]); return 2; }

    char *content = read_file(space_path);
    if (!content) { fprintf(stderr, "Error: cannot open '%s'\n", space_path); return 1; }
    doe_space_t sp; char err[DOE_ERR_SIZE];
    if (doe_space_parse(content, &sp, err) != 0) {
        fprintf(stderr, "Error parsing %s: %s\n", space_path, err);
        free(content); return 1;
    }
    free(content);

    size_t target = sp.factor_count;
    for (size_t i = 0; i < sp.factor_count; i++)
        if (strcmp(sp.factors[i].name, fname) == 0) { target = i; break; }
    if (target == sp.factor_count) {
        fprintf(stderr, "Error: '%s' is not a factor in %s. Available:", fname, space_path);
        for (size_t i = 0; i < sp.factor_count; i++) fprintf(stderr, " %s", sp.factors[i].name);
        fprintf(stderr, "\n");
        return 1;
    }

    size_t k = sp.factor_count;
    double *u = malloc(levels * k * sizeof *u);
    double *y = malloc(levels * sizeof *y);
    if (!u || !y) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    for (size_t r = 0; r < levels; r++) {
        for (size_t c = 0; c < k; c++) u[r * k + c] = base_u;
        /* Sweep the target across [0,1]; the 0.999 guard keeps a categorical
         * factor's top level reachable without u == 1.0 rounding past it. */
        u[r * k + target] = (levels == 1) ? base_u
                          : (double)r / (double)(levels - 1) * 0.999;
    }

    run_ctx_t ctx; memset(&ctx, 0, sizeof ctx);
    ctx.sp = &sp; ctx.u = u; ctx.k = k;

    printf("OFAT confirmation of '%s' — %zu runs, others held at %s\n\n",
           fname, levels, base_name);
    if (doe_run_capture(&sp, "OFAT", script, levels, val_of, &ctx, y, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(u); free(y); return 1;
    }

    char vbuf[DOE_MAX_VALUE];
    printf("%-24s %14s\n", fname, "response");
    printf("%-24s %14s\n", "------------------------", "--------------");
    double lo = y[0], hi = y[0];
    for (size_t r = 0; r < levels; r++) {
        printf("%-24s %14.6g\n",
               doe_factor_value(&sp, target, u[r * k + target], vbuf, sizeof vbuf), y[r]);
        if (y[r] < lo) lo = y[r];
        if (y[r] > hi) hi = y[r];
    }

    double range = hi - lo;
    printf("\nMeasured effect at this base point: %.6g (range over %zu levels)\n",
           range, levels);

    if (range == 0.0) {
        printf("\nThis factor did NOT move the response at all here. If a screen\n"
               "reported an effect for it, that effect was aliasing or noise, not\n"
               "a main effect -- do not act on it.\n");
    } else if (levels >= 3) {
        /* Curvature: is the middle where additivity would put it? */
        double mid_expect = 0.5 * (y[0] + y[levels - 1]);
        double mid_actual = y[levels / 2];
        double curve = fabs(mid_actual - mid_expect);
        if (curve > 0.1 * range)
            printf("Curvature: the mid-level sits %.6g off the straight line between\n"
                   "the ends (%.0f%% of the range). A two-level design would have\n"
                   "missed this; the optimum may not be at an endpoint.\n",
                   curve, 100.0 * curve / range);
    }
    printf("\nThis is the effect AT ONE POINT. It confirms or refutes a specific\n"
           "claim; it does not replace a screen.\n");

    free(u); free(y);
    return 0;
}
