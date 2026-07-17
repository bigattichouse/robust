/*
 * fuzz_parsers.c — deterministic random-input fuzz for doe_space_parse and
 * doe_csv_read_metric (HARDENING.md Phase 3). Built and run with ASan/UBSan
 * via `make fuzz`. Not coverage-guided; three generation strategies per input:
 *
 *   1. pure random bytes
 *   2. token soup drawn from a format-aware dictionary
 *   3. byte-level mutation (flip / truncate / insert) of a valid template
 *
 * Usage: fuzz_parsers [seed] [iters]   (defaults: seed 20260716, 20000)
 * Fully deterministic from the seed, so any sanitizer report reproduces by
 * rerunning with the same arguments.
 *
 * Invariant checked here: parsing never crashes, and every failure leaves err
 * NUL-terminated within DOE_ERR_SIZE. Memory errors are the sanitizers' job.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 4096

static doe_rng_t g_rng;

static size_t rnd(size_t n) {
    if (n == 0) return 0;
    return (size_t)(doe_rng_next(&g_rng) % n);
}

static const char *SPACE_DICT[] = {
    "factors:", "factors", "seed:", "samples:", "trajectories:", "grid_levels:",
    "second_order:", "log", "true", "\n", "  ", " ", "\t", ":", ",", "#",
    "0", "1", "-1", "0.5", "1e-6", "1e309", "-1e309", "inf", "-inf", "nan",
    "999999999999", "18446744073709551615", "x", "x0", "café",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
};

static const char *CSV_DICT[] = {
    "0", "1", "2", "9", "-", "+", ".", "e", ",", "\n", "\r\n", " ", "\t", "#",
    "run_id", "response", "inf", "-inf", "nan", "3.14", "1e309",
    "999999999999", "0000000000000000000000000000000000000000",
};

static const char *SPACE_TMPL =
    "factors:\n"
    "  x0: 0,1\n"
    "  x1: 1e-3, 10 log\n"
    "  mode: a, b, c\n"
    "seed: 42\n"
    "samples: 128\n"
    "trajectories: 5\n"
    "grid_levels: 4\n";

static const char *CSV_TMPL =
    "run_id,response\n"
    "1,3.14\n"
    "2,2.71\n"
    "# comment\n"
    "3,-1e5\n";

static size_t gen_random(char *buf, size_t cap) {
    size_t n = rnd(cap - 1);
    for (size_t i = 0; i < n; i++) buf[i] = (char)(unsigned char)(doe_rng_next(&g_rng) & 0xff);
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
        case 0:   /* flip a byte */
            if (len) buf[rnd(len)] = (char)(unsigned char)(doe_rng_next(&g_rng) & 0xff);
            break;
        case 1:   /* truncate */
            if (len) { len = rnd(len); buf[len] = '\0'; }
            break;
        default:  /* insert a byte */
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

/* err must be NUL-terminated somewhere inside its DOE_ERR_SIZE bytes. */
static int err_ok(const char *err) {
    return memchr(err, '\0', DOE_ERR_SIZE) != NULL;
}

int main(int argc, char **argv) {
    uint64_t seed  = argc > 1 ? strtoull(argv[1], NULL, 10) : 20260716ull;
    long     iters = argc > 2 ? strtol(argv[2], NULL, 10)   : 20000;
    doe_rng_seed(&g_rng, seed);
    printf("fuzz_parsers: seed=%llu iters=%ld\n", (unsigned long long)seed, iters);

    static char buf[MAX_INPUT];
    char err[DOE_ERR_SIZE];
    long ok = 0;

    /* ---- doe_space_parse ---- */
    for (long i = 0; i < iters; i++) {
        switch (rnd(3)) {
        case 0:  gen_random(buf, sizeof buf); break;
        case 1:  gen_soup(buf, sizeof buf, SPACE_DICT, sizeof SPACE_DICT / sizeof *SPACE_DICT); break;
        default: gen_mutant(buf, sizeof buf, SPACE_TMPL); break;
        }
        memset(err, 'A', sizeof err);   /* prove the parser terminates it */
        doe_space_t sp;
        if (doe_space_parse(buf, &sp, err) == 0) {
            ok++;
            if (sp.factor_count > DOE_MAX_FACTORS) {
                fprintf(stderr, "FAIL: factor_count %zu > max (iter %ld)\n", sp.factor_count, i);
                return 1;
            }
        } else if (!err_ok(err)) {
            fprintf(stderr, "FAIL: unterminated err from doe_space_parse (iter %ld)\n", i);
            return 1;
        }
    }
    printf("  doe_space_parse:     %ld inputs, %ld parsed OK, no violations\n", iters, ok);

    /* ---- doe_csv_read_metric (via a scratch file) ---- */
    const char *path = "build/fuzz_input.csv";
    long citers = iters / 10;
    ok = 0;
    for (long i = 0; i < citers; i++) {
        size_t n;
        switch (rnd(3)) {
        case 0:  n = gen_random(buf, sizeof buf); break;
        case 1:  n = gen_soup(buf, sizeof buf, CSV_DICT, sizeof CSV_DICT / sizeof *CSV_DICT); break;
        default: n = gen_mutant(buf, sizeof buf, CSV_TMPL); break;
        }
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "FAIL: cannot write %s\n", path); return 1; }
        fwrite(buf, 1, n, f);
        fclose(f);

        double resp[16];
        size_t got = 0;
        memset(err, 'A', sizeof err);
        if (doe_csv_read_metric(path, "response", resp, 16, &got, err) == 0) {
            ok++;
        } else if (!err_ok(err)) {
            fprintf(stderr, "FAIL: unterminated err from doe_csv_read_metric (iter %ld)\n", i);
            return 1;
        }
    }
    remove(path);
    printf("  doe_csv_read_metric: %ld inputs, %ld parsed OK, no violations\n", citers, ok);

    printf("fuzz_parsers: clean\n");
    return 0;
}
