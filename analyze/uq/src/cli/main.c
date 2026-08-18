/*
 * uq — what the output distribution looks like, not just who drives it.
 *
 * Morris and Sobol answer "which factors matter". Neither says whether the
 * response is tight or wildly spread, symmetric or skewed, or how bad the bad
 * cases are. A design whose mean is fine and whose 5th percentile is a failure
 * is not a good design, and no sensitivity index reports that.
 *
 * Consumes runs already paid for: no sampling, no model execution.
 * EXPANSION.md E1.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXLINE 8192

/*
 * The machine-readable contract, same as every other tool's: `tool`,
 * `command` and `schema` lead the document so a consumer can identify what
 * it is holding before it reads a single field. Without them a `uq` document
 * and a `regress` one were told apart by guessing at their keys. Additions
 * are free; renames and removals need a schema bump.
 */
#define UQ_JSON_SCHEMA 1

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <results.csv|-> [--metric NAME] [--bins N] [--json]\n"
        "\n"
        "  --metric NAME  column to summarise (default: response)\n"
        "  --bins N       histogram bins (default 20)\n"
        "  --json         machine-readable output\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }
    const char *path = argv[1];
    const char *metric = "response";
    size_t bins = 20;
    int as_json = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) metric = argv[++i];
        else if (strcmp(argv[i], "--bins") == 0 && i + 1 < argc) {
            long b = strtol(argv[++i], NULL, 10);
            if (b < 1 || b > 200) { fprintf(stderr, "Error: --bins must be 1..200\n"); return 2; }
            bins = (size_t)b;
        } else if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else { fprintf(stderr, "Error: unknown option '%s'\n", argv[i]); usage(argv[0]); return 2; }
    }

    char err[DOE_ERR_SIZE];
    /* Reuse the shared reader so `uq` accepts exactly what every other tool
     * does, including a .front file (its preamble is comments). */
    if (strcmp(path, "-") == 0) {
        fprintf(stderr, "Error: uq needs a file, not stdin (it must re-read to size)\n");
        return 2;
    }

    /*
     * Size against the file, in one pass, before reading it.
     *
     * This used to guess: allocate 1024, read, and if the buffer came back
     * exactly full, double it and retry. But the shared reader treats a run_id
     * past `max_rows` as a data error -- correctly, for the tools that pass a
     * DESIGN's run count there -- so the probe never reported "buffer full",
     * it reported "run_id 1025 exceeds run count 1024" and quit. The growth
     * loop could not run, and `uq`, whose whole job is summarising large
     * response sets, was capped at 1024 rows.
     */
    size_t n = 0;
    if (doe_csv_max_run_id(path, &n, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    /* NaN, not malloc's indeterminate bytes: the reader leaves a slot no row
     * mentions untouched, and the compaction below asks isfinite() about every
     * slot. Reading uninitialised memory to decide that is undefined, and it
     * decided wrong often enough to matter on a file with gaps in its ids. */
    double *y = malloc(n * sizeof *y);
    if (!y) { fprintf(stderr, "Error: out of memory\n"); return 1; }
    for (size_t i = 0; i < n; i++) y[i] = NAN;

    size_t got = 0;
    if (doe_csv_read_metric(path, metric, y, n, &got, err) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(y); return 1;
    }

    /* doe_csv_read_metric keys by run_id, so gaps arrive as untouched slots.
     * Compact to the values actually present. */
    size_t m = 0;
    for (size_t i = 0; i < n; i++) if (isfinite(y[i])) y[m++] = y[i];
    n = m;
    if (n == 0) {
        fprintf(stderr, "Error: no finite '%s' values in %s\n", metric, path);
        free(y); return 1;
    }

    double mean = doe_mean(y, n);
    double sd   = (n > 1) ? doe_std(y, n) : 0.0;
    double *srt = malloc(n * sizeof *srt);
    if (!srt) { fprintf(stderr, "Error: out of memory\n"); free(y); return 1; }
    memcpy(srt, y, n * sizeof *srt);
    double p05 = doe_quantile(srt, n, 0.05);   /* sorts srt in place */
    double p25 = doe_quantile(srt, n, 0.25);
    double p50 = doe_quantile(srt, n, 0.50);
    double p75 = doe_quantile(srt, n, 0.75);
    double p95 = doe_quantile(srt, n, 0.95);
    double lo = srt[0], hi = srt[n - 1];

    if (as_json) {
        /* Escaped: --metric comes from argv, and a quote in it turned this
         * mode's output into something no parser would accept. */
        char *m = doe_json_escape(metric);
        printf("{\n  \"tool\": \"uq\",\n  \"command\": \"analyze\",\n");
        printf("  \"schema\": %d,\n", UQ_JSON_SCHEMA);
        printf("  \"metric\": \"%s\",\n  \"n\": %zu,\n", m ? m : "", n);
        doe_free(m);
        printf("  \"mean\": %.10g,\n  \"sd\": %.10g,\n", mean, sd);
        printf("  \"min\": %.10g,\n  \"p05\": %.10g,\n  \"p25\": %.10g,\n"
               "  \"p50\": %.10g,\n  \"p75\": %.10g,\n  \"p95\": %.10g,\n  \"max\": %.10g\n}\n",
               lo, p05, p25, p50, p75, p95, hi);
        free(y); free(srt); return 0;
    }

    printf("Output distribution of '%s' — %zu runs\n\n", metric, n);
    printf("  mean   %12.6g      sd     %12.6g\n", mean, sd);
    printf("  min    %12.6g      max    %12.6g\n", lo, hi);
    printf("  p05    %12.6g      p95    %12.6g\n", p05, p95);
    printf("  p25    %12.6g      p75    %12.6g\n", p25, p75);
    printf("  median %12.6g\n", p50);

    if (hi > lo) {
        size_t *count = calloc(bins, sizeof *count);
        if (count) {
            for (size_t i = 0; i < n; i++) {
                size_t b = (size_t)((y[i] - lo) / (hi - lo) * (double)bins);
                if (b >= bins) b = bins - 1;
                count[b]++;
            }
            size_t peak = 1;
            for (size_t b = 0; b < bins; b++) if (count[b] > peak) peak = count[b];
            printf("\n  histogram (%zu bins)\n", bins);
            size_t cum = 0;
            for (size_t b = 0; b < bins; b++) {
                double edge = lo + (hi - lo) * (double)b / (double)bins;
                cum += count[b];
                printf("  %12.6g |", edge);
                size_t w = (count[b] * 40) / peak;
                for (size_t i = 0; i < w; i++) putchar('#');
                printf(" %zu  (cdf %.3f)\n", count[b], (double)cum / (double)n);
            }
            free(count);
        }
    } else {
        printf("\n  every run returned the same value; there is no distribution\n"
               "  to describe. Check the model actually depends on its inputs.\n");
    }

    /* Skew is the thing a mean hides: report it only when it is worth acting on. */
    if (sd > 0.0) {
        double lower = p50 - p05, upper = p95 - p50;
        if (upper > 2.0 * lower)
            printf("\n  Right-skewed: the top tail is %.1fx the bottom. The mean\n"
                   "  overstates a typical run.\n", upper / lower);
        else if (lower > 2.0 * upper)
            printf("\n  Left-skewed: the bottom tail is %.1fx the top. The mean\n"
                   "  overstates a typical run, and the bad cases are far worse\n"
                   "  than it suggests.\n", lower / upper);
    }

    free(y); free(srt);
    return 0;
}
