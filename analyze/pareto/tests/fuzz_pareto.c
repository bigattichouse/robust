/*
 * fuzz_pareto.c — deterministic random-input fuzz for pareto_read_csv and
 * pareto_front_load (see SECURITY.md). Built and run with ASan/UBSan via
 * `make fuzz`. Same three generation strategies as common/tests/fuzz_parsers.c:
 *
 *   1. pure random bytes
 *   2. token soup drawn from a format-aware dictionary
 *   3. byte-level mutation (flip / truncate / insert) of a valid template
 *
 * Usage: fuzz_pareto [seed] [iters]   (defaults: seed 20260806, 5000)
 * Fully deterministic from the seed, so any sanitizer report reproduces by
 * rerunning with the same arguments.
 *
 * Why these two functions specifically: they are hand-rolled parsers over
 * untrusted files, and pareto_front_load additionally runs strtok_r across the
 * `# objectives:` line and sscanf across the `# merge:` preamble — the newest
 * and least-exercised parsing in the repo.
 *
 * Invariants checked here:
 *   - parsing never crashes (memory errors are the sanitizers' job)
 *   - every failure leaves err NUL-terminated within DOE_ERR_SIZE
 *   - a successful load never reports fewer than 2 objectives, never exceeds
 *     the objective cap, and every returned point carries a non-NULL row
 *   - success implies the front is self-consistent enough to run the dominance
 *     core over without reading uninitialised values
 */

#define _POSIX_C_SOURCE 200809L

#include "pareto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 4096

static doe_rng_t g_rng;

static size_t rnd(size_t n) {
    if (n == 0) return 0;
    return (size_t)(doe_rng_next(&g_rng) % n);
}

static const char *CSV_DICT[] = {
    "run_id", "yield", "cost", "temp", ",", "\n", "\r\n", " ", "\t", "#",
    "0", "1", "-1", "0.5", "1e-6", "1e309", "-1e309", "inf", "-inf", "nan",
    "999999999999", "18446744073709551615", ".", "e", "+", "-",
    "0000000000000000000000000000000000000000",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
};

static const char *FRONT_DICT[] = {
    "# tgu-front 1\n", "# tgu-front\n", "# objectives:", " yield max", " cost min",
    " yield", " max", " min", " bogus", ",", ":", "\n", "#", " ",
    "# merge: b.csv 2026-08-06T00:00:00Z in=1 admitted=1 evicted=0 rejected=0 dup=0\n",
    "# merge: ", "in=", "admitted=", "evicted=", "rejected=", "dup=",
    "run_id,yield,cost\n", "1,0.5,10\n", "2,nan,20\n", "3,1e309,-inf\n",
    "-1", "18446744073709551615", "999999999999999999999999",
};

static const char *CSV_TMPL =
    "run_id,yield,cost\n"
    "1,0.50,10\n"
    "2,0.80,20\n"
    "# comment\n"
    "3,0.30,5\n";

static const char *FRONT_TMPL =
    "# tgu-front 1\n"
    "# objectives: yield max, cost min\n"
    "# merge: b1.csv 2026-08-06T12:00:00Z in=4 admitted=3 evicted=0 rejected=1 dup=0\n"
    "run_id,yield,cost\n"
    "2,0.80,20\n"
    "1,0.50,10\n"
    "3,0.30,5\n";

static size_t gen_random(char *buf, size_t cap) {
    size_t n = rnd(cap - 1);
    for (size_t i = 0; i < n; i++)
        buf[i] = (char)(unsigned char)(doe_rng_next(&g_rng) & 0xff);
    buf[n] = '\0';
    return n;
}

static size_t gen_soup(char *buf, size_t cap, const char **dict, size_t ndict) {
    size_t pos = 0;
    size_t toks = 1 + rnd(200);
    for (size_t t = 0; t < toks; t++) {
        const char *w = dict[rnd(ndict)];
        size_t len = strlen(w);
        if (pos + len + 1 > cap) break;
        memcpy(buf + pos, w, len);
        pos += len;
    }
    buf[pos] = '\0';
    return pos;
}

static size_t gen_mutant(char *buf, size_t cap, const char *tmpl) {
    size_t len = strlen(tmpl);
    if (len >= cap) len = cap - 1;
    memcpy(buf, tmpl, len);
    buf[len] = '\0';

    size_t edits = 1 + rnd(8);
    for (size_t e = 0; e < edits; e++) {
        switch (rnd(3)) {
        case 0:
            if (len) buf[rnd(len)] = (char)(unsigned char)(doe_rng_next(&g_rng) & 0xff);
            break;
        case 1:
            if (len) { len = rnd(len); buf[len] = '\0'; }
            break;
        default:
            if (len + 2 < cap) {
                size_t p = rnd(len + 1);
                memmove(buf + p + 1, buf + p, len - p + 1);
                buf[p] = (char)(unsigned char)(doe_rng_next(&g_rng) & 0xff);
                len++;
            }
            break;
        }
    }
    return len;
}

static int err_ok(const char *err) {
    return memchr(err, '\0', DOE_ERR_SIZE) != NULL;
}

static int write_input(const char *path, const char *buf, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, n, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    uint64_t seed  = argc > 1 ? strtoull(argv[1], NULL, 10) : 20260806ull;
    long     iters = argc > 2 ? strtol(argv[2], NULL, 10)   : 5000;
    doe_rng_seed(&g_rng, seed);
    printf("fuzz_pareto: seed=%llu iters=%ld\n", (unsigned long long)seed, iters);

    static char buf[MAX_INPUT];
    char err[DOE_ERR_SIZE];

    /* ---- pareto_read_csv ---- */
    const char *cpath = "build/fuzz_pareto_input.csv";
    long ok = 0;
    for (long i = 0; i < iters; i++) {
        size_t n;
        switch (rnd(3)) {
        case 0:  n = gen_random(buf, sizeof buf); break;
        case 1:  n = gen_soup(buf, sizeof buf, CSV_DICT,
                              sizeof CSV_DICT / sizeof *CSV_DICT); break;
        default: n = gen_mutant(buf, sizeof buf, CSV_TMPL); break;
        }
        if (write_input(cpath, buf, n) != 0) {
            fprintf(stderr, "FAIL: cannot write %s\n", cpath); return 1;
        }

        pareto_objective_t objs[PARETO_MAX_OBJECTIVES];
        size_t nobj = 0;
        memset(err, 'A', sizeof err);
        pareto_add_objective(objs, &nobj, "yield", PARETO_MAX, err);
        pareto_add_objective(objs, &nobj, "cost",  PARETO_MIN, err);

        pareto_point_t *pts = NULL; size_t cnt = 0; char *hdr = NULL;
        memset(err, 'A', sizeof err);
        if (pareto_read_csv(cpath, objs, nobj, "fuzz", &pts, &cnt, &hdr, err) == 0) {
            ok++;
            if (cnt > PARETO_MAX_ROWS) {
                fprintf(stderr, "FAIL: row count %zu > cap (iter %ld)\n", cnt, i);
                return 1;
            }
            for (size_t p = 0; p < cnt; p++) {
                if (!pts[p].row) {
                    fprintf(stderr, "FAIL: NULL row at %zu (iter %ld)\n", p, i);
                    return 1;
                }
            }
            /* Run the dominance core over whatever came back: catches any
             * uninitialised value the reader let through. */
            if (cnt) {
                unsigned char *keep = malloc(cnt);
                if (keep) {
                    pareto_non_dominated(pts, cnt, objs, nobj, keep);
                    free(keep);
                }
            }
            pareto_points_free(pts, cnt);
            free(hdr);
        } else if (!err_ok(err)) {
            fprintf(stderr, "FAIL: unterminated err from pareto_read_csv (iter %ld)\n", i);
            return 1;
        }
    }
    remove(cpath);
    printf("  pareto_read_csv:   %ld inputs, %ld parsed OK, no violations\n", iters, ok);

    /* ---- pareto_front_load ---- */
    const char *fpath = "build/fuzz_pareto_input.front";
    ok = 0;
    for (long i = 0; i < iters; i++) {
        size_t n;
        switch (rnd(3)) {
        case 0:  n = gen_random(buf, sizeof buf); break;
        case 1:  n = gen_soup(buf, sizeof buf, FRONT_DICT,
                              sizeof FRONT_DICT / sizeof *FRONT_DICT); break;
        default: n = gen_mutant(buf, sizeof buf, FRONT_TMPL); break;
        }
        if (write_input(fpath, buf, n) != 0) {
            fprintf(stderr, "FAIL: cannot write %s\n", fpath); return 1;
        }

        pareto_front_t f;
        memset(err, 'A', sizeof err);
        if (pareto_front_load(fpath, &f, err) == 0) {
            ok++;
            if (f.objective_count < 2 || f.objective_count > PARETO_MAX_OBJECTIVES) {
                fprintf(stderr, "FAIL: objective_count %zu out of range (iter %ld)\n",
                        f.objective_count, i);
                return 1;
            }
            for (size_t p = 0; p < f.point_count; p++) {
                if (!f.points[p].row) {
                    fprintf(stderr, "FAIL: NULL row in front at %zu (iter %ld)\n", p, i);
                    return 1;
                }
            }
            if (f.point_count) {
                unsigned char *keep = malloc(f.point_count);
                if (keep) {
                    pareto_non_dominated(f.points, f.point_count,
                                         f.objectives, f.objective_count, keep);
                    free(keep);
                }
            }
            pareto_front_free(&f);
        } else if (!err_ok(err)) {
            fprintf(stderr, "FAIL: unterminated err from pareto_front_load (iter %ld)\n", i);
            return 1;
        }
    }
    remove(fpath);
    printf("  pareto_front_load: %ld inputs, %ld parsed OK, no violations\n", iters, ok);

    printf("fuzz_pareto: clean\n");
    return 0;
}
