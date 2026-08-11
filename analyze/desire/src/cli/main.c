/*
 * desire — Derringer-Suich desirability (EXPANSION.md E7).
 *
 * Real experiments trade objectives off: yield up, cost down, cycle time down.
 * Every other tool here analyses ONE metric. This collapses several into one
 * by mapping each to [0,1] and taking the geometric mean, then appending a
 * `desirability` column to the results CSV.
 *
 *     desire --max yield --min cost results.csv \
 *       | sobol analyze model.space - --metric desirability
 *
 * The output is the same results-CSV dialect as the input, which is the whole
 * design: the existing single-response pipeline -- screen, attribute, RSM --
 * runs on it unchanged. This is a filter, not a stage.
 *
 * The GEOMETRIC mean, not the arithmetic one, and that is the method rather
 * than a detail: one objective at zero takes the whole design to zero. A
 * candidate that fails a requirement outright cannot be rescued by excelling
 * elsewhere, which is exactly what an arithmetic mean would let it do.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_OBJ    16
#define MAX_COLS  256
#define MAX_ROWS  100000
#define MAXLINE  8192

typedef enum { OBJ_MAX, OBJ_MIN, OBJ_TARGET } obj_kind_t;

typedef struct {
    char       name[128];
    obj_kind_t kind;
    double     target;       /* OBJ_TARGET only */
    int        col;          /* resolved from the header */
    double     lo, hi;       /* observed range, the default bounds */
} objective_t;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n] = '\0';
    return s;
}

static int split(char *line, char **f, int max) {
    int n = 0; char *p = line;
    while (n < max) {
        f[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0'; p = c + 1;
    }
    return n;
}

/*
 * One metric's desirability. Linear between the bounds (Derringer-Suich's
 * weight exponent s=1: without a stated preference curve, anything else is an
 * opinion the data does not support).
 */
static double desirability(const objective_t *o, double y) {
    double lo = o->lo, hi = o->hi;
    if (hi <= lo) return 1.0;          /* constant column: no information, no penalty */

    if (o->kind == OBJ_MAX) {
        if (y <= lo) return 0.0;
        if (y >= hi) return 1.0;
        return (y - lo) / (hi - lo);
    }
    if (o->kind == OBJ_MIN) {
        if (y <= lo) return 1.0;
        if (y >= hi) return 0.0;
        return (hi - y) / (hi - lo);
    }
    /* target: 1 at the target, falling to 0 at whichever bound is further. */
    double t = o->target;
    if (y == t) return 1.0;
    if (y < t) {
        if (t <= lo) return 0.0;
        return (y <= lo) ? 0.0 : (y - lo) / (t - lo);
    }
    if (t >= hi) return 0.0;
    return (y >= hi) ? 0.0 : (hi - y) / (hi - t);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--max COL | --min COL | --target COL:VALUE]... <results.csv|->\n"
        "\n"
        "  Derringer-Suich desirability: maps each named metric to [0,1] over its\n"
        "  observed range, combines them by GEOMETRIC mean, and appends a\n"
        "  `desirability` column. Output is the same CSV dialect as the input, so\n"
        "  the single-response pipeline runs on it unchanged:\n"
        "\n"
        "    desire --max yield --min cost results.csv \\\n"
        "      | sobol analyze model.space - --metric desirability\n"
        "\n"
        "  Geometric, not arithmetic: one objective at zero takes the whole row to\n"
        "  zero. A candidate that fails a requirement outright is not rescued by\n"
        "  excelling elsewhere.\n",
        prog);
}

int main(int argc, char **argv) {
    objective_t objs[MAX_OBJ];
    int nobj = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        int kind = -1;
        if      (strcmp(argv[i], "--max") == 0)    kind = OBJ_MAX;
        else if (strcmp(argv[i], "--min") == 0)    kind = OBJ_MIN;
        else if (strcmp(argv[i], "--target") == 0) kind = OBJ_TARGET;
        else if (strcmp(argv[i], "--help") == 0)   { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else { path = argv[i]; continue; }

        if (i + 1 >= argc) { fprintf(stderr, "Error: %s needs a column\n", argv[i]); return 2; }
        if (nobj == MAX_OBJ) { fprintf(stderr, "Error: too many objectives (max %d)\n", MAX_OBJ); return 2; }

        objective_t *o = &objs[nobj];
        memset(o, 0, sizeof *o);
        o->kind = (obj_kind_t)kind;
        o->col = -1;

        const char *spec = argv[++i];
        if (kind == OBJ_TARGET) {
            const char *colon = strrchr(spec, ':');
            if (!colon) {
                fprintf(stderr, "Error: --target needs COL:VALUE, got '%s'\n", spec);
                return 2;
            }
            size_t len = (size_t)(colon - spec);
            if (len == 0 || len >= sizeof o->name) {
                fprintf(stderr, "Error: bad --target column in '%s'\n", spec);
                return 2;
            }
            memcpy(o->name, spec, len);
            o->name[len] = '\0';
            char *end;
            o->target = strtod(colon + 1, &end);
            if (end == colon + 1 || *end != '\0' || !isfinite(o->target)) {
                fprintf(stderr, "Error: --target value in '%s' is not a number\n", spec);
                return 2;
            }
        } else {
            if (strlen(spec) >= sizeof o->name) {
                fprintf(stderr, "Error: column name too long\n");
                return 2;
            }
            snprintf(o->name, sizeof o->name, "%s", spec);
        }
        nobj++;
    }

    if (nobj == 0) {
        fprintf(stderr, "Error: name at least one objective (--max/--min/--target)\n");
        usage(argv[0]);
        return 2;
    }
    if (!path) { fprintf(stderr, "Error: no results file\n"); usage(argv[0]); return 2; }

    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
    if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", path); return 1; }

    /* Read the whole file: bounds come from the observed range, so nothing can
     * be scored until every row has been seen. */
    char (*lines)[MAXLINE] = malloc(sizeof *lines * MAX_ROWS);
    if (!lines) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    size_t nrows = 0;
    char header[MAXLINE] = {0};
    int ncols = 0;

    char line[MAXLINE];
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (len && line[len-1] != '\n' && !feof(f)) {
            fprintf(stderr, "Error: line %zu exceeds %d bytes\n", nrows + 1, MAXLINE);
            free(lines); return 1;
        }
        while (len && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        if (!header[0]) {
            snprintf(header, sizeof header, "%s", line);
            char work[MAXLINE], *fields[MAX_COLS];
            memcpy(work, line, len + 1);
            ncols = split(work, fields, MAX_COLS);
            for (int c = 0; c < ncols; c++) {
                char *name = trim(fields[c]);
                for (int o = 0; o < nobj; o++)
                    if (strcmp(name, objs[o].name) == 0) objs[o].col = c;
            }
            for (int o = 0; o < nobj; o++) {
                if (objs[o].col < 0) {
                    fprintf(stderr, "Error: column '%s' not found in %s\n", objs[o].name, path);
                    free(lines); return 1;
                }
            }
            continue;
        }
        if (nrows == MAX_ROWS) {
            fprintf(stderr, "Error: more than %d data rows\n", MAX_ROWS);
            free(lines); return 1;
        }
        snprintf(lines[nrows++], MAXLINE, "%s", line);
    }
    if (f != stdin) fclose(f);

    if (!header[0]) { fprintf(stderr, "Error: %s has no header row\n", path); free(lines); return 1; }
    if (nrows == 0) { fprintf(stderr, "Error: %s has no data rows\n", path); free(lines); return 1; }

    /* Observed range per objective. */
    for (int o = 0; o < nobj; o++) { objs[o].lo = INFINITY; objs[o].hi = -INFINITY; }
    for (size_t r = 0; r < nrows; r++) {
        char work[MAXLINE], *fields[MAX_COLS];
        snprintf(work, sizeof work, "%s", lines[r]);
        int n = split(work, fields, MAX_COLS);
        for (int o = 0; o < nobj; o++) {
            if (objs[o].col >= n) continue;
            char *end;
            double v = strtod(trim(fields[objs[o].col]), &end);
            if (end == fields[objs[o].col] || !isfinite(v)) continue;
            if (v < objs[o].lo) objs[o].lo = v;
            if (v > objs[o].hi) objs[o].hi = v;
        }

    }

    printf("%s,desirability\n", header);
    for (size_t r = 0; r < nrows; r++) {
        char work[MAXLINE], *fields[MAX_COLS];
        snprintf(work, sizeof work, "%s", lines[r]);
        int n = split(work, fields, MAX_COLS);

        double product = 1.0;
        int usable = 1;
        for (int o = 0; o < nobj; o++) {
            if (objs[o].col >= n) { usable = 0; break; }
            char *end;
            double v = strtod(trim(fields[objs[o].col]), &end);
            if (end == fields[objs[o].col] || !isfinite(v)) { usable = 0; break; }
            product *= desirability(&objs[o], v);
        }
        double D = usable ? pow(product, 1.0 / (double)nobj) : 0.0;
        if (!usable) {
            fprintf(stderr, "Warning: row %zu has a missing or non-numeric objective; "
                            "desirability 0\n", r + 1);
        }
        printf("%s,%.6g\n", lines[r], D);
    }

    free(lines);
    return 0;
}
