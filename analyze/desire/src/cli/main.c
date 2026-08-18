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
/* No MAX_ROWS, MAX_COLS or MAXLINE any more: those were this file's own
 * ceilings, and it refused a results file past 100000 rows outright. doe_table
 * grows to fit. */

typedef enum { OBJ_MAX, OBJ_MIN, OBJ_TARGET } obj_kind_t;

typedef struct {
    char       name[128];
    obj_kind_t kind;
    double     target;       /* OBJ_TARGET only */
    int        col;          /* resolved from the header */
    double     lo, hi;       /* observed range, the default bounds */
} objective_t;


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

    /*
     * Read the whole table: bounds come from the observed range, so nothing
     * can be scored until every row has been seen.
     *
     * This used to be a private parser with a fixed ceiling -- 100000 rows,
     * 256 columns -- refusing anything larger outright. That is the same class
     * of defect as the 1024-row cap that made `uq` fail on real Monte-Carlo
     * output: a limit invented by the reader rather than the data. doe_table
     * grows to fit, and hands back each row verbatim so the echo below is
     * exactly what arrived.
     */
    char terr[DOE_ERR_SIZE];
    doe_table_t tbl;
    if (doe_table_read(path, &tbl, terr) != 0) {
        fprintf(stderr, "Error: %s\n", terr);
        return 1;
    }

    for (int o = 0; o < nobj; o++) {
        long c = doe_table_col(&tbl, objs[o].name);
        if (c < 0) {
            fprintf(stderr, "Error: column '%s' not found in %s\n", objs[o].name, path);
            doe_table_free(&tbl);
            return 1;
        }
        objs[o].col = (int)c;
    }

    /* Observed range per objective. */
    for (int o = 0; o < nobj; o++) { objs[o].lo = INFINITY; objs[o].hi = -INFINITY; }
    for (size_t r = 0; r < tbl.nrows; r++) {
        for (int o = 0; o < nobj; o++) {
            double v;
            if (doe_table_number(&tbl, r, (size_t)objs[o].col, &v) != 0) continue;
            if (v < objs[o].lo) objs[o].lo = v;
            if (v > objs[o].hi) objs[o].hi = v;
        }
    }

    /* The header back with one column appended, then every row the same way. */
    {
        printf("%s", tbl.names[0]);
        for (size_t c = 1; c < tbl.ncols; c++) printf(",%s", tbl.names[c]);
        printf(",desirability\n");
    }
    for (size_t r = 0; r < tbl.nrows; r++) {
        double product = 1.0;
        int usable = 1;
        for (int o = 0; o < nobj; o++) {
            double v;
            if (doe_table_number(&tbl, r, (size_t)objs[o].col, &v) != 0) { usable = 0; break; }
            product *= desirability(&objs[o], v);
        }
        double D = usable ? pow(product, 1.0 / (double)nobj) : 0.0;
        if (!usable) {
            fprintf(stderr, "Warning: row %zu has a missing or non-numeric objective; "
                            "desirability 0\n", r + 1);
        }
        printf("%s,%.6g\n", doe_table_row(&tbl, r), D);
    }

    doe_table_free(&tbl);
    return 0;
}
