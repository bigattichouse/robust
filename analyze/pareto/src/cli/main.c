/*
 * pareto CLI — filter | init | merge | list | why
 *
 * Filters read and write the results-CSV dialect, so they compose in pipes.
 * See spec/pareto.bp.
 */

/* strtok_r — POSIX, not C99. Same convention as common/src/runner.c. */
#define _POSIX_C_SOURCE 200809L

#include "pareto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> [ARGS] --max NAME | --min NAME ...\n"
        "\n"
        "Commands:\n"
        "  filter <results.csv|->            Non-dominated rows, same CSV dialect out\n"
        "  init                              Write an empty .front to stdout\n"
        "  merge  <study.front> <batch.csv>  Fold a batch into a frontier, in place\n"
        "  list   <study.front>              Emit the front's rows without the preamble\n"
        "  why    <study.front> <results.csv> --run N\n"
        "                                    Explain why a run is or is not on the front\n"
        "  --version\n"
        "\n"
        "Objectives:\n"
        "  --max NAME     larger is better        --min NAME     smaller is better\n"
        "  At least two are required; one objective is a sort, not a frontier.\n"
        "  For `merge`, `list` and `why` the front's own objectives are authoritative.\n"
        "\n"
        "Options:\n"
        "  --columns-from FILE   (init) take the CSV header from FILE\n"
        "  --source LABEL        (merge) provenance label; defaults to the file name\n"
        "  --duplicates          (list)  flag points tied on every objective\n"
        "  --quiet               (merge) suppress the summary on stderr\n",
        prog);
}

/* Collect --max/--min into objs. Returns 0, or -1 (message already printed). */
static int parse_objectives(int argc, char **argv, int from,
                            pareto_objective_t *objs, size_t *nobj) {
    char err[DOE_ERR_SIZE];
    for (int i = from; i < argc; i++) {
        int is_max = strcmp(argv[i], "--max") == 0;
        int is_min = strcmp(argv[i], "--min") == 0;
        if (!is_max && !is_min) continue;
        if (i + 1 >= argc) {
            fprintf(stderr, "Error: %s needs a column name\n", argv[i]);
            return -1;
        }
        if (pareto_add_objective(objs, nobj, argv[i+1],
                                 is_max ? PARETO_MAX : PARETO_MIN, err) != 0) {
            fprintf(stderr, "Error: %s\n", err);
            return -1;
        }
        i++;
    }
    return 0;
}

static const char *opt_value(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], flag) == 0 && i + 1 < argc) return argv[i+1];
    return NULL;
}

static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

/* Does a CSV header line carry a column with this exact (trimmed) name? */
static int header_has_column(const char *header, const char *name) {
    char tmp[PARETO_MAX_LINE];
    snprintf(tmp, sizeof tmp, "%s", header);
    char *save = NULL;
    char *t = strtok_r(tmp, ",", &save);
    while (t) {
        while (*t == ' ' || *t == '\t') t++;
        size_t tl = strlen(t);
        while (tl && (t[tl-1] == ' ' || t[tl-1] == '\t')) t[--tl] = '\0';
        if (strcmp(t, name) == 0) return 1;
        t = strtok_r(NULL, ",", &save);
    }
    return 0;
}

static int need_two(size_t nobj) {
    if (nobj < 2) {
        fprintf(stderr,
            "Error: a frontier needs at least 2 objectives (got %zu).\n"
            "       With one objective the answer is just the best row — use `sort`.\n",
            nobj);
        return -1;
    }
    return 0;
}

static void warn_if_large(size_t n) {
    if (n > PARETO_WARN_ROWS) {
        double cmps = (double)n * (double)n;
        fprintf(stderr,
            "note: %zu rows -> ~%.3g dominance comparisons; this will take a while.\n",
            n, cmps);
    }
}

/* ---- filter ---------------------------------------------------------- */

static int cmd_filter(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }
    const char *path = argv[2];

    pareto_objective_t objs[PARETO_MAX_OBJECTIVES];
    size_t nobj = 0;
    if (parse_objectives(argc, argv, 3, objs, &nobj) != 0) return 2;
    if (need_two(nobj) != 0) return 2;

    char err[DOE_ERR_SIZE];
    pareto_point_t *pts = NULL; size_t n = 0; char *header = NULL;
    if (pareto_read_csv(path, objs, nobj, NULL, &pts, &n, &header, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    warn_if_large(n);

    unsigned char *keep = malloc((n ? n : 1) * sizeof *keep);
    if (!keep) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    pareto_non_dominated(pts, n, objs, nobj, keep);

    printf("%s\n", header);
    for (size_t i = 0; i < n; i++) if (keep[i]) printf("%s\n", pts[i].row);

    free(keep); free(header); pareto_points_free(pts, n);
    return 0;
}

/* ---- init ------------------------------------------------------------ */

static int cmd_init(int argc, char **argv) {
    pareto_objective_t objs[PARETO_MAX_OBJECTIVES];
    size_t nobj = 0;
    if (parse_objectives(argc, argv, 2, objs, &nobj) != 0) return 2;
    if (need_two(nobj) != 0) return 2;

    const char *from = opt_value(argc, argv, "--columns-from");
    char header[PARETO_MAX_LINE];
    header[0] = '\0';

    if (from) {
        FILE *f = fopen(from, "r");
        if (!f) { fprintf(stderr, "Error: cannot open '%s'\n", from); return 1; }
        char line[PARETO_MAX_LINE];
        while (fgets(line, sizeof line, f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len == 0 || line[0] == '#') continue;
            snprintf(header, sizeof header, "%s", line);
            break;
        }
        fclose(f);
        if (!header[0]) {
            fprintf(stderr, "Error: no header row found in '%s'\n", from);
            return 1;
        }
        /* Fail here rather than at the first merge: an objective the header
         * does not carry makes the front unusable, and the user is holding
         * the file right now. */
        for (size_t o = 0; o < nobj; o++) {
            if (!header_has_column(header, objs[o].name)) {
                fprintf(stderr, "Error: unknown objective column '%s' in '%s'\n",
                        objs[o].name, from);
                fprintf(stderr, "       header is: %s\n", header);
                return 1;
            }
        }
    }

    printf("# tgu-front 1\n");
    printf("# objectives:");
    for (size_t o = 0; o < nobj; o++)
        printf("%s %s %s", o ? "," : "", objs[o].name,
               objs[o].sense == PARETO_MAX ? "max" : "min");
    printf("\n");
    if (header[0]) printf("%s\n", header);
    return 0;
}

/* ---- merge ----------------------------------------------------------- */

static int cmd_merge(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 2; }
    const char *front_path = argv[2];
    const char *batch_path = argv[3];

    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    if (pareto_front_load(front_path, &f, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    const char *label = opt_value(argc, argv, "--source");
    char lbl[PARETO_MAX_SOURCE];
    if (label) snprintf(lbl, sizeof lbl, "%s", label);
    else {
        const char *b = strrchr(batch_path, '/');
        snprintf(lbl, sizeof lbl, "%s", b ? b + 1 : batch_path);
    }

    pareto_point_t *pts = NULL; size_t n = 0; char *header = NULL;
    if (pareto_read_csv(batch_path, f.objectives, f.objective_count, lbl,
                        &pts, &n, &header, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        pareto_front_free(&f);
        return 1;
    }

    /* An empty front adopts the batch's header; a populated one must match. */
    if (!f.columns) {
        f.columns = header; header = NULL;
    } else if (strcmp(f.columns, header) != 0) {
        fprintf(stderr,
            "Error: column header mismatch\n"
            "       front: %s\n"
            "       %s: %s\n", f.columns, batch_path, header);
        free(header); pareto_points_free(pts, n); pareto_front_free(&f);
        return 1;
    }
    free(header);

    warn_if_large(f.point_count + n);

    pareto_merge_t rec;
    if (pareto_front_merge(&f, pts, n, lbl, &rec, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        pareto_points_free(pts, n); pareto_front_free(&f);
        return 1;
    }
    pareto_points_free(pts, n);

    if (pareto_front_save(front_path, &f, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        pareto_front_free(&f);
        return 1;
    }

    if (!has_flag(argc, argv, "--quiet")) {
        fprintf(stderr, "%s: %zu rows -> %zu admitted, %zu evicted, %zu dominated",
                lbl, rec.rows_in, rec.admitted, rec.evicted, rec.rejected);
        if (rec.duplicate)
            fprintf(stderr, ", %zu already present", rec.duplicate);
        fprintf(stderr, "\nfront: %zu points\n", f.point_count);
    }
    pareto_front_free(&f);
    return 0;
}

/* ---- list ------------------------------------------------------------ */

static int cmd_list(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }
    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    if (pareto_front_load(argv[2], &f, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    if (f.columns) printf("%s\n", f.columns);
    for (size_t i = 0; i < f.point_count; i++) printf("%s\n", f.points[i].row);

    if (has_flag(argc, argv, "--duplicates")) {
        int any = 0;
        for (size_t i = 0; i < f.point_count; i++)
            for (size_t j = i + 1; j < f.point_count; j++) {
                int same = 1;
                for (size_t o = 0; o < f.objective_count; o++)
                    if (f.points[i].values[o] != f.points[j].values[o]) { same = 0; break; }
                if (same) {
                    if (!any) { fprintf(stderr, "tied on every objective:\n"); any = 1; }
                    fprintf(stderr, "  run %llu and run %llu\n",
                            f.points[i].run_id, f.points[j].run_id);
                }
            }
    }
    pareto_front_free(&f);
    return 0;
}

/* ---- why ------------------------------------------------------------- */

static int cmd_why(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 2; }
    const char *run_s = opt_value(argc, argv, "--run");
    if (!run_s) { fprintf(stderr, "Error: why needs --run N\n"); return 2; }
    unsigned long long want = strtoull(run_s, NULL, 10);

    char err[DOE_ERR_SIZE];
    pareto_front_t f;
    if (pareto_front_load(argv[2], &f, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    pareto_point_t *pts = NULL; size_t n = 0; char *header = NULL;
    if (pareto_read_csv(argv[3], f.objectives, f.objective_count, NULL,
                        &pts, &n, &header, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        pareto_front_free(&f);
        return 1;
    }
    free(header);

    const pareto_point_t *target = NULL;
    for (size_t i = 0; i < n; i++) if (pts[i].run_id == want) { target = &pts[i]; break; }
    if (!target) {
        fprintf(stderr, "Error: run %llu not found in %s\n", want, argv[3]);
        pareto_points_free(pts, n); pareto_front_free(&f);
        return 1;
    }

    printf("run %llu (", want);
    for (size_t o = 0; o < f.objective_count; o++)
        printf("%s%s=%g", o ? ", " : "", f.objectives[o].name, target->values[o]);
    printf(")\n");

    size_t ndom = 0;
    const pareto_point_t *first = NULL;
    for (size_t i = 0; i < f.point_count; i++)
        if (pareto_dominates(&f.points[i], target, f.objectives, f.objective_count)) {
            if (!first) first = &f.points[i];
            ndom++;
        }

    if (ndom == 0) {
        printf("  non-dominated: no point on the front beats it on every objective.\n");
        for (size_t o = 0; o < f.objective_count; o++) {
            const pareto_point_t *best = NULL;
            for (size_t i = 0; i < f.point_count; i++)
                if (!best || pareto_better(f.points[i].values[o], best->values[o],
                                           f.objectives[o].sense))
                    best = &f.points[i];
            if (best)
                printf("    %-16s best on front: %g (run %llu)\n",
                       f.objectives[o].name, best->values[o], best->run_id);
        }
    } else {
        printf("  dominated by run %llu:\n", first->run_id);
        for (size_t o = 0; o < f.objective_count; o++) {
            double a = target->values[o], b = first->values[o];
            printf("    %-16s %g -> %g   %s by %g\n",
                   f.objectives[o].name, a, b,
                   pareto_better(b, a, f.objectives[o].sense) ? "better" : "equal/worse",
                   fabs(b - a));
        }
        if (ndom > 1)
            printf("  %zu other point%s on the front also dominate%s it.\n",
                   ndom - 1, ndom - 1 == 1 ? "" : "s", ndom - 1 == 1 ? "s" : "");
    }

    pareto_points_free(pts, n);
    pareto_front_free(&f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "--version") == 0) {
        printf("pareto %d.%d.%d\n", DOE_VERSION_MAJOR, DOE_VERSION_MINOR, DOE_VERSION_PATCH);
        return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) { usage(argv[0]); return 0; }
    if (strcmp(cmd, "filter") == 0) return cmd_filter(argc, argv);
    if (strcmp(cmd, "init")   == 0) return cmd_init(argc, argv);
    if (strcmp(cmd, "merge")  == 0) return cmd_merge(argc, argv);
    if (strcmp(cmd, "list")   == 0) return cmd_list(argc, argv);
    if (strcmp(cmd, "why")    == 0) return cmd_why(argc, argv);

    fprintf(stderr, "Error: unknown command '%s'\n", cmd);
    usage(argv[0]);
    return 2;
}
