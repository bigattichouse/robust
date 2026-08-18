/*
 * csv.c — results CSV reader. Ported from taguchi's multi-column parser.
 *
 * Reads a results file with an optional header row. When a header is present,
 * the column named `metric` carries the response; without a header, column 1
 * is used (and `metric` must be "response"). Responses are keyed by run_id
 * (column 0, 1-based) and written into responses[run_id - 1]. Blank lines,
 * '#' comment lines, and rows with an empty metric cell are skipped.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    return s;
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

/*
 * The highest run_id in a results file.
 *
 * `max_rows` in doe_csv_read_metric is a DESIGN's run count for every caller
 * that has a design -- morris, sobol and rsm all pass the size of the array
 * they generated, and a run_id past it means the file does not belong to the
 * design, which is worth refusing.
 *
 * `uq` has no design. It summarises whatever responses it is handed, so it
 * used the parameter as a capacity hint: allocate 1024, read, and if the
 * buffer filled exactly, double and retry. Against a longer file the reader
 * saw run_id 1025 > 1024 and reported it as a data error -- so the growth loop
 * could never run and `uq` was silently capped at 1024 rows, failing outright
 * on every larger Monte-Carlo or Sobol results file.
 *
 * Sizing up front is the fix, and it is the honest one: the capacity and the
 * validation stop sharing a parameter that can only mean one of them. Reports
 * the greatest run_id rather than the row count, because that is what indexes
 * the buffer -- a file holding runs 1, 5 and 900 needs 900 slots for three
 * values.
 *
 * Returns 0 on success, -1 on error (err filled).
 */
int doe_csv_max_run_id(const char *path, size_t *max_out, char *err) {
    if (!path || !max_out) {
        if (err) snprintf(err, DOE_ERR_SIZE, "null input to doe_csv_max_run_id");
        return -1;
    }
    /* Deliberately not stdin: this exists to be a FIRST pass over a file the
     * caller reads again, and a pipe cannot be re-read. */
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, DOE_ERR_SIZE, "cannot open results '%s'", path);
        return -1;
    }

    char line[8192];
    char *fields[512];
    int header_seen = 0, line_no = 0;
    size_t rows = 0, max_id = 0;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            snprintf(err, DOE_ERR_SIZE, "line %d exceeds maximum length (%zu)",
                     line_no, sizeof line - 2);
            fclose(f);
            return -1;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;

        int nf = csv_split(line, fields, 512);
        char *endp;
        long run_id = strtol(trim(fields[0]), &endp, 10);

        if (!header_seen) {
            header_seen = 1;
            /* Same rule the reader uses: a non-numeric first field is a
             * header. Anything else is already a data row. */
            if (*endp != '\0') continue;
        }
        if (nf < 1 || *endp != '\0' || run_id < 1) {
            snprintf(err, DOE_ERR_SIZE, "line %d: invalid run_id", line_no);
            fclose(f);
            return -1;
        }
        if ((size_t)run_id > max_id) max_id = (size_t)run_id;
        rows++;
    }

    fclose(f);
    if (rows == 0) {
        snprintf(err, DOE_ERR_SIZE, "no data rows in '%s'", path);
        return -1;
    }
    *max_out = max_id;
    return 0;
}

int doe_csv_read_metric(const char *path, const char *metric,
                        double *responses, size_t max_rows,
                        size_t *count_out, char *err) {
    if (!path || !responses || !count_out) {
        if (err) snprintf(err, DOE_ERR_SIZE, "null input to doe_csv_read_metric");
        return -1;
    }
    /* "-" is stdin, so a results CSV can arrive down a pipe. This reader only
     * ever fgets, never seeks, so it costs one branch -- and it makes the
     * documented composition (`desire ... | sobol analyze model.space -`)
     * actually work, which it did not. */
    int from_stdin = (path[0] == '-' && path[1] == '\0');
    FILE *f = from_stdin ? stdin : fopen(path, "r");
    if (!f) {
        snprintf(err, DOE_ERR_SIZE, "cannot open results '%s'", path);
        return -1;
    }

    char line[8192];
    char *fields[512];
    int metric_col = -1;
    int header_seen = 0;
    size_t count = 0;
    int line_no = 0;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        size_t len = strlen(line);
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            snprintf(err, DOE_ERR_SIZE, "line %d exceeds maximum length (%zu)",
                     line_no, sizeof line - 2);
            if (!from_stdin) fclose(f);
            return -1;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;

        if (!header_seen) {
            header_seen = 1;

            char tmp[8192];
            memcpy(tmp, line, len + 1);
            char *hf[512];
            int nh = csv_split(tmp, hf, 512);

            char *endp;
            strtol(trim(hf[0]), &endp, 10);
            int is_header = (*endp != '\0');   /* non-numeric first field => header */

            if (is_header) {
                for (int c = 0; c < nh; c++) {
                    if (strcmp(trim(hf[c]), metric) == 0) { metric_col = c; break; }
                }
                if (metric_col == -1) {
                    if (strcmp(metric, "response") == 0) {
                        metric_col = 1;
                    } else {
                        snprintf(err, DOE_ERR_SIZE, "metric '%s' not in CSV header", metric);
                        if (!from_stdin) fclose(f);
                        return -1;
                    }
                }
                continue;   /* header consumed */
            } else {
                if (strcmp(metric, "response") != 0) {
                    snprintf(err, DOE_ERR_SIZE,
                             "no header in '%s'; cannot locate metric '%s'", path, metric);
                    if (!from_stdin) fclose(f);
                    return -1;
                }
                metric_col = 1;
                /* fall through: this line is data */
            }
        }

        int nf = csv_split(line, fields, 512);
        if (nf <= metric_col) {
            snprintf(err, DOE_ERR_SIZE, "line %d: only %d column(s), metric at %d",
                     line_no, nf, metric_col + 1);
            if (!from_stdin) fclose(f);
            return -1;
        }

        char *endp;
        long run_id = strtol(trim(fields[0]), &endp, 10);
        if (*endp != '\0' || run_id < 1) {
            snprintf(err, DOE_ERR_SIZE, "line %d: invalid run_id", line_no);
            if (!from_stdin) fclose(f);
            return -1;
        }
        if ((size_t)run_id > max_rows) {
            snprintf(err, DOE_ERR_SIZE, "line %d: run_id %ld exceeds run count %zu",
                     line_no, run_id, max_rows);
            if (!from_stdin) fclose(f);
            return -1;
        }

        char *vs = trim(fields[metric_col]);
        if (vs[0] == '\0') continue;   /* missing metric cell */

        double v = strtod(vs, &endp);
        if (*endp != '\0') {
            snprintf(err, DOE_ERR_SIZE, "line %d: invalid value '%s' for metric '%s'",
                     line_no, vs, metric);
            if (!from_stdin) fclose(f);
            return -1;
        }
        if (!isfinite(v)) {
            snprintf(err, DOE_ERR_SIZE, "line %d: non-finite value '%s'", line_no, vs);
            if (!from_stdin) fclose(f);
            return -1;
        }

        responses[run_id - 1] = v;
        count++;
    }

    if (!from_stdin) fclose(f);
    if (count == 0) {
        snprintf(err, DOE_ERR_SIZE, "no data rows in '%s'", path);
        return -1;
    }
    *count_out = count;
    return 0;
}
