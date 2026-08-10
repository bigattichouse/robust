/*
 * json.c — minimal JSON helpers. Richer document builders are added per tool;
 * for now the shared piece is correct string escaping.
 */

#include "doe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

char *doe_json_escape(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    /* Worst case is every character escaping to \u00XX, six bytes each, plus
     * the terminator. That is exact, not generous -- so the writes below are
     * bounded explicitly rather than trusted to arithmetic. */
    size_t cap = len * 6 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;

    char *o = out;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  *o++ = '\\'; *o++ = '"';  break;
            case '\\': *o++ = '\\'; *o++ = '\\'; break;
            case '\n': *o++ = '\\'; *o++ = 'n';  break;
            case '\r': *o++ = '\\'; *o++ = 'r';  break;
            case '\t': *o++ = '\\'; *o++ = 't';  break;
            default:
                if (c < 0x20) {
                    /* snprintf, not sprintf: the bound is provable here, but a
                     * bounded call stays correct if the format ever changes. */
                    size_t left = cap - (size_t)(o - out);
                    int w = snprintf(o, left, "\\u%04x", c);
                    if (w < 0 || (size_t)w >= left) break;
                    o += w;
                } else {
                    *o++ = (char)c;
                }
        }
    }
    *o = '\0';
    return out;
}

/*
 * The bounded form. Truncation is silent by design: the callers are emitting
 * one field of a document, and a name longer than the buffer must not be
 * allowed to run off the end of it or to abort the whole output. Every
 * in-tree caller sizes `out` from DOE_MAX_NAME, so truncation cannot happen
 * there -- the clamp exists so a future caller with a smaller buffer still
 * produces a well-formed (if shortened) JSON string rather than a broken one.
 */
const char *doe_json_string(const char *s, char *out, size_t cap) {
    if (!out || cap == 0) return out;
    if (cap < 3) { out[0] = '\0'; return out; }

    size_t o = 0;
    out[o++] = '"';
    for (size_t i = 0; s && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        char esc[8];
        size_t n;
        switch (c) {
            case '"':  memcpy(esc, "\\\"", 2); n = 2; break;
            case '\\': memcpy(esc, "\\\\", 2); n = 2; break;
            case '\n': memcpy(esc, "\\n",  2); n = 2; break;
            case '\r': memcpy(esc, "\\r",  2); n = 2; break;
            case '\t': memcpy(esc, "\\t",  2); n = 2; break;
            default:
                if (c < 0x20) { n = (size_t)snprintf(esc, sizeof esc, "\\u%04x", c); }
                else          { esc[0] = (char)c; n = 1; }
        }
        if (o + n + 2 > cap) break;      /* room for the closing quote and NUL */
        memcpy(out + o, esc, n);
        o += n;
    }
    out[o++] = '"';
    out[o]   = '\0';
    return out;
}

const char *doe_json_number(double v, char *out, size_t cap) {
    if (!out || cap == 0) return out;
    /*
     * %.10g, not the %.4g the human tables use: this output exists to be
     * consumed, and a screening decision made on a rounded mu* is a different
     * decision. Ten digits round-trips every value these tools produce closely
     * enough to reproduce the ranking, without printing binary noise.
     */
    if (isfinite(v)) snprintf(out, cap, "%.10g", v);
    else             snprintf(out, cap, "null");
    return out;
}

char *doe_html_escape(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *out = malloc(len * 6 + 1);   /* worst case: '"' -> "&quot;" (6 chars) */
    if (!out) return NULL;

    char *o = out;
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
            case '&':  memcpy(o, "&amp;",  5); o += 5; break;
            case '<':  memcpy(o, "&lt;",   4); o += 4; break;
            case '>':  memcpy(o, "&gt;",   4); o += 4; break;
            case '"':  memcpy(o, "&quot;", 6); o += 6; break;
            case '\'': memcpy(o, "&#39;",  5); o += 5; break;
            default:   *o++ = s[i];
        }
    }
    *o = '\0';
    return out;
}

void doe_free(void *p) {
    free(p);
}
