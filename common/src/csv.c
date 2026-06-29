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

int doe_csv_read_metric(const char *path, const char *metric,
                        double *responses, size_t max_rows,
                        size_t *count_out, char *err) {
    FILE *f = fopen(path, "r");
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
                        fclose(f);
                        return -1;
                    }
                }
                continue;   /* header consumed */
            } else {
                if (strcmp(metric, "response") != 0) {
                    snprintf(err, DOE_ERR_SIZE,
                             "no header in '%s'; cannot locate metric '%s'", path, metric);
                    fclose(f);
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
            fclose(f);
            return -1;
        }

        char *endp;
        long run_id = strtol(trim(fields[0]), &endp, 10);
        if (*endp != '\0' || run_id < 1) {
            snprintf(err, DOE_ERR_SIZE, "line %d: invalid run_id", line_no);
            fclose(f);
            return -1;
        }
        if ((size_t)run_id > max_rows) {
            snprintf(err, DOE_ERR_SIZE, "line %d: run_id %ld exceeds run count %zu",
                     line_no, run_id, max_rows);
            fclose(f);
            return -1;
        }

        char *vs = trim(fields[metric_col]);
        if (vs[0] == '\0') continue;   /* missing metric cell */

        double v = strtod(vs, &endp);
        if (*endp != '\0') {
            snprintf(err, DOE_ERR_SIZE, "line %d: invalid value '%s' for metric '%s'",
                     line_no, vs, metric);
            fclose(f);
            return -1;
        }

        responses[run_id - 1] = v;
        count++;
    }

    fclose(f);
    if (count == 0) {
        snprintf(err, DOE_ERR_SIZE, "no data rows in '%s'", path);
        return -1;
    }
    *count_out = count;
    return 0;
}
