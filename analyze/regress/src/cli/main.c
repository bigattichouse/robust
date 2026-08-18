/*
 * regress — standardized regression coefficients (SRC/SRRC) and R^2.
 *
 * The one thing variance shares cannot tell you: the DIRECTION of an effect.
 * Sobol says a factor owns 30% of the variance; it does not say whether
 * raising it raises or lowers the response. SRC does, with a sign.
 *
 * R^2 doubles as a trust diagnostic. Near 1 and the linear story suffices --
 * the coefficients are the whole picture. Low and it does not, which is
 * precisely when the variance-based indices were worth their cost.
 *
 * Consumes runs already paid for: no sampling, no model execution.
 * EXPANSION.md E1.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


/*
 * Below this, a coefficient is floating-point noise rather than a direction.
 * An exactly-inert factor comes back as ~1e-14, and reporting that as "raises
 * the response" is worse than saying nothing -- someone might act on it.
 */
#define REGRESS_ZERO 1e-9

/*
 * The machine-readable contract, same as every other tool's: `tool`,
 * `command` and `schema` lead the document so a consumer can identify what it
 * is holding before it reads a single field. The table below is a display;
 * these keys are the interface. Additions are free; renames and removals need
 * a schema bump.
 */
#define REGRESS_JSON_SCHEMA 1

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <file.space> <data.csv> [--metric NAME] [--ranks] [--json]\n"
        "\n"
        "  data.csv must carry one column per factor named in the .space, plus\n"
        "  the metric column. `morris sample` emits the factor columns; join\n"
        "  your responses onto it.\n"
        "\n"
        "  --metric NAME  response column (default: response)\n"
        "  --ranks        SRRC: regress on ranks instead of values, which\n"
        "                 captures a monotone but non-linear relationship\n"
        "  --json         machine-readable output\n",
        prog);
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
    const char *space_path = argv[1], *csv_path = argv[2];
    const char *metric = "response";
    int use_ranks = 0, as_json = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric = argv[++i];
        else if (strcmp(argv[i], "--ranks") == 0) use_ranks = 1;
        else if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else { fprintf(stderr, "Error: unknown option '%s'\n", argv[i]); usage(argv[0]); return 2; }
    }

    char *content = read_file(space_path);
    if (!content) { fprintf(stderr, "Error: cannot open '%s'\n", space_path); return 1; }
    doe_space_t sp; char err[DOE_ERR_SIZE];
    if (doe_space_parse(content, &sp, err) != 0) {
        fprintf(stderr, "Error parsing %s: %s\n", space_path, err);
        free(content); return 1;
    }
    free(content);

    /*
     * Read the whole table once, then pull the columns by name. This used to
     * be a private line-splitting loop -- one of two left in the suite -- with
     * its own trim, its own split, and its own growth. doe_table does the
     * reading; what is left here is the part that is actually about
     * regression: which columns, and what to refuse.
     */
    doe_table_t tbl;
    if (doe_table_read(csv_path, &tbl, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    /* Resolve one column per factor, plus the metric. */
    long *col = malloc(sp.factor_count * sizeof *col);
    if (!col) { fprintf(stderr, "Error: out of memory\n"); doe_table_free(&tbl); return 1; }
    long mcol = doe_table_col(&tbl, metric);
    if (mcol < 0) {
        fprintf(stderr, "Error: metric column '%s' not found in %s\n", metric, csv_path);
        free(col); doe_table_free(&tbl); return 1;
    }
    for (size_t j = 0; j < sp.factor_count; j++) {
        col[j] = doe_table_col(&tbl, sp.factors[j].name);
        if (col[j] < 0) {
            fprintf(stderr, "Error: factor '%s' has no column in %s\n",
                    sp.factors[j].name, csv_path);
            free(col); doe_table_free(&tbl); return 1;
        }
    }

    size_t n = tbl.nrows;
    double *X = malloc(n * sp.factor_count * sizeof *X);
    double *y = malloc(n * sizeof *y);
    if (!X || !y) {
        fprintf(stderr, "Error: out of memory\n");
        free(X); free(y); free(col); doe_table_free(&tbl); return 1;
    }

    for (size_t r = 0; r < n; r++) {
        for (size_t j = 0; j < sp.factor_count; j++) {
            if (doe_table_number(&tbl, r, (size_t)col[j], &X[r * sp.factor_count + j]) != 0) {
                fprintf(stderr, "Error: row %zu has a missing or non-numeric value\n", r + 1);
                free(X); free(y); free(col); doe_table_free(&tbl); return 1;
            }
        }
        if (doe_table_number(&tbl, r, (size_t)mcol, &y[r]) != 0) {
            fprintf(stderr, "Error: row %zu: non-numeric %s\n", r + 1, metric);
            free(X); free(y); free(col); doe_table_free(&tbl); return 1;
        }
    }
    doe_table_free(&tbl);

    if (use_ranks) doe_rank_transform(X, y, n, sp.factor_count);

    double *coef = calloc(sp.factor_count, sizeof *coef);
    double r2 = 0.0;
    if (!coef) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    if (doe_ols_src(X, y, n, sp.factor_count, coef, &r2, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(X); free(y); free(col); free(coef);
        return 1;
    }

    /* Rank by |coefficient| — magnitude is importance, sign is direction. */
    size_t *order = malloc(sp.factor_count * sizeof *order);
    for (size_t i = 0; i < sp.factor_count; i++) order[i] = i;
    for (size_t i = 0; i < sp.factor_count; i++)
        for (size_t j = i + 1; j < sp.factor_count; j++)
            if (fabs(coef[order[j]]) > fabs(coef[order[i]])) {
                size_t t = order[i]; order[i] = order[j]; order[j] = t;
            }

    if (as_json) {
        /*
         * Names are escaped, not interpolated raw. A factor name may contain a
         * quote (the .space parser rejects only control characters) and the
         * metric comes straight from argv -- either one used to produce a
         * document no parser would accept, from a mode whose entire purpose is
         * being parsed.
         */
        char *m = doe_json_escape(metric);
        printf("{\n  \"tool\": \"regress\",\n  \"command\": \"analyze\",\n");
        printf("  \"schema\": %d,\n", REGRESS_JSON_SCHEMA);
        printf("  \"metric\": \"%s\",\n  \"kind\": \"%s\",\n  \"runs\": %zu,\n",
               m ? m : "", use_ranks ? "SRRC" : "SRC", n);
        doe_free(m);
        printf("  \"r2\": %s,\n  \"coefficients\": [\n",
               doe_json_number(r2, (char[DOE_JSON_NUM]){0}, DOE_JSON_NUM));
        for (size_t i = 0; i < sp.factor_count; i++) {
            size_t j = order[i];
            char nm[DOE_JSON_STR(DOE_MAX_NAME)];
            printf("    {\"factor\": %s, \"coef\": %s, \"direction\": \"%s\"}%s\n",
                   doe_json_string(sp.factors[j].name, nm, sizeof nm),
                   doe_json_number(coef[j], (char[DOE_JSON_NUM]){0}, DOE_JSON_NUM),
                   fabs(coef[j]) < REGRESS_ZERO ? "none"
                     : coef[j] > 0 ? "increases" : "decreases",
                   i + 1 < sp.factor_count ? "," : "");
        }
        printf("  ]\n}\n");
    } else {
        printf("%s (metric: %s) — %zu runs, %zu factors\n\n",
               use_ranks ? "SRRC (rank regression)" : "SRC (standardized regression)",
               metric, n, sp.factor_count);
        printf("%-20s %12s  %s\n", "Factor", "coefficient", "direction");
        printf("%-20s %12s  %s\n", "------", "-----------", "---------");
        for (size_t i = 0; i < sp.factor_count; i++) {
            size_t j = order[i];
            printf("%-20s %12.4g  %s\n", sp.factors[j].name, coef[j],
                   fabs(coef[j]) < REGRESS_ZERO ? "no effect"
                 : coef[j] > 0 ? "raises the response" : "lowers the response");
        }
        printf("\nR^2 = %.4f — ", r2);
        if (r2 >= 0.9)
            printf("the linear story explains the response; these coefficients\n"
                   "are the whole picture, and a variance decomposition would add little.\n");
        else if (r2 >= 0.6)
            printf("a linear model captures most but not all of it. Treat the\n"
                   "ranking as indicative and check interactions (Sobol S_T - S_i).\n");
        else
            printf("a linear model does NOT explain this response. The signs may\n"
                   "still be useful but the ranking is not; this is exactly the case\n"
                   "variance-based indices exist for.%s\n",
                   use_ranks ? "" : " Try --ranks first: a monotone\n"
                   "but curved relationship shows up there and not here.");
    }

    free(X); free(y); free(col); free(coef); free(order);
    return 0;
}
