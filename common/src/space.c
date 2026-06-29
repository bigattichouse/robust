/*
 * space.c — parser and factor scaling for the .space factor-definition format.
 *
 *   factors:
 *     reflux_ratio:  1.0, 8.0                 # linear continuous range [min,max]
 *     catalyst_load: 1e-4, 1e-1  log          # log-scaled range (min > 0)
 *     packing:       random, structured, gauze # categorical (>= 2 levels)
 *     recycle:       true, false              # categorical
 *   seed:         20260628
 *   trajectories: 15        # Morris r
 *   grid_levels:  4         # Morris p
 *   samples:      1024      # Sobol N
 *   second_order: false     # Sobol
 *
 * Disambiguation: exactly two numeric values => LINEAR range; a trailing
 * "log"/"linear"/"cat" word forces the scale; anything else => CATEGORICAL.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '\0') return s;
    char *e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r')) {
        *e-- = '\0';
    }
    return s;
}

static int is_number(const char *s, double *out) {
    if (!s || *s == '\0') return 0;
    char *endp;
    double v = strtod(s, &endp);
    if (endp == s) return 0;
    while (*endp == ' ' || *endp == '\t') endp++;
    if (*endp != '\0') return 0;
    if (out) *out = v;
    return 1;
}

static int parse_factor(const char *name, char *value, doe_factor_t *f, char *err) {
    doe_scale_t scale = DOE_LINEAR;
    int have_marker = 0;

    /* Split off a trailing whitespace-separated scale marker, if present. */
    {
        char *ws = NULL;
        for (char *c = value; *c; c++) {
            if (*c == ' ' || *c == '\t') ws = c;
        }
        if (ws) {
            const char *word = ws + 1;
            if      (strcmp(word, "log") == 0)          { scale = DOE_LOG;         have_marker = 1; }
            else if (strcmp(word, "linear") == 0)       { scale = DOE_LINEAR;      have_marker = 1; }
            else if (strcmp(word, "cat") == 0 ||
                     strcmp(word, "categorical") == 0)  { scale = DOE_CATEGORICAL; have_marker = 1; }
            if (have_marker) { *ws = '\0'; value = trim(value); }
        }
    }

    /* Comma-split the remaining values. */
    char *toks[DOE_MAX_LEVELS];
    size_t ntok = 0;
    {
        char *p = value;
        for (;;) {
            if (ntok >= DOE_MAX_LEVELS) {
                snprintf(err, DOE_ERR_SIZE, "factor '%s': too many values (max %d)",
                         name, DOE_MAX_LEVELS);
                return -1;
            }
            char *c = strchr(p, ',');
            if (c) *c = '\0';
            toks[ntok++] = trim(p);
            if (!c) break;
            p = c + 1;
        }
    }
    if (ntok == 0 || toks[0][0] == '\0') {
        snprintf(err, DOE_ERR_SIZE, "factor '%s' has no values", name);
        return -1;
    }

    double a = 0.0, b = 0.0;
    if (!have_marker) {
        if (ntok == 2 && is_number(toks[0], &a) && is_number(toks[1], &b)) scale = DOE_LINEAR;
        else scale = DOE_CATEGORICAL;
    }

    if (strlen(name) >= DOE_MAX_NAME) {
        snprintf(err, DOE_ERR_SIZE, "factor name '%s' too long (max %d)", name, DOE_MAX_NAME - 1);
        return -1;
    }

    memset(f, 0, sizeof *f);
    strncpy(f->name, name, DOE_MAX_NAME - 1);
    f->scale = scale;

    if (scale == DOE_LINEAR || scale == DOE_LOG) {
        if (ntok != 2 || !is_number(toks[0], &a) || !is_number(toks[1], &b)) {
            snprintf(err, DOE_ERR_SIZE, "factor '%s': %s scale needs two numeric bounds",
                     name, scale == DOE_LOG ? "log" : "linear");
            return -1;
        }
        if (a >= b) {
            snprintf(err, DOE_ERR_SIZE,
                     "factor '%s': lower bound (%g) must be < upper bound (%g)", name, a, b);
            return -1;
        }
        if (scale == DOE_LOG && a <= 0.0) {
            snprintf(err, DOE_ERR_SIZE, "factor '%s': log scale requires positive bounds", name);
            return -1;
        }
        f->lo = a;
        f->hi = b;
        f->level_count = 0;
    } else {
        if (ntok < 2) {
            snprintf(err, DOE_ERR_SIZE, "factor '%s': categorical needs >= 2 levels", name);
            return -1;
        }
        f->level_count = ntok;
        for (size_t i = 0; i < ntok; i++) {
            if (strlen(toks[i]) >= DOE_MAX_VALUE) {
                snprintf(err, DOE_ERR_SIZE, "factor '%s': level '%s' too long", name, toks[i]);
                return -1;
            }
            strncpy(f->levels[i], toks[i], DOE_MAX_VALUE - 1);
        }
    }
    return 0;
}

int doe_space_parse(const char *content, doe_space_t *space, char *err) {
    memset(space, 0, sizeof *space);
    space->trajectories = 10;
    space->grid_levels  = 4;
    space->samples      = 1024;
    space->second_order = false;

    size_t len = strlen(content);
    char *buf = malloc(len + 1);
    if (!buf) {
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }
    memcpy(buf, content, len + 1);

    char *line = buf;
    int rc = 0;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *next = nl ? nl + 1 : NULL;

        char *hash = strchr(line, '#');     /* strip trailing comment */
        if (hash) *hash = '\0';

        char *t = trim(line);
        if (*t == '\0') { line = next; continue; }

        char *colon = strchr(t, ':');
        if (!colon) {
            snprintf(err, DOE_ERR_SIZE, "line without ':' -> '%s'", t);
            rc = -1; break;
        }
        *colon = '\0';
        char *key = trim(t);
        char *val = trim(colon + 1);

        if (strcmp(key, "factors") == 0) { line = next; continue; }   /* header */

        if (strcmp(key, "seed") == 0) {
            space->seed = strtoull(val, NULL, 10);
        } else if (strcmp(key, "trajectories") == 0) {
            space->trajectories = (size_t)strtoul(val, NULL, 10);
        } else if (strcmp(key, "grid_levels") == 0) {
            space->grid_levels = (size_t)strtoul(val, NULL, 10);
        } else if (strcmp(key, "samples") == 0) {
            space->samples = (size_t)strtoul(val, NULL, 10);
        } else if (strcmp(key, "second_order") == 0) {
            space->second_order = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "method") == 0 || strcmp(key, "array") == 0) {
            /* accepted but not used by the core */
        } else {
            if (space->factor_count >= DOE_MAX_FACTORS) {
                snprintf(err, DOE_ERR_SIZE, "too many factors (max %d)", DOE_MAX_FACTORS);
                rc = -1; break;
            }
            if (*val == '\0') {
                snprintf(err, DOE_ERR_SIZE, "factor '%s' has no values", key);
                rc = -1; break;
            }
            if (parse_factor(key, val, &space->factors[space->factor_count], err) != 0) {
                rc = -1; break;
            }
            space->factor_count++;
        }
        line = next;
    }

    free(buf);
    if (rc != 0) return rc;

    if (space->factor_count == 0) {
        snprintf(err, DOE_ERR_SIZE, "no factors defined");
        return -1;
    }
    return 0;
}

int doe_space_parse_file(const char *path, doe_space_t *space, char *err) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, DOE_ERR_SIZE, "cannot open '%s'", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, DOE_ERR_SIZE, "cannot seek '%s'", path);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        snprintf(err, DOE_ERR_SIZE, "cannot size '%s'", path);
        return -1;
    }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';

    int rc = doe_space_parse(buf, space, err);
    free(buf);
    return rc;
}

double doe_factor_scale(const doe_factor_t *f, double u) {
    if (f->scale == DOE_LOG) {
        double llo = log(f->lo), lhi = log(f->hi);
        return exp(llo + u * (lhi - llo));
    }
    return f->lo + u * (f->hi - f->lo);   /* LINEAR */
}

const char *doe_factor_value(const doe_factor_t *f, double u, char *buf, size_t buf_size) {
    if (f->scale == DOE_CATEGORICAL) {
        size_t idx = (size_t)(u * (double)f->level_count);
        if (idx >= f->level_count) idx = f->level_count - 1;   /* u == 1.0 guard */
        snprintf(buf, buf_size, "%s", f->levels[idx]);
    } else {
        snprintf(buf, buf_size, "%.10g", doe_factor_scale(f, u));
    }
    return buf;
}
