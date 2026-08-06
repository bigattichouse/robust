/*
 * pareto.c — dominance core, results-CSV reader, and the .front store.
 * See ../../include/pareto.h and spec/pareto.bp.
 */

/* strtok_r, gmtime_r, getpid — POSIX, not C99. Same convention as
 * common/src/runner.c. Must precede every include. */
#define _POSIX_C_SOURCE 200809L

#include "pareto.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/* ===================================================================
 * small helpers
 * =================================================================== */

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = '\0';
    return s;
}

/*
 * Append to a DOE_ERR_SIZE error buffer, returning the new offset, never
 * running past the end.
 *
 * The obvious `off += snprintf(err + off, DOE_ERR_SIZE - off, ...)` is WRONG
 * and was a live stack-buffer-overflow here until 2026-08-06: snprintf returns
 * the length it *would* have written, so one long field pushes `off` past the
 * buffer, after which `err + off` is out of bounds and `DOE_ERR_SIZE - off` is
 * negative — converted to a huge size_t. Found by tools/pareto/tests/
 * fuzz_pareto.c on its first run. Use this helper for any multi-part message.
 */
static size_t err_cat(char *err, size_t off, const char *fmt, ...) {
    if (off >= DOE_ERR_SIZE - 1) return DOE_ERR_SIZE - 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(err + off, DOE_ERR_SIZE - off, fmt, ap);
    va_end(ap);
    if (n < 0) return off;
    off += (size_t)n;
    if (off >= DOE_ERR_SIZE) off = DOE_ERR_SIZE - 1;   /* truncated */
    return off;
}

/* Split a line in place on commas. Returns field count (capped at max). */
static int csv_split(char *line, char **fields, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    return n;
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void iso_now(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm g;
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

/* Basename without directory, for the default source label. */
static void label_from_path(const char *path, char *out, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!*base || strcmp(base, "-") == 0) base = "stdin";
    snprintf(out, n, "%s", base);
}

/* ===================================================================
 * dominance core
 * =================================================================== */

int pareto_better(double x, double y, pareto_sense_t sense) {
    return sense == PARETO_MAX ? (x > y) : (x < y);
}

int pareto_dominates(const pareto_point_t *a, const pareto_point_t *b,
                     const pareto_objective_t *objs, size_t nobj) {
    int strictly = 0;
    for (size_t i = 0; i < nobj; i++) {
        /* b better than a on any objective -> a does not dominate b */
        if (pareto_better(b->values[i], a->values[i], objs[i].sense)) return 0;
        if (pareto_better(a->values[i], b->values[i], objs[i].sense)) strictly = 1;
    }
    return strictly;
}

void pareto_non_dominated(const pareto_point_t *pts, size_t n,
                          const pareto_objective_t *objs, size_t nobj,
                          unsigned char *keep) {
    for (size_t i = 0; i < n; i++) keep[i] = 1;
    for (size_t i = 0; i < n; i++) {
        if (!keep[i]) continue;
        for (size_t j = 0; j < n; j++) {
            if (i == j) continue;
            if (pareto_dominates(&pts[j], &pts[i], objs, nobj)) { keep[i] = 0; break; }
        }
    }
}

/* ===================================================================
 * objectives
 * =================================================================== */

int pareto_add_objective(pareto_objective_t *objs, size_t *nobj,
                         const char *name, pareto_sense_t sense, char *err) {
    if (*nobj >= PARETO_MAX_OBJECTIVES) {
        snprintf(err, DOE_ERR_SIZE, "too many objectives (limit %d)",
                 PARETO_MAX_OBJECTIVES);
        return -1;
    }
    if (strlen(name) == 0) {
        snprintf(err, DOE_ERR_SIZE, "empty objective name");
        return -1;
    }
    if (strlen(name) >= DOE_MAX_NAME) {
        snprintf(err, DOE_ERR_SIZE, "objective name too long: '%.32s...'", name);
        return -1;
    }
    for (size_t i = 0; i < *nobj; i++) {
        if (strcmp(objs[i].name, name) == 0) {
            snprintf(err, DOE_ERR_SIZE, "objective '%s' named twice", name);
            return -1;
        }
    }
    snprintf(objs[*nobj].name, DOE_MAX_NAME, "%s", name);
    objs[*nobj].sense = sense;
    objs[*nobj].column = -1;
    (*nobj)++;
    return 0;
}

/* ===================================================================
 * results CSV
 * =================================================================== */

static int resolve_columns(char *header, pareto_objective_t *objs, size_t nobj,
                           char *err) {
    char tmp[PARETO_MAX_LINE];
    snprintf(tmp, sizeof tmp, "%s", header);
    char *hf[512];
    int nh = csv_split(tmp, hf, 512);
    for (int i = 0; i < nh; i++) hf[i] = trim(hf[i]);

    for (size_t o = 0; o < nobj; o++) {
        objs[o].column = -1;
        for (int i = 0; i < nh; i++) {
            if (strcmp(hf[i], objs[o].name) == 0) { objs[o].column = i; break; }
        }
        if (objs[o].column < 0) {
            size_t off = 0;
            off = err_cat(err, off, "unknown objective column '%s' (available:",
                          objs[o].name);
            /* Header fields are attacker-controlled and can be as long as the
             * whole line, so every append must clamp. */
            for (int i = 0; i < nh && off + 2 < DOE_ERR_SIZE; i++)
                off = err_cat(err, off, " %s", hf[i]);
            err_cat(err, off, ")");
            return -1;
        }
    }
    return 0;
}

static int parse_double(const char *s, double *out) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || errno == ERANGE) return -1;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return -1;
    if (!isfinite(v)) return -1;
    *out = v;
    return 0;
}

int pareto_read_csv(const char *path, pareto_objective_t *objs, size_t nobj,
                    const char *source_label,
                    pareto_point_t **out, size_t *count, char **header_out,
                    char *err) {
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
    if (!f) {
        snprintf(err, DOE_ERR_SIZE, "cannot open results '%s'", path);
        return -1;
    }

    char label[PARETO_MAX_SOURCE];
    if (source_label) snprintf(label, sizeof label, "%s", source_label);
    else label_from_path(path, label, sizeof label);

    char line[PARETO_MAX_LINE];
    char *header = NULL;
    pareto_point_t *pts = NULL;
    size_t n = 0, cap = 0;
    int line_no = 0, rc = 0;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len-1] != '\n') {
            snprintf(err, DOE_ERR_SIZE, "line %d exceeds maximum length (%d)",
                     line_no, PARETO_MAX_LINE - 2);
            rc = -1; break;
        }
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        if (!header) {
            header = dup_str(line);
            if (!header) { snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }
            if (resolve_columns(header, objs, nobj, err) != 0) { rc = -1; break; }
            continue;
        }

        if (n >= PARETO_MAX_ROWS) {
            snprintf(err, DOE_ERR_SIZE, "too many rows (limit %u)", PARETO_MAX_ROWS);
            rc = -1; break;
        }
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 256;
            pareto_point_t *tmp = realloc(pts, ncap * sizeof *tmp);
            if (!tmp) { snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }
            pts = tmp; cap = ncap;
        }

        char work[PARETO_MAX_LINE];
        memcpy(work, line, len + 1);
        char *ff[512];
        int nf = csv_split(work, ff, 512);
        for (int i = 0; i < nf; i++) ff[i] = trim(ff[i]);

        pareto_point_t *p = &pts[n];
        memset(p, 0, sizeof *p);

        int bad = 0;
        for (size_t o = 0; o < nobj; o++) {
            int c = objs[o].column;
            if (c >= nf || *ff[c] == '\0') {
                snprintf(err, DOE_ERR_SIZE,
                         "line %d: missing value for objective '%s'", line_no, objs[o].name);
                bad = 1; break;
            }
            if (parse_double(ff[c], &p->values[o]) != 0) {
                snprintf(err, DOE_ERR_SIZE,
                         "line %d: non-numeric or non-finite value '%s' in column '%s'",
                         line_no, ff[c], objs[o].name);
                bad = 1; break;
            }
        }
        if (bad) { rc = -1; break; }

        p->run_id = (nf > 0) ? strtoull(ff[0], NULL, 10) : 0;
        snprintf(p->source, sizeof p->source, "%s", label);
        p->row = dup_str(line);
        if (!p->row) { snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }
        n++;
    }

    if (f != stdin) fclose(f);

    if (rc == 0 && !header) {
        snprintf(err, DOE_ERR_SIZE,
                 "results file has no header row (needed to resolve objective names)");
        rc = -1;
    }

    if (rc != 0) {
        pareto_points_free(pts, n);
        free(header);
        return -1;
    }

    *out = pts; *count = n;
    if (header_out) *header_out = header; else free(header);
    return 0;
}

void pareto_points_free(pareto_point_t *pts, size_t n) {
    if (!pts) return;
    for (size_t i = 0; i < n; i++) free(pts[i].row);
    free(pts);
}

/* ===================================================================
 * .front store
 * =================================================================== */

void pareto_front_init(pareto_front_t *f) { memset(f, 0, sizeof *f); }

void pareto_front_free(pareto_front_t *f) {
    if (!f) return;
    pareto_points_free(f->points, f->point_count);
    free(f->columns);
    free(f->history);
    memset(f, 0, sizeof *f);
}

static int front_push_point(pareto_front_t *f, const pareto_point_t *p, char *err) {
    if (f->point_count == f->point_cap) {
        size_t ncap = f->point_cap ? f->point_cap * 2 : 64;
        pareto_point_t *tmp = realloc(f->points, ncap * sizeof *tmp);
        if (!tmp) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }
        f->points = tmp; f->point_cap = ncap;
    }
    pareto_point_t *dst = &f->points[f->point_count];
    *dst = *p;
    dst->row = dup_str(p->row);
    if (!dst->row) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }
    f->point_count++;
    return 0;
}

static int front_push_merge(pareto_front_t *f, const pareto_merge_t *m, char *err) {
    if (f->history_count == f->history_cap) {
        size_t ncap = f->history_cap ? f->history_cap * 2 : 8;
        pareto_merge_t *tmp = realloc(f->history, ncap * sizeof *tmp);
        if (!tmp) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }
        f->history = tmp; f->history_cap = ncap;
    }
    f->history[f->history_count++] = *m;
    return 0;
}

/* Sort: by first objective in its improving direction, then run_id. Makes a
 * no-op merge byte-identical and `git diff` on a .front meaningful. */
static const pareto_objective_t *sort_objs;
static int cmp_points(const void *va, const void *vb) {
    const pareto_point_t *a = va, *b = vb;
    if (pareto_better(a->values[0], b->values[0], sort_objs[0].sense)) return -1;
    if (pareto_better(b->values[0], a->values[0], sort_objs[0].sense)) return 1;
    if (a->run_id < b->run_id) return -1;
    if (a->run_id > b->run_id) return 1;
    return strcmp(a->row, b->row);
}

int pareto_front_load(const char *path, pareto_front_t *f, char *err) {
    FILE *fp = fopen(path, "r");
    if (!fp) { snprintf(err, DOE_ERR_SIZE, "cannot open front '%s'", path); return -1; }

    pareto_front_init(f);

    char line[PARETO_MAX_LINE];
    int line_no = 0, rc = 0, saw_magic = 0;
    int *cols = NULL;

    while (fgets(line, sizeof line, fp)) {
        line_no++;
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len-1] != '\n') {
            snprintf(err, DOE_ERR_SIZE, "line %d exceeds maximum length", line_no);
            rc = -1; break;
        }
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        if (line[0] == '#') {
            char *p = trim(line + 1);
            if (strncmp(p, "tgu-front", 9) == 0) { saw_magic = 1; continue; }
            if (strncmp(p, "objectives:", 11) == 0) {
                char *list = p + 11;
                char *save = NULL;
                for (char *tok = strtok_r(list, ",", &save); tok;
                     tok = strtok_r(NULL, ",", &save)) {
                    char *t = trim(tok);
                    char *sp = strrchr(t, ' ');
                    if (!sp) {
                        snprintf(err, DOE_ERR_SIZE,
                                 "malformed objectives line: '%s'", t);
                        rc = -1; break;
                    }
                    *sp = '\0';
                    char *nm = trim(t), *sense = trim(sp + 1);
                    pareto_sense_t s;
                    if (strcmp(sense, "max") == 0) s = PARETO_MAX;
                    else if (strcmp(sense, "min") == 0) s = PARETO_MIN;
                    else {
                        snprintf(err, DOE_ERR_SIZE,
                                 "objective '%s': sense must be max or min (got '%s')", nm, sense);
                        rc = -1; break;
                    }
                    if (pareto_add_objective(f->objectives, &f->objective_count,
                                             nm, s, err) != 0) { rc = -1; break; }
                }
                if (rc != 0) break;
                continue;
            }
            if (strncmp(p, "merge:", 6) == 0) {
                pareto_merge_t m;
                memset(&m, 0, sizeof m);
                if (sscanf(p + 6,
                           "%63s %31s in=%zu admitted=%zu evicted=%zu rejected=%zu dup=%zu",
                           m.source, m.when, &m.rows_in, &m.admitted,
                           &m.evicted, &m.rejected, &m.duplicate) >= 6) {
                    if (front_push_merge(f, &m, err) != 0) { rc = -1; break; }
                }
                continue;
            }
            continue;   /* unknown comment: ignore, forward-compatible */
        }

        if (!f->columns) {
            if (!saw_magic) {
                snprintf(err, DOE_ERR_SIZE,
                         "'%s' is not a .front file (missing '# tgu-front' header)", path);
                rc = -1; break;
            }
            if (f->objective_count < 2) {
                snprintf(err, DOE_ERR_SIZE,
                         "front declares %zu objective(s); at least 2 are required",
                         f->objective_count);
                rc = -1; break;
            }
            f->columns = dup_str(line);
            if (!f->columns) { snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }
            if (resolve_columns(f->columns, f->objectives, f->objective_count, err) != 0) {
                rc = -1; break;
            }
            cols = malloc(f->objective_count * sizeof *cols);
            if (!cols) { snprintf(err, DOE_ERR_SIZE, "out of memory"); rc = -1; break; }
            for (size_t o = 0; o < f->objective_count; o++) cols[o] = f->objectives[o].column;
            continue;
        }

        char work[PARETO_MAX_LINE];
        memcpy(work, line, len + 1);
        char *ff[512];
        int nf = csv_split(work, ff, 512);
        for (int i = 0; i < nf; i++) ff[i] = trim(ff[i]);

        pareto_point_t p;
        memset(&p, 0, sizeof p);
        int bad = 0;
        for (size_t o = 0; o < f->objective_count; o++) {
            int c = cols[o];
            if (c >= nf || parse_double(ff[c], &p.values[o]) != 0) {
                snprintf(err, DOE_ERR_SIZE,
                         "front line %d: bad value for objective '%s'",
                         line_no, f->objectives[o].name);
                bad = 1; break;
            }
        }
        if (bad) { rc = -1; break; }
        p.run_id = (nf > 0) ? strtoull(ff[0], NULL, 10) : 0;
        snprintf(p.source, sizeof p.source, "%s", "front");
        p.row = line;                       /* front_push_point copies it */
        if (front_push_point(f, &p, err) != 0) { rc = -1; break; }
    }

    free(cols);
    fclose(fp);

    if (rc == 0 && !saw_magic) {
        snprintf(err, DOE_ERR_SIZE, "'%s' is not a .front file (missing '# tgu-front' header)", path);
        rc = -1;
    }
    if (rc == 0 && f->objective_count < 2) {
        snprintf(err, DOE_ERR_SIZE, "front declares %zu objective(s); at least 2 are required",
                 f->objective_count);
        rc = -1;
    }
    if (rc != 0) { pareto_front_free(f); return -1; }
    return 0;
}

int pareto_front_save(const char *path, const pareto_front_t *f, char *err) {
    char tmp[1024];
    int nw = snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid());
    if (nw < 0 || (size_t)nw >= sizeof tmp) {
        snprintf(err, DOE_ERR_SIZE, "front path too long");
        return -1;
    }

    FILE *fp = fopen(tmp, "w");
    if (!fp) {
        snprintf(err, DOE_ERR_SIZE, "cannot write front: %s", strerror(errno));
        return -1;
    }

    fprintf(fp, "# tgu-front 1\n");
    fprintf(fp, "# objectives:");
    for (size_t o = 0; o < f->objective_count; o++)
        fprintf(fp, "%s %s %s", o ? "," : "", f->objectives[o].name,
                f->objectives[o].sense == PARETO_MAX ? "max" : "min");
    fprintf(fp, "\n");
    for (size_t i = 0; i < f->history_count; i++) {
        const pareto_merge_t *m = &f->history[i];
        fprintf(fp, "# merge: %s %s in=%zu admitted=%zu evicted=%zu rejected=%zu dup=%zu\n",
                m->source, m->when, m->rows_in, m->admitted, m->evicted,
                m->rejected, m->duplicate);
    }
    if (f->columns) fprintf(fp, "%s\n", f->columns);
    for (size_t i = 0; i < f->point_count; i++)
        fprintf(fp, "%s\n", f->points[i].row);

    if (fflush(fp) != 0 || ferror(fp)) {
        fclose(fp); remove(tmp);
        snprintf(err, DOE_ERR_SIZE, "cannot write front: %s", strerror(errno));
        return -1;
    }
    if (fclose(fp) != 0) {
        remove(tmp);
        snprintf(err, DOE_ERR_SIZE, "cannot write front: %s", strerror(errno));
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        snprintf(err, DOE_ERR_SIZE, "cannot replace front: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int pareto_front_merge(pareto_front_t *f, pareto_point_t *pts, size_t n,
                       const char *source_label, pareto_merge_t *rec, char *err) {
    size_t incumbent = f->point_count;
    size_t total = incumbent + n;
    if (total > PARETO_MAX_ROWS) {
        snprintf(err, DOE_ERR_SIZE, "front + batch is %zu rows (limit %u)",
                 total, PARETO_MAX_ROWS);
        return -1;
    }

    memset(rec, 0, sizeof *rec);
    snprintf(rec->source, sizeof rec->source, "%s", source_label);
    iso_now(rec->when, sizeof rec->when);
    rec->rows_in = n;

    /*
     * Drop arrivals that are already on the front: same run_id AND the same
     * value on every objective. Re-merging a batch must be a no-op, and
     * without this it would not be -- identical points do not dominate each
     * other, so both copies would survive and the front would grow on every
     * repeat.
     *
     * This is NOT the tie rule. A tie is a DIFFERENT run whose objectives
     * happen to be equal; both of those are kept deliberately, because they
     * are distinct operating points the objectives do not separate.
     */
    unsigned char *isdup = calloc(n ? n : 1, 1);
    if (!isdup) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return -1; }
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < incumbent; j++) {
            if (pts[i].run_id != f->points[j].run_id) continue;
            int same = 1;
            for (size_t o = 0; o < f->objective_count; o++)
                if (pts[i].values[o] != f->points[j].values[o]) { same = 0; break; }
            if (same) { isdup[i] = 1; rec->duplicate++; break; }
        }
    }

    size_t narr = n - rec->duplicate;
    total = incumbent + narr;

    /* Union: incumbents first, then the surviving arrivals, so an index below
     * `incumbent` identifies an incumbent when classifying the outcome. */
    pareto_point_t *all = malloc((total ? total : 1) * sizeof *all);
    unsigned char *keep = malloc((total ? total : 1) * sizeof *keep);
    if (!all || !keep) {
        free(all); free(keep); free(isdup);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < incumbent; i++) all[i] = f->points[i];
    size_t w = incumbent;
    for (size_t i = 0; i < n; i++) if (!isdup[i]) all[w++] = pts[i];

    pareto_non_dominated(all, total, f->objectives, f->objective_count, keep);

    for (size_t i = 0; i < incumbent; i++) if (!keep[i]) rec->evicted++;
    for (size_t i = incumbent; i < total; i++) {
        if (keep[i]) rec->admitted++;
        else         rec->rejected++;
    }
    free(isdup);

    /* Rebuild the point array from the survivors. Rows are re-duplicated, so
     * free the old owners afterwards and never alias. */
    pareto_point_t *kept = NULL;
    size_t nkept = 0, cap = 0;
    for (size_t i = 0; i < total; i++) {
        if (!keep[i]) continue;
        if (nkept == cap) {
            size_t ncap = cap ? cap * 2 : 64;
            pareto_point_t *t = realloc(kept, ncap * sizeof *t);
            if (!t) {
                pareto_points_free(kept, nkept);
                free(all); free(keep);
                snprintf(err, DOE_ERR_SIZE, "out of memory");
                return -1;
            }
            kept = t; cap = ncap;
        }
        kept[nkept] = all[i];
        kept[nkept].row = dup_str(all[i].row);
        if (!kept[nkept].row) {
            pareto_points_free(kept, nkept);
            free(all); free(keep);
            snprintf(err, DOE_ERR_SIZE, "out of memory");
            return -1;
        }
        nkept++;
    }

    pareto_points_free(f->points, f->point_count);
    f->points = kept;
    f->point_count = nkept;
    f->point_cap = cap;

    sort_objs = f->objectives;
    if (nkept > 1) qsort(f->points, nkept, sizeof *f->points, cmp_points);

    free(all); free(keep);

    if (front_push_merge(f, rec, err) != 0) return -1;
    return 0;
}
