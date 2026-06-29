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
    char *out = malloc(len * 6 + 1);   /* worst case: every char -> \u00XX */
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
                    o += sprintf(o, "\\u%04x", c);
                } else {
                    *o++ = (char)c;
                }
        }
    }
    *o = '\0';
    return out;
}

void doe_free(void *p) {
    free(p);
}
