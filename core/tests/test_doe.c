/*
 * test_doe.c — unit tests for the libdoe core (PRNG, .space parser/scaling,
 * stats, JSON). The determinism test is the load-bearing one: reproducible
 * designs depend on identical PRNG streams from a given seed.
 */

#include "doe.h"
#include "test_framework.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---- PRNG -------------------------------------------------------------- */

static int test_prng_determinism(void) {
    doe_rng_t a, b;
    doe_rng_seed(&a, 12345);
    doe_rng_seed(&b, 12345);
    for (int i = 0; i < 1000; i++) {
        CHECK(doe_rng_next(&a) == doe_rng_next(&b));   /* same seed -> same stream */
    }

    doe_rng_t c;
    doe_rng_seed(&a, 12345);
    doe_rng_seed(&c, 99999);
    int differs = 0;
    for (int i = 0; i < 10; i++) {
        if (doe_rng_next(&a) != doe_rng_next(&c)) differs = 1;
    }
    CHECK(differs);                                    /* different seed -> different stream */
    return 1;
}

static int test_prng_uniform_range(void) {
    doe_rng_t r;
    doe_rng_seed(&r, 7);
    const int N = 100000;
    double sum = 0.0;
    for (int i = 0; i < N; i++) {
        double u = doe_rng_uniform(&r);
        CHECK(u >= 0.0 && u < 1.0);
        sum += u;
    }
    CHECK_DBL(sum / N, 0.5, 0.01);                     /* mean ~ 0.5 */
    return 1;
}

/* ---- .space parsing + scaling ----------------------------------------- */

static int test_space_linear_and_log(void) {
    const char *s =
        "factors:\n"
        "  x: 0.0, 10.0\n"
        "  y: 1e-3, 1.0 log\n"
        "seed: 42\n"
        "samples: 256\n"
        "trajectories: 15\n";
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);
    CHECK(sp.factor_count == 2);
    CHECK(sp.seed == 42);
    CHECK(sp.samples == 256);
    CHECK(sp.trajectories == 15);

    CHECK(sp.factors[0].scale == DOE_LINEAR);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 0.0), 0.0, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 0.5), 5.0, 1e-9);
    CHECK_DBL(doe_factor_scale(&sp.factors[0], 1.0), 10.0, 1e-9);

    CHECK(sp.factors[1].scale == DOE_LOG);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 0.0), 1e-3, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 1.0), 1.0, 1e-12);
    CHECK_DBL(doe_factor_scale(&sp.factors[1], 0.5), sqrt(1e-3 * 1.0), 1e-9);  /* geometric mid */
    return 1;
}

static int test_space_categorical(void) {
    const char *s =
        "factors:\n"
        "  mode: random, structured, gauze\n"
        "  recycle: true, false\n";
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse(s, &sp, err) == 0);

    CHECK(sp.factors[0].scale == DOE_CATEGORICAL);
    CHECK(sp.factors[0].level_count == 3);
    char buf[DOE_MAX_VALUE];
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.0,  buf, sizeof buf), "random") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.5,  buf, sizeof buf), "structured") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 0.99, buf, sizeof buf), "gauze") == 0);
    CHECK(strcmp(doe_factor_value(&sp, 0, 1.0,  buf, sizeof buf), "gauze") == 0);  /* u==1 guard */

    CHECK(sp.factors[1].scale == DOE_CATEGORICAL);
    CHECK(sp.factors[1].level_count == 2);
    return 1;
}

static int test_space_errors(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];
    CHECK(doe_space_parse("factors:\n  x: 5.0, 1.0\n",   &sp, err) != 0);  /* lo >= hi  */
    CHECK(doe_space_parse("factors:\n  x: -1, 10 log\n", &sp, err) != 0);  /* log <= 0  */
    CHECK(doe_space_parse("seed: 1\n",                   &sp, err) != 0);  /* no factors */
    return 1;
}

/*
 * Per-line errors name their line, and the number is the FILE's, not the
 * count of lines the parser bothered to look at. Blank lines and comments are
 * the whole difficulty: a scanner that skips them (strtok does) reports a
 * number that is confidently wrong, which is worse than reporting none.
 */
static int test_space_errors_report_the_line(void) {
    doe_space_t sp;
    char err[DOE_ERR_SIZE];

    /* the bad factor is on line 6, after two blanks and a comment */
    CHECK(doe_space_parse("factors:\n"          /* 1 */
                          "  a: 0, 1\n"         /* 2 */
                          "\n"                  /* 3 */
                          "# a comment\n"       /* 4 */
                          "\n"                  /* 5 */
                          "  b: 5.0, 1.0\n",    /* 6 <- lo >= hi */
                          &sp, err) != 0);
    CHECK(strstr(err, "line 6:") != NULL);

    /* line 1, and the message should not stutter "line 1: line without..." */
    CHECK(doe_space_parse("not a key value\n", &sp, err) != 0);
    CHECK(strstr(err, "line 1:") != NULL);
    CHECK(strstr(err, "line without") == NULL);

    /* a bad value on the very last line, with no trailing newline */
    CHECK(doe_space_parse("factors:\n  a: 0,1\n  b: 1,0", &sp, err) != 0);
    CHECK(strstr(err, "line 3:") != NULL);

    /* errors from the group parser get located too */
    CHECK(doe_space_parse("factors:\n  a: 0,1\n  b: 0,1\n"
                          "groups:\n"
                          "  g1: a, nosuchfactor\n", &sp, err) != 0);
    CHECK(strstr(err, "line 5:") != NULL);

    /*
     * Whole-file problems deliberately carry NO line number: they are not
     * about a line, and pointing at one would point at something that is not
     * wrong. This is the half of the feature that is easy to get wrong by
     * prefixing everything.
     */
    CHECK(doe_space_parse("seed: 1\n", &sp, err) != 0);          /* no factors   */
    CHECK(strstr(err, "line ") == NULL);
    CHECK(doe_space_parse("factors:\n  a: 0,1\nsamples: 99999999999\n", &sp, err) != 0);
    CHECK(strstr(err, "line ") == NULL);                          /* resource cap */
    CHECK(doe_space_parse("factors:\n  a: 0,1\n  b: 0,1\n"
                          "groups:\n  g1: a\n  g2: a\n", &sp, err) != 0);
    CHECK(strstr(err, "line ") == NULL);                          /* partition    */
    return 1;
}

/*
 * The line prefix rewrites a finished error message in place. That is a new
 * string-building site in a codebase whose most repeated defect is exactly
 * that, so it is bracketed by sentinels and checked in EVERY build mode
 * rather than left to `make test-asan`.
 */
static int test_space_error_prefix_never_overflows(void) {
    /* err sits in the middle of a larger block; the guard bytes either side
     * must be untouched no matter how long the message wanted to be. */
    enum { GUARD = 64 };
    char block[GUARD + DOE_ERR_SIZE + GUARD];
    memset(block, 0xAB, sizeof block);
    char *err = block + GUARD;

    /* A factor name far longer than the buffer, on a high line number, so the
     * message is truncated AND the prefix still has to fit. */
    char spec[4096];
    size_t off = 0;
    off += (size_t)snprintf(spec + off, sizeof spec - off, "factors:\n");
    for (int i = 0; i < 20; i++)
        off += (size_t)snprintf(spec + off, sizeof spec - off, "  f%d: 0, 1\n", i);
    off += (size_t)snprintf(spec + off, sizeof spec - off, "  ");
    for (int i = 0; i < 900 && off < sizeof spec - 16; i++) spec[off++] = 'x';
    snprintf(spec + off, sizeof spec - off, ": 5.0, 1.0\n");

    doe_space_t sp;
    CHECK(doe_space_parse(spec, &sp, err) != 0);
    CHECK(strlen(err) < DOE_ERR_SIZE);

    for (size_t i = 0; i < GUARD; i++) {
        CHECK((unsigned char)block[i] == 0xAB);                       /* before */
        CHECK((unsigned char)block[GUARD + DOE_ERR_SIZE + i] == 0xAB); /* after */
    }
    return 1;
}

/* ---- stats ------------------------------------------------------------- */

static int test_stats(void) {
    double x[5] = {2, 4, 4, 4, 5};
    CHECK_DBL(doe_mean(x, 5), 3.8, 1e-12);
    CHECK_DBL(doe_variance(x, 5), 1.2, 1e-12);          /* sample variance (n-1) */
    CHECK_DBL(doe_std(x, 5), sqrt(1.2), 1e-12);
    return 1;
}

/* ---- JSON -------------------------------------------------------------- */

static int test_json_escape(void) {
    char *j = doe_json_escape("a\"b\\c\n");
    CHECK(j != NULL);
    CHECK(strcmp(j, "a\\\"b\\\\c\\n") == 0);
    doe_free(j);
    return 1;
}

/*
 * doe_json_string: the bounded escape the --json emitters use per field. It
 * has to produce a QUOTED, escaped, always-terminated string, and it has to
 * stay well-formed when the buffer is too small -- a half-written string with
 * no closing quote would take the whole document down with it.
 */
static int test_json_string_bounded(void) {
    char buf[DOE_JSON_STR(DOE_MAX_NAME)];

    CHECK(strcmp(doe_json_string("kv_type", buf, sizeof buf), "\"kv_type\"") == 0);
    CHECK(strcmp(doe_json_string("a\"b\\c", buf, sizeof buf), "\"a\\\"b\\\\c\"") == 0);
    CHECK(strcmp(doe_json_string("x\ny", buf, sizeof buf), "\"x\\ny\"") == 0);
    CHECK(strcmp(doe_json_string("", buf, sizeof buf), "\"\"") == 0);
    CHECK(strcmp(doe_json_string(NULL, buf, sizeof buf), "\"\"") == 0);

    /* A control character takes six bytes; the buffer must not be overrun. */
    CHECK(strcmp(doe_json_string("\x01", buf, sizeof buf), "\"\\u0001\"") == 0);

    /* Truncation stays syntactically valid: opening quote, whole escape
     * sequences only, closing quote, NUL -- never a split "\\u00" fragment. */
    char small[8];
    const char *out = doe_json_string("abcdefghij", small, sizeof small);
    CHECK(strlen(out) < sizeof small);
    CHECK(out[0] == '"' && out[strlen(out) - 1] == '"');
    CHECK(strcmp(out, "\"abcde\"") == 0);   /* 8 bytes: 5 chars, 2 quotes, NUL */

    char tiny[4];
    out = doe_json_string("\x01\x01", tiny, sizeof tiny);
    CHECK(strcmp(out, "\"\"") == 0);       /* no room for a 6-byte escape */

    /* A buffer too small even for `""` must still terminate, not scribble. */
    char nothing[2] = { 'X', 'X' };
    out = doe_json_string("a", nothing, sizeof nothing);
    CHECK(out[0] == '\0');
    return 1;
}

/*
 * doe_json_number: JSON has no NaN or Infinity literal. printf would emit a
 * bare `nan`/`inf` token, which turns one odd value into a document the
 * consumer cannot read at all -- so those become `null`.
 */
static int test_json_number(void) {
    char buf[DOE_JSON_NUM];

    CHECK(strcmp(doe_json_number(0.0, buf, sizeof buf), "0") == 0);
    CHECK(strcmp(doe_json_number(215.625, buf, sizeof buf), "215.625") == 0);
    CHECK(strcmp(doe_json_number(-1.5, buf, sizeof buf), "-1.5") == 0);
    CHECK(strcmp(doe_json_number(0.0 / 0.0, buf, sizeof buf), "null") == 0);
    CHECK(strcmp(doe_json_number(1.0 / 0.0, buf, sizeof buf), "null") == 0);
    CHECK(strcmp(doe_json_number(-1.0 / 0.0, buf, sizeof buf), "null") == 0);

    /* Ten significant digits: enough that a ranking survives the round trip,
     * which %.4g (what the human tables print) would not guarantee. */
    CHECK(strcmp(doe_json_number(1.0 / 3.0, buf, sizeof buf), "0.3333333333") == 0);

    /* No exponent form that JSON rejects: %g yields e.g. 1e-09, which is legal. */
    doe_json_number(1e-9, buf, sizeof buf);
    CHECK(strchr(buf, 'e') != NULL && strstr(buf, "e-") != NULL);
    return 1;
}

/* ---- doe_ols_src: standardized regression -------------------------------- */

/*
 * EXPANSION.md E1's validation clause: exact on a known linear model. With
 * y = 10*x0 + 5*x1 + 0*x2 the standardized coefficients must be proportional
 * to (10, 5, 0) and R^2 must be exactly 1 -- there is nothing for a linear
 * model to miss.
 */
static int test_ols_exact_on_linear(void) {
    enum { N = 60, K = 3 };
    double X[N * K], y[N];
    doe_rng_t rng; doe_rng_seed(&rng, 11);
    /*
     * Each column is an independent PERMUTATION of the same values, so every
     * column has identical mean and standard deviation by construction. That
     * matters: SRC_j = beta_j * sd_j / sd_y, so unequal column spread shifts
     * the ratio away from beta_0/beta_1 for reasons that have nothing to do
     * with the regression. With plain random draws the sd's differ by a few
     * percent and the ratio lands near 1.6 rather than 2 -- noise, not error.
     */
    for (size_t j = 0; j < K; j++) {
        double v[N];
        for (size_t i = 0; i < N; i++) v[i] = (double)i / (double)N;
        for (size_t i = N; i > 1; i--) {          /* Fisher-Yates */
            size_t r = (size_t)(doe_rng_uniform(&rng) * (double)i);
            if (r >= i) r = i - 1;
            double t = v[i-1]; v[i-1] = v[r]; v[r] = t;
        }
        for (size_t i = 0; i < N; i++) X[i * K + j] = v[i];
    }
    for (size_t i = 0; i < N; i++)
        y[i] = 10.0 * X[i*K+0] + 5.0 * X[i*K+1] + 0.0 * X[i*K+2];
    double coef[K], r2 = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) == 0);

    CHECK_DBL(r2, 1.0, 1e-9);
    CHECK(coef[0] > 0.0 && coef[1] > 0.0);
    CHECK(fabs(coef[2]) < 1e-9);              /* inert factor is exactly flat */
    /* SRC is proportional to the coefficients when the factors share a
     * distribution, so the ratio must recover 10/5. */
    CHECK_DBL(coef[0] / coef[1], 2.0, 1e-6);
    return 1;
}

/* The sign is the point: SRC reports direction, which variance shares cannot. */
static int test_ols_reports_direction(void) {
    enum { N = 50, K = 2 };
    double X[N * K], y[N];
    doe_rng_t rng; doe_rng_seed(&rng, 3);
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < K; j++) X[i * K + j] = doe_rng_uniform(&rng);
        y[i] = 4.0 * X[i*K+0] - 9.0 * X[i*K+1];
    }
    double coef[K], r2 = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) == 0);
    CHECK(coef[0] > 0.0);                     /* raises  */
    CHECK(coef[1] < 0.0);                     /* lowers  */
    CHECK(fabs(coef[1]) > fabs(coef[0]));     /* and matters more */
    return 1;
}

/* A curved but monotone response: SRC is imperfect, ranks recover it. */
static int test_ols_ranks_beat_values_on_curvature(void) {
    enum { N = 80, K = 1 };
    double X[N * K], y[N], Xr[N * K], yr[N];
    doe_rng_t rng; doe_rng_seed(&rng, 21);
    for (size_t i = 0; i < N; i++) {
        double x = doe_rng_uniform(&rng);
        X[i] = x;
        y[i] = x * x * x * x * x;             /* strictly increasing, very curved */
    }
    memcpy(Xr, X, sizeof X); memcpy(yr, y, sizeof y);

    double c1[K], r2v = 0.0, c2[K], r2r = 0.0; char err[DOE_ERR_SIZE];
    CHECK(doe_ols_src(X, y, N, K, c1, &r2v, err) == 0);
    doe_rank_transform(Xr, yr, N, K);
    CHECK(doe_ols_src(Xr, yr, N, K, c2, &r2r, err) == 0);

    CHECK(r2r > r2v);                          /* ranks explain more */
    CHECK(r2r > 0.99);                         /* monotone: nearly perfect */
    return 1;
}

static int test_ols_rejects_degenerate(void) {
    enum { N = 20, K = 2 };
    double X[N * K], y[N]; char err[DOE_ERR_SIZE];
    doe_rng_t rng; doe_rng_seed(&rng, 7);

    /* constant response */
    for (size_t i = 0; i < N; i++) {
        X[i*K+0] = doe_rng_uniform(&rng); X[i*K+1] = doe_rng_uniform(&rng);
        y[i] = 3.0;
    }
    double coef[K], r2;
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(memchr(err, '\0', DOE_ERR_SIZE) != NULL);
    CHECK(strstr(err, "constant") != NULL);

    /* constant factor column */
    for (size_t i = 0; i < N; i++) { X[i*K+0] = 0.5; y[i] = doe_rng_uniform(&rng); }
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "constant") != NULL);

    /* two factors that move together: effects cannot be separated */
    for (size_t i = 0; i < N; i++) {
        double v = doe_rng_uniform(&rng);
        X[i*K+0] = v; X[i*K+1] = 2.0 * v;
        y[i] = v;
    }
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, N, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "rank-deficient") != NULL);

    /* fewer runs than factors */
    memset(err, 'A', sizeof err);
    CHECK(doe_ols_src(X, y, 2, K, coef, &r2, err) != 0);
    CHECK(strstr(err, "more runs than factors") != NULL);
    return 1;
}

static int test_quantiles(void) {
    double v[9] = {9, 1, 8, 2, 7, 3, 6, 4, 5};
    CHECK_DBL(doe_median(v, 9), 5.0, 1e-12);
    double w[5] = {10, 20, 30, 40, 50};
    CHECK_DBL(doe_quantile(w, 5, 0.0), 10.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 1.0), 50.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 0.5), 30.0, 1e-12);
    CHECK_DBL(doe_quantile(w, 5, 0.25), 20.0, 1e-12);
    return 1;
}

/*
 * The JSON escaper allocates len*6+1, which is EXACTLY the worst case: every
 * character escaping to \u00XX. A string entirely of control characters sits
 * on that boundary, so it is the input that would expose an off-by-one.
 */
static int test_json_escape_worst_case(void) {
    char in[64];
    for (size_t i = 0; i < sizeof in - 1; i++) in[i] = 0x01;   /* all escape */
    in[sizeof in - 1] = '\0';

    char *e = doe_json_escape(in);
    CHECK(e != NULL);
    CHECK(strlen(e) == (sizeof in - 1) * 6);
    for (size_t i = 0; i < strlen(e); i += 6)
        CHECK(strncmp(e + i, "\\u0001", 6) == 0);
    doe_free(e);

    /* The named escapes, and a plain string passing through untouched. */
    e = doe_json_escape("a\"b\\c\nd\re\tf");
    CHECK(e != NULL);
    CHECK(strstr(e, "\\\"") && strstr(e, "\\\\") && strstr(e, "\\n")
          && strstr(e, "\\r") && strstr(e, "\\t"));
    doe_free(e);

    e = doe_json_escape("plain");
    CHECK(e != NULL && strcmp(e, "plain") == 0);
    doe_free(e);

    CHECK(doe_json_escape(NULL) == NULL);
    return 1;
}

/* ============================================================================
 * Sobol sequence (M5) — pinned against Joe & Kuo's own reference generator.
 *
 * Every constant below came from THEIR sobol.cc compiled and run against
 * new-joe-kuo-6.21201, not from ours. `sources/README.md` records the
 * provenance and `sources/fetch.sh` re-downloads both, so any of this can be
 * re-derived from scratch.
 * ============================================================================ */

/* The 10x3 sample published on the Joe-Kuo web page as the expected output of
 * `./sobol 10 3 new-joe-kuo-6.21201`. This is the one reference vector that
 * needs no local tooling at all to check: it is printed on the page. */
static int test_sobol_matches_published_sample(void) {
    static const double want[10][3] = {
        {0.0,    0.0,    0.0   }, {0.5,    0.5,    0.5   },
        {0.75,   0.25,   0.25  }, {0.25,   0.75,   0.75  },
        {0.375,  0.375,  0.625 }, {0.875,  0.875,  0.125 },
        {0.625,  0.125,  0.875 }, {0.125,  0.625,  0.375 },
        {0.1875, 0.3125, 0.9375}, {0.6875, 0.8125, 0.4375},
    };
    double p[30];
    CHECK(doe_sample_sobol(10, 3, p) == 0);
    for (size_t i = 0; i < 10; i++)
        for (size_t j = 0; j < 3; j++)
            /* dyadic rationals: the match is exact, not approximate */
            CHECK(p[i * 3 + j] == want[i][j]);
    return 1;
}

/*
 * Breadth, cheaply: a rolling checksum over a whole block of the sequence.
 * h = (h*31 + round(x * 2^32)) mod 1000000007, in point-then-dimension order.
 * Coordinates are exact multiples of 2^-32, so the scaled value is an exact
 * integer and the checksum is reproducible on any IEEE-754 platform.
 *
 * The three expected values were computed from the REFERENCE generator's
 * output, so this fails if our sequence diverges from theirs anywhere in the
 * block -- including in dimensions and at sample counts far too large to write
 * out by hand.
 */
static uint64_t sobol_checksum(size_t n, size_t dim0, size_t k) {
    double *p = malloc(n * k * sizeof *p);
    if (!p) return 0;
    if (doe_sample_sobol_dims(n, dim0, k, p) != 0) { free(p); return 0; }
    uint64_t h = 0;
    for (size_t i = 0; i < n * k; i++)
        h = (h * 31u + (uint64_t)(p[i] * 4294967296.0)) % 1000000007u;
    free(p);
    return h;
}

static int test_sobol_matches_reference_in_bulk(void) {
    /* 4096 points x 300 dimensions */
    CHECK(sobol_checksum(4096, 0, 300) == 965049671u);
    /* 65536 points x 8 dimensions — the sample count `make validate` uses,
     * which needs 16 direction numbers per dimension rather than 12 */
    CHECK(sobol_checksum(65536, 0, 8) == 43231725u);
    /* dimensions 513..1024 — the top of the vendored table, and the offset
     * path that gives `sobol` its B half */
    CHECK(sobol_checksum(512, 512, 512) == 540947946u);
    return 1;
}

/*
 * The A/B contract from Saltelli et al. 2010 Sec. 5.1, as a test rather than a
 * comment: the two halves must be the left and right halves of ONE
 * 2k-dimensional sequence. The failure this guards is a caller "helpfully"
 * generating B as a second k-dimensional draw, which produces B == A exactly.
 */
static int test_sobol_halves_are_one_sequence(void) {
    const size_t n = 256, k = 6;
    double whole[256 * 12], a[256 * 6], b[256 * 6];

    CHECK(doe_sample_sobol(n, 2 * k, whole) == 0);
    CHECK(doe_sample_sobol_dims(n, 0, k, a) == 0);
    CHECK(doe_sample_sobol_dims(n, k, k, b) == 0);

    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < k; j++) {
            CHECK(a[i * k + j] == whole[i * 2 * k + j]);          /* left half  */
            CHECK(b[i * k + j] == whole[i * 2 * k + k + j]);      /* right half */
        }

    /* and the halves are genuinely different columns, not a repeated draw */
    int differs = 0;
    for (size_t i = 0; i < n * k; i++) if (a[i] != b[i]) differs = 1;
    CHECK(differs);
    return 1;
}

/*
 * Balance: for the first 2^m points, each dimension must place exactly one
 * point in each of the 2^m equal bins of [0,1). This is what a (0,1)-sequence
 * in base 2 guarantees, and it is the property the whole method rests on --
 * an ordinary PRNG fails it immediately. Checked at the far end of the table
 * as well as the near end, since a table transcription error would most
 * plausibly land there.
 */
static int test_sobol_is_equidistributed(void) {
    const size_t n = 1024;
    static const size_t dims[] = {0, 1, 2, 40, 511, 1000, 1023};
    double *p = malloc(n * sizeof *p);
    char *seen = malloc(n);
    CHECK(p && seen);
    for (size_t d = 0; d < sizeof dims / sizeof *dims; d++) {
        if (doe_sample_sobol_dims(n, dims[d], 1, p) != 0) { free(p); free(seen); return 0; }
        memset(seen, 0, n);
        for (size_t i = 0; i < n; i++) {
            size_t bin = (size_t)(p[i] * (double)n);
            if (bin >= n || seen[bin]) { free(p); free(seen); return 0; }
            seen[bin] = 1;
        }
    }
    free(p); free(seen);
    return 1;
}

/* The dimension cap must be an error, never a silent fallback to something
 * else, and a failed call must leave `out` untouched (the doe.h contract). */
static int test_sobol_rejects_out_of_range(void) {
    double p[8];
    for (size_t i = 0; i < 8; i++) p[i] = -12345.0;

    CHECK(doe_sample_sobol(4, DOE_SOBOL_MAX_DIM + 1, p) != 0);
    CHECK(doe_sample_sobol_dims(4, DOE_SOBOL_MAX_DIM, 1, p) != 0);
    CHECK(doe_sample_sobol_dims(4, 1, DOE_SOBOL_MAX_DIM, p) != 0);
    /* dim0 + k must not be allowed to wrap */
    CHECK(doe_sample_sobol_dims(4, (size_t)-1, 2, p) != 0);
    CHECK(doe_sample_sobol(0, 2, p) != 0);
    CHECK(doe_sample_sobol(4, 0, p) != 0);
    CHECK(doe_sample_sobol(4, 2, NULL) != 0);

    for (size_t i = 0; i < 8; i++) CHECK(p[i] == -12345.0);

    /* the largest legal request still succeeds */
    double *big = malloc(2 * DOE_SOBOL_MAX_DIM * sizeof *big);
    CHECK(big != NULL);
    CHECK(doe_sample_sobol(2, DOE_SOBOL_MAX_DIM, big) == 0);
    free(big);
    return 1;
}

/* n = 1 yields the origin alone and must not walk off the direction vector. */
static int test_sobol_single_point(void) {
    double p[4] = {9, 9, 9, 9};
    CHECK(doe_sample_sobol(1, 4, p) == 0);
    for (size_t i = 0; i < 4; i++) CHECK(p[i] == 0.0);
    return 1;
}

int main(void) {
    printf("libdoe core tests\n");
    RUN_TEST(test_sobol_matches_published_sample);
    RUN_TEST(test_sobol_matches_reference_in_bulk);
    RUN_TEST(test_sobol_halves_are_one_sequence);
    RUN_TEST(test_sobol_is_equidistributed);
    RUN_TEST(test_sobol_rejects_out_of_range);
    RUN_TEST(test_sobol_single_point);
    RUN_TEST(test_json_escape_worst_case);
    RUN_TEST(test_ols_exact_on_linear);
    RUN_TEST(test_ols_reports_direction);
    RUN_TEST(test_ols_ranks_beat_values_on_curvature);
    RUN_TEST(test_ols_rejects_degenerate);
    RUN_TEST(test_quantiles);
    RUN_TEST(test_prng_determinism);
    RUN_TEST(test_prng_uniform_range);
    RUN_TEST(test_space_linear_and_log);
    RUN_TEST(test_space_categorical);
    RUN_TEST(test_space_errors);
    RUN_TEST(test_space_errors_report_the_line);
    RUN_TEST(test_space_error_prefix_never_overflows);
    RUN_TEST(test_stats);
    RUN_TEST(test_json_escape);
    RUN_TEST(test_json_string_bounded);
    RUN_TEST(test_json_number);
    return TEST_SUMMARY();
}
