/*
 * grid — small full factorial to RESOLVE an interaction, exactly.
 *
 * Sobol's S_T - S_i flags that a factor works through interactions but not
 * with whom. Morris's sigma says an effect is inconsistent but not why. A
 * full factorial over the two or three suspects answers it exactly: no
 * aliasing, no estimator, no assumption of additivity -- every combination is
 * actually run.
 *
 * That is the point of keeping it small. A 3x3 is nine runs; it is affordable
 * precisely because screening already narrowed the field to a couple of
 * candidates.
 *
 * The interaction is measured as the departure from additivity, which is what
 * "interaction" means: for each cell, how far the response sits from what the
 * row and column effects alone predict.
 */

/* strtok_r — POSIX, not C99. Same convention as core/src/runner.c. */
#define _POSIX_C_SOURCE 200809L

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXG 3

/*
 * The machine-readable contract; see screen/morris/src/cli/main.c for why it
 * exists. Bumped on a rename or removal, never on an addition.
 */
#define GRID_JSON_SCHEMA 1

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <file.space> <script> --factors A,B[,C] [--levels N] [--base lo|mid|hi] [--json]\n"
        "\n"
        "  Runs the full factorial over 2 or 3 named factors, holding the rest\n"
        "  at the base point. Every combination is executed, so the interaction\n"
        "  is measured exactly rather than estimated.\n"
        "\n"
        "  --factors A,B[,C]  2 or 3 factors to cross (required)\n"
        "  --levels N         levels per factor, 2..8 (default 3)\n"
        "  --base lo|mid|hi   where to hold the others (default mid)\n"
"  --json             machine-readable output (stable contract)\n",
        prog);
}

typedef struct {
    const doe_space_t *sp;
    const double      *u;
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
    char *flist = NULL;
    size_t levels = 3;
    double base_u = 0.5;
    const char *base_name = "mid";
    int as_json = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--factors") == 0 && i + 1 < argc) flist = argv[++i];
        else if (strcmp(argv[i], "--levels") == 0 && i + 1 < argc) {
            long L = strtol(argv[++i], NULL, 10);
            if (L < 2 || L > 8) { fprintf(stderr, "Error: --levels must be 2..8\n"); return 2; }
            levels = (size_t)L;
        } else if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base_name = argv[++i];
            if (strcmp(base_name, "lo") == 0)       base_u = 0.0;
            else if (strcmp(base_name, "mid") == 0) base_u = 0.5;
            else if (strcmp(base_name, "hi") == 0)  base_u = 1.0;
            else { fprintf(stderr, "Error: --base must be lo, mid or hi\n"); return 2; }
        } else if (strcmp(argv[i], "--json") == 0) { as_json = 1; }
        else { fprintf(stderr, "Error: unknown option '%s'\n", argv[i]); usage(argv[0]); return 2; }
    }
    if (!flist) { fprintf(stderr, "Error: --factors is required\n"); usage(argv[0]); return 2; }

    char *content = read_file(space_path);
    if (!content) { fprintf(stderr, "Error: cannot open '%s'\n", space_path); return 1; }
    doe_space_t sp; char err[DOE_ERR_SIZE];
    if (doe_space_parse(content, &sp, err) != 0) {
        fprintf(stderr, "Error parsing %s: %s\n", space_path, err);
        free(content); return 1;
    }
    free(content);

    /* Resolve the named factors. */
    size_t idx[MAXG], ng = 0;
    char list[512];
    snprintf(list, sizeof list, "%s", flist);
    char *save = NULL;
    for (char *t = strtok_r(list, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n-1] == ' ') t[--n] = '\0';
        if (ng >= MAXG) {
            fprintf(stderr, "Error: at most %d factors (a full factorial grows as N^g;\n"
                            "       screening should have narrowed it further first)\n", MAXG);
            return 2;
        }
        size_t found = sp.factor_count;
        for (size_t i = 0; i < sp.factor_count; i++)
            if (strcmp(sp.factors[i].name, t) == 0) { found = i; break; }
        if (found == sp.factor_count) {
            fprintf(stderr, "Error: '%s' is not a factor in %s. Available:", t, space_path);
            for (size_t i = 0; i < sp.factor_count; i++) fprintf(stderr, " %s", sp.factors[i].name);
            fprintf(stderr, "\n");
            return 1;
        }
        for (size_t g = 0; g < ng; g++)
            if (idx[g] == found) {
                fprintf(stderr, "Error: factor '%s' named twice\n", t);
                return 2;
            }
        idx[ng++] = found;
    }
    if (ng < 2) { fprintf(stderr, "Error: --factors needs at least 2 names\n"); return 2; }

    size_t npoints = 1;
    for (size_t g = 0; g < ng; g++) npoints *= levels;

    size_t k = sp.factor_count;
    double *u = malloc(npoints * k * sizeof *u);
    double *y = malloc(npoints * sizeof *y);
    if (!u || !y) { fprintf(stderr, "Error: out of memory\n"); return 1; }

    for (size_t r = 0; r < npoints; r++) {
        for (size_t c = 0; c < k; c++) u[r * k + c] = base_u;
        size_t rem = r;
        for (size_t g = 0; g < ng; g++) {
            size_t lv = rem % levels;
            rem /= levels;
            u[r * k + idx[g]] = (double)lv / (double)(levels - 1) * 0.999;
        }
    }

    run_ctx_t ctx; memset(&ctx, 0, sizeof ctx);
    ctx.sp = &sp; ctx.u = u; ctx.k = k;

    /* Progress goes to stderr under --json: it is printed before the runs
     * execute, so on stdout it would sit above the document. */
    FILE *prog = as_json ? stderr : stdout;
    fprintf(prog, "Full factorial over");
    for (size_t g = 0; g < ng; g++)
        fprintf(prog, " %s%s", sp.factors[idx[g]].name, g + 1 < ng ? " x" : "");
    fprintf(prog, " — %zu runs at %zu levels, others held at %s\n\n",
            npoints, levels, base_name);

    if (doe_run_capture(&sp, "GRID", script, npoints, val_of, &ctx, y, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(u); free(y); return 1;
    }

    char vb[DOE_MAX_VALUE];

    /*
     * The interaction statistics, computed ONCE regardless of how they are
     * rendered. Two renderers each doing their own arithmetic is how a table
     * and a document come to disagree about the same experiment.
     */
    double ma = 0.0, mb = 0.0, max_dev = 0.0, ss_int = 0.0, ss_tot = 0.0, biggest = 0.0;
    int interacts = 0;
    if (ng == 2) {
        double grand = 0.0;
        for (size_t i = 0; i < npoints; i++) grand += y[i];
        grand /= (double)npoints;

        double rowm[8] = {0}, colm[8] = {0};
        for (size_t a = 0; a < levels; a++)
            for (size_t b = 0; b < levels; b++) {
                rowm[a] += y[b * levels + a];
                colm[b] += y[b * levels + a];
            }
        for (size_t i = 0; i < levels; i++) {
            rowm[i] /= (double)levels;
            colm[i] /= (double)levels;
        }

        for (size_t a = 0; a < levels; a++)
            for (size_t b = 0; b < levels; b++) {
                double additive = rowm[a] + colm[b] - grand;
                double dev = y[b * levels + a] - additive;
                if (fabs(dev) > max_dev) max_dev = fabs(dev);
                ss_int += dev * dev;
                double d = y[b * levels + a] - grand;
                ss_tot += d * d;
            }

        double row_lo = rowm[0], row_hi = rowm[0], col_lo = colm[0], col_hi = colm[0];
        for (size_t i = 1; i < levels; i++) {
            if (rowm[i] < row_lo) row_lo = rowm[i];
            if (rowm[i] > row_hi) row_hi = rowm[i];
            if (colm[i] < col_lo) col_lo = colm[i];
            if (colm[i] > col_hi) col_hi = colm[i];
        }
        ma = row_hi - row_lo;
        mb = col_hi - col_lo;
        biggest = (ma > mb) ? ma : mb;
        interacts = biggest > 0.0 && max_dev / biggest > 0.1;
    }

    if (as_json) {
        /*
         * Every run with its factor values and response, plus the interaction
         * verdict as a field. `interacts` is the answer this tool exists to
         * give -- a consumer should not have to re-derive it from a threshold
         * it has to know about.
         */
        char nb[DOE_JSON_NUM], sb[DOE_JSON_STR(DOE_MAX_NAME)];
        printf("{\n");
        printf("  \"tool\": \"grid\",\n");
        printf("  \"command\": \"factorial\",\n");
        printf("  \"schema\": %d,\n", GRID_JSON_SCHEMA);
        printf("  \"factors\": [");
        for (size_t g = 0; g < ng; g++) {
            if (g) printf(", ");
            printf("%s", doe_json_string(sp.factors[idx[g]].name, sb, sizeof sb));
        }
        printf("],\n");
        printf("  \"base\": \"%s\",\n", base_name);
        printf("  \"levels\": %zu,\n", levels);
        printf("  \"runs\": %zu,\n", npoints);
        printf("  \"points\": [\n");
        for (size_t r = 0; r < npoints; r++) {
            printf("    {\"run_id\": %zu, \"values\": [", r + 1);
            for (size_t g = 0; g < ng; g++) {
                char vjb[DOE_JSON_STR(DOE_MAX_VALUE)];
                if (g) printf(", ");
                printf("%s", doe_json_string(
                    doe_factor_value(&sp, idx[g], u[r * k + idx[g]], vb, sizeof vb),
                    vjb, sizeof vjb));
            }
            printf("], \"response\": %s}%s\n",
                   doe_json_number(y[r], nb, sizeof nb), r + 1 < npoints ? "," : "");
        }
        printf("  ],\n");
        if (ng != 2) {
            /* Only the two-factor case has a defined interaction here. */
            printf("  \"interaction\": null\n");
        } else {
            printf("  \"main_effects\": [");
            printf("{\"factor\": %s, \"effect\": %s}, ",
                   doe_json_string(sp.factors[idx[0]].name, sb, sizeof sb),
                   doe_json_number(ma, nb, sizeof nb));
            printf("{\"factor\": %s, \"effect\": %s}],\n",
                   doe_json_string(sp.factors[idx[1]].name, sb, sizeof sb),
                   doe_json_number(mb, nb, sizeof nb));
            printf("  \"interaction\": {\n");
            printf("    \"max_departure_from_additivity\": %s,\n",
                   doe_json_number(max_dev, nb, sizeof nb));
            printf("    \"share_of_total_variation\": %s,\n",
                   ss_tot > 0.0 ? doe_json_number(ss_int / ss_tot, nb, sizeof nb) : "null");
            printf("    \"relative_to_larger_main_effect\": %s,\n",
                   biggest > 0.0 ? doe_json_number(max_dev / biggest, nb, sizeof nb) : "null");
            printf("    \"interacts\": %s\n", interacts ? "true" : "false");
            printf("  }\n");
        }
        printf("}\n");
        free(u); free(y);
        return 0;
    }

    if (ng == 2) {
        /* Print the response surface as a table: rows = A, columns = B. */
        printf("%-16s", sp.factors[idx[0]].name);
        for (size_t b = 0; b < levels; b++)
            printf(" %12s", doe_factor_value(&sp, idx[1],
                   (double)b / (double)(levels-1) * 0.999, vb, sizeof vb));
        printf("   <- %s\n", sp.factors[idx[1]].name);
        for (size_t a = 0; a < levels; a++) {
            printf("%-16s", doe_factor_value(&sp, idx[0],
                   (double)a / (double)(levels-1) * 0.999, vb, sizeof vb));
            for (size_t b = 0; b < levels; b++)
                printf(" %12.6g", y[b * levels + a]);
            printf("\n");
        }

        /*
         * Interaction = departure from additivity, computed above so this
         * table and --json report the same numbers.
         */
        printf("\nMain effect of %-14s %.6g\n", sp.factors[idx[0]].name, ma);
        printf("Main effect of %-14s %.6g\n", sp.factors[idx[1]].name, mb);
        printf("Interaction (max departure from additivity): %.6g\n", max_dev);
        if (ss_tot > 0.0)
            printf("Interaction share of total variation: %.1f%%\n", 100.0 * ss_int / ss_tot);
        if (biggest > 0.0)
            printf("Interaction relative to the larger main effect: %.1f%%\n",
                   100.0 * max_dev / biggest);

        /*
         * Judge against the MAIN EFFECTS, not against total variation. The
         * question a user is really asking is "if I optimise these two
         * independently, how wrong will I be?" -- and that is the interaction
         * measured against the effects they would act on. A share-of-variance
         * threshold answers a different question and gets this wrong: for
         * y = a + b + 0.2ab the interaction is a quarter of each main effect,
         * plainly worth knowing, yet only 7.7% of total variation.
         */
        if (interacts)
            printf("\nThese two DO interact. Their main effects are not additive, so\n"
                   "optimising them independently will not find the joint optimum --\n"
                   "read the table, not the two ranges.\n");
        else
            printf("\nNo meaningful interaction here: the response is additive in these\n"
                   "two, so they can be optimised independently. If a screen flagged\n"
                   "them (S_T - S_i, or a large sigma), the partner is elsewhere.\n");
    } else {
        printf("run  ");
        for (size_t g = 0; g < ng; g++) printf("%-14s", sp.factors[idx[g]].name);
        printf("%14s\n", "response");
        for (size_t r = 0; r < npoints; r++) {
            printf("%-5zu", r + 1);
            for (size_t g = 0; g < ng; g++)
                printf("%-14s", doe_factor_value(&sp, idx[g], u[r * k + idx[g]], vb, sizeof vb));
            printf("%14.6g\n", y[r]);
        }
    }

    free(u); free(y);
    return 0;
}
