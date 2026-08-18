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
    return doe_csv_read_metric_seen(path, metric, responses, max_rows,
                                    NULL, count_out, err);
}

/*
 * The same read, plus a tally of how many rows landed on each run.
 *
 * "Did this file cover the design?" is a question every design-backed tool
 * has, and each one used to answer a piece of it by hand -- taguchi not at
 * all, which is how a results file short of its array got a level mean of
 * 0.000 and a file with twice the rows got a ranking off the first nine.
 * `seen[i] == 0` is a run the file never mentions and `seen[i] > 1` is one it
 * mentions twice; the caller decides which of those it can live with.
 */
int doe_csv_read_metric_seen(const char *path, const char *metric,
                             double *responses, size_t max_rows,
                             unsigned *seen, size_t *count_out, char *err) {
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
        if (seen) seen[run_id - 1]++;
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

/* ============================================================================
 * doe_table — a results CSV read whole, addressed by column name.
 *
 * doe_csv_read_metric answers "one metric, keyed by run id", which is what a
 * tool holding a design wants. Two tools want something else and each grew
 * their own parser to get it: `regress` needs one column per factor plus the
 * metric, in file order, and `desire` needs several named objective columns
 * AND every row back verbatim so it can echo them with a column appended.
 *
 * Both copies came with a fixed ceiling -- desire refused a file past 100000
 * rows or 256 columns -- which is the same class of defect as the 1024-row cap
 * that made `uq` fail on any real Monte-Carlo output. Growing to fit is the
 * whole point of reading a file you did not generate.
 * ============================================================================
 */

/* Generous but bounded: a results file larger than this is a mistake, and
 * reading it into memory unbounded is a worse one. */
#define DOE_TABLE_MAX_BYTES (64u * 1024u * 1024u)

/* Slurp a whole stream, growing as it goes. Works for stdin, which cannot be
 * seeked to find its length. */
static char *slurp(FILE *f, size_t *len_out, char *err) {
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) { snprintf(err, DOE_ERR_SIZE, "out of memory"); return NULL; }
    for (;;) {
        if (len == cap) {
            if (cap >= DOE_TABLE_MAX_BYTES) {
                snprintf(err, DOE_ERR_SIZE, "input exceeds %u bytes",
                         DOE_TABLE_MAX_BYTES);
                free(buf);
                return NULL;
            }
            size_t ncap = cap * 2;
            if (ncap > DOE_TABLE_MAX_BYTES) ncap = DOE_TABLE_MAX_BYTES;
            char *nb = realloc(buf, ncap);
            if (!nb) { snprintf(err, DOE_ERR_SIZE, "out of memory"); free(buf); return NULL; }
            buf = nb; cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) break;
    }
    if (len == cap) {   /* exactly full: one more byte would not have fit */
        char *nb = realloc(buf, cap + 1);
        if (!nb) { snprintf(err, DOE_ERR_SIZE, "out of memory"); free(buf); return NULL; }
        buf = nb;
    }
    buf[len] = '\0';
    *len_out = len;
    return buf;
}

/* Count the fields a line splits into, without modifying it. */
static size_t field_count(const char *s) {
    size_t n = 1;
    for (; *s; s++) if (*s == ',') n++;
    return n;
}

int doe_table_read(const char *path, doe_table_t *t, char *err) {
    if (!path || !t) {
        if (err) snprintf(err, DOE_ERR_SIZE, "null input to doe_table_read");
        return -1;
    }
    memset(t, 0, sizeof *t);

    int from_stdin = (path[0] == '-' && path[1] == '\0');
    FILE *f = from_stdin ? stdin : fopen(path, "r");
    if (!f) {
        snprintf(err, DOE_ERR_SIZE, "cannot open results '%s'", path);
        return -1;
    }
    size_t len = 0;
    char *text = slurp(f, &len, err);
    if (!from_stdin) fclose(f);
    if (!text) return -1;

    /* `raw` points into `text`; `work` is the copy the splitter chews up, so a
     * caller can still echo a row exactly as it arrived. */
    char *work = malloc(len + 1);
    if (!work) {
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        free(text);
        return -1;
    }
    memcpy(work, text, len + 1);

    t->text = text;
    t->work = work;

    /* Pass 1: find the header and count the data rows. */
    size_t nrows = 0, ncols = 0;
    int have_header = 0;
    for (size_t i = 0; i <= len; ) {
        size_t start = i;
        while (i < len && text[i] != '\n') i++;
        size_t end = i;
        if (i < len) i++;                      /* step past the newline */
        while (end > start && text[end - 1] == '\r') end--;
        text[end] = '\0';
        work[end] = '\0';
        const char *line = text + start;
        if (line[0] == '\0' || line[0] == '#') { if (start >= len) break; continue; }
        if (!have_header) { have_header = 1; ncols = field_count(line); }
        else nrows++;
        if (start >= len) break;
    }

    if (!have_header) {
        snprintf(err, DOE_ERR_SIZE, "'%s' has no header row", path);
        doe_table_free(t);
        return -1;
    }
    if (nrows == 0) {
        snprintf(err, DOE_ERR_SIZE, "'%s' has no data rows", path);
        doe_table_free(t);
        return -1;
    }

    t->names = calloc(ncols, sizeof *t->names);
    t->raw   = calloc(nrows, sizeof *t->raw);
    t->cells = calloc(nrows * ncols, sizeof *t->cells);
    if (!t->names || !t->raw || !t->cells) {
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        doe_table_free(t);
        return -1;
    }
    t->ncols = ncols;
    t->nrows = nrows;

    /* Pass 2: split. Lines are already NUL-terminated from pass 1. */
    size_t row = 0;
    int header_done = 0;
    for (size_t i = 0; i <= len; ) {
        size_t start = i;
        while (i < len && text[i] != '\0') i++;
        size_t stop = i;
        if (i <= len) i++;
        const char *line = text + start;
        if (line[0] == '\0' || line[0] == '#') { if (start >= len) break; continue; }

        char *w = work + start;
        /* Cut at the comma BEFORE trimming: trimming first leaves the field's
         * trailing spaces inside the not-yet-terminated remainder, so " yield "
         * came back as "yield " and no lookup by name could ever match it. */
        if (!header_done) {
            header_done = 1;
            size_t c = 0;
            char *p = w;
            while (c < ncols) {
                char *comma = strchr(p, ',');
                if (comma) *comma = '\0';
                t->names[c++] = trim(p);
                if (!comma) break;
                p = comma + 1;
            }
        } else {
            t->raw[row] = text + start;
            size_t c = 0;
            char *p = w;
            while (c < ncols) {
                char *comma = strchr(p, ',');
                if (comma) *comma = '\0';
                t->cells[row * ncols + c++] = trim(p);
                if (!comma) break;
                p = comma + 1;
            }
            row++;
        }
        (void)stop;
        if (start >= len) break;
    }

    return 0;
}

void doe_table_free(doe_table_t *t) {
    if (!t) return;
    free(t->names);
    free(t->raw);
    free(t->cells);
    free(t->text);
    free(t->work);
    memset(t, 0, sizeof *t);
}

long doe_table_col(const doe_table_t *t, const char *name) {
    if (!t || !name) return -1;
    for (size_t c = 0; c < t->ncols; c++)
        if (t->names[c] && strcmp(t->names[c], name) == 0) return (long)c;
    return -1;
}

const char *doe_table_cell(const doe_table_t *t, size_t row, size_t col) {
    if (!t || row >= t->nrows || col >= t->ncols) return NULL;
    return t->cells[row * t->ncols + col];
}

const char *doe_table_row(const doe_table_t *t, size_t row) {
    if (!t || row >= t->nrows) return NULL;
    return t->raw[row];
}

int doe_table_number(const doe_table_t *t, size_t row, size_t col, double *out) {
    const char *s = doe_table_cell(t, row, col);
    if (!s || s[0] == '\0') return -1;
    char *end;
    double v = strtod(s, &end);
    /* A trailing character means the cell is not a number, and a non-finite
     * one is not a measurement -- both are refusals, not values. */
    if (*end != '\0' || !isfinite(v)) return -1;
    if (out) *out = v;
    return 0;
}
