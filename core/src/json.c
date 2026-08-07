/*
 * json.c — minimal JSON helpers. Richer document builders are added per tool;
 * for now the shared piece is correct string escaping.
 */

#include "doe.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
