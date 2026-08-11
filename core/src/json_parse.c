/*
 * json_parse.c — a small, strict JSON reader.
 *
 * Enough to read the `--json` documents this toolkit emits, and nothing more:
 * objects, arrays, strings, numbers, true/false/null. No comments, no trailing
 * commas, no NaN/Infinity — the emitters never produce them, and accepting
 * them here would only let a malformed document through quietly.
 *
 * It parses into a flat array of nodes with indices rather than pointers, so
 * one allocation holds the whole tree and freeing is a single call.
 *
 * Reads files the user names, so per SECURITY.md it assumes nothing about the
 * input: depth is capped (a deeply nested document is the cheap way to blow
 * the stack), every length is bounded by the input, and every failure returns
 * an error rather than a partial tree.
 */

#include "doe.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define JSON_MAX_DEPTH 64

typedef struct {
    const char *s;
    size_t      pos, len;
    doe_json_t *doc;
    int         depth;
    char       *err;
} jp_t;

static int parse_value(jp_t *p);

static int fail(jp_t *p, const char *what) {
    snprintf(p->err, DOE_ERR_SIZE, "%s at byte %zu", what, p->pos);
    return -1;
}

/* Append a node and return its index, or -1. */
static long node_new(jp_t *p, doe_json_type_t type) {
    if (p->doc->count == p->doc->cap) {
        size_t cap = p->doc->cap ? p->doc->cap * 2 : 64;
        /* Bound growth by the input: a node needs at least one byte of text,
         * so more nodes than bytes is impossible and means an internal bug. */
        if (cap > p->len + 64) cap = p->len + 64;
        if (cap <= p->doc->count) { fail(p, "document too complex"); return -1; }
        doe_json_node_t *n = realloc(p->doc->nodes, cap * sizeof *n);
        if (!n) { fail(p, "out of memory"); return -1; }
        p->doc->nodes = n;
        p->doc->cap = cap;
    }
    doe_json_node_t *n = &p->doc->nodes[p->doc->count];
    memset(n, 0, sizeof *n);
    n->type = type;
    n->first = n->next = -1;
    return (long)p->doc->count++;
}

static void skip_ws(jp_t *p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else break;
    }
}

/* Decode a JSON string into freshly allocated memory. */
static char *parse_string_raw(jp_t *p) {
    if (p->pos >= p->len || p->s[p->pos] != '"') { fail(p, "expected string"); return NULL; }
    p->pos++;

    /* The decoded form is never longer than the encoded one. */
    size_t start = p->pos;
    char *out = malloc(p->len - start + 1);
    if (!out) { fail(p, "out of memory"); return NULL; }

    size_t o = 0;
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->s[p->pos];
        if (c == '"') { p->pos++; out[o] = '\0'; return out; }
        if (c < 0x20) { free(out); fail(p, "control character in string"); return NULL; }
        if (c != '\\') { out[o++] = (char)c; p->pos++; continue; }

        p->pos++;
        if (p->pos >= p->len) { free(out); fail(p, "unterminated escape"); return NULL; }
        char e = p->s[p->pos++];
        switch (e) {
            case '"':  out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; break;
            case '/':  out[o++] = '/';  break;
            case 'b':  out[o++] = '\b'; break;
            case 'f':  out[o++] = '\f'; break;
            case 'n':  out[o++] = '\n'; break;
            case 'r':  out[o++] = '\r'; break;
            case 't':  out[o++] = '\t'; break;
            case 'u': {
                if (p->pos + 4 > p->len) { free(out); fail(p, "short \\u escape"); return NULL; }
                unsigned v = 0;
                for (int i = 0; i < 4; i++) {
                    char h = p->s[p->pos + i];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                    else { free(out); fail(p, "bad \\u escape"); return NULL; }
                }
                p->pos += 4;
                /* UTF-8 encode. Surrogates are passed through as-is: this
                 * toolkit's emitters only ever produce \u00XX for control
                 * characters, so a pair here means the document came from
                 * somewhere else and is not ours to interpret. */
                if (v < 0x80) out[o++] = (char)v;
                else if (v < 0x800) {
                    out[o++] = (char)(0xC0 | (v >> 6));
                    out[o++] = (char)(0x80 | (v & 0x3F));
                } else {
                    out[o++] = (char)(0xE0 | (v >> 12));
                    out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                    out[o++] = (char)(0x80 | (v & 0x3F));
                }
                break;
            }
            default: free(out); fail(p, "unknown escape"); return NULL;
        }
    }
    free(out);
    fail(p, "unterminated string");
    return NULL;
}

static int parse_number(jp_t *p, long idx) {
    const char *start = p->s + p->pos;
    char *end = NULL;
    double v = strtod(start, &end);
    if (end == start) return fail(p, "expected number");
    /* strtod accepts "nan" and "inf"; JSON does not. */
    for (const char *q = start; q < end; q++)
        if (*q == 'n' || *q == 'N' || *q == 'i' || *q == 'I')
            return fail(p, "NaN and Infinity are not JSON");
    p->pos += (size_t)(end - start);
    p->doc->nodes[idx].number = v;
    return 0;
}

static int parse_container(jp_t *p, long idx, char close) {
    int is_obj = (close == '}');
    p->pos++;                      /* consume the opener */
    long last = -1;

    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) return fail(p, "unterminated container");
        if (p->s[p->pos] == close) { p->pos++; return 0; }

        if (last >= 0) {
            if (p->s[p->pos] != ',') return fail(p, "expected ',' or close");
            p->pos++;
            skip_ws(p);
            if (p->pos < p->len && p->s[p->pos] == close)
                return fail(p, "trailing comma");
        }

        char *key = NULL;
        if (is_obj) {
            key = parse_string_raw(p);
            if (!key) return -1;
            skip_ws(p);
            if (p->pos >= p->len || p->s[p->pos] != ':') {
                free(key);
                return fail(p, "expected ':'");
            }
            p->pos++;
        }

        long child = -1;
        {
            skip_ws(p);
            long before = (long)p->doc->count;
            if (parse_value(p) != 0) { free(key); return -1; }
            child = before;
        }
        p->doc->nodes[child].key = key;   /* NULL for array elements */

        if (last < 0) p->doc->nodes[idx].first = child;
        else          p->doc->nodes[last].next = child;
        last = child;
        p->doc->nodes[idx].length++;
    }
}

static int parse_value(jp_t *p) {
    skip_ws(p);
    if (p->pos >= p->len) return fail(p, "unexpected end of input");
    if (++p->depth > JSON_MAX_DEPTH) { p->depth--; return fail(p, "nesting too deep"); }

    int rc = 0;
    char c = p->s[p->pos];
    if (c == '{' || c == '[') {
        long idx = node_new(p, c == '{' ? DOE_JSON_OBJECT : DOE_JSON_ARRAY);
        rc = (idx < 0) ? -1 : parse_container(p, idx, c == '{' ? '}' : ']');
    } else if (c == '"') {
        long idx = node_new(p, DOE_JSON_STRING);
        if (idx < 0) rc = -1;
        else {
            char *str = parse_string_raw(p);
            if (!str) rc = -1;
            else p->doc->nodes[idx].string = str;
        }
    } else if (c == 't' || c == 'f' || c == 'n') {
        const char *lit = (c == 't') ? "true" : (c == 'f') ? "false" : "null";
        size_t n = strlen(lit);
        if (p->pos + n > p->len || memcmp(p->s + p->pos, lit, n) != 0)
            rc = fail(p, "bad literal");
        else {
            long idx = node_new(p, (c == 'n') ? DOE_JSON_NULL : DOE_JSON_BOOL);
            if (idx < 0) rc = -1;
            else {
                p->doc->nodes[idx].number = (c == 't') ? 1.0 : 0.0;
                p->pos += n;
            }
        }
    } else {
        long idx = node_new(p, DOE_JSON_NUMBER);
        rc = (idx < 0) ? -1 : parse_number(p, idx);
    }

    p->depth--;
    return rc;
}

int doe_json_parse(const char *text, doe_json_t *out, char *err) {
    if (!text || !out || !err) return -1;
    memset(out, 0, sizeof *out);

    jp_t p = { .s = text, .pos = 0, .len = strlen(text),
               .doc = out, .depth = 0, .err = err };

    if (parse_value(&p) != 0) { doe_json_free(out); return -1; }
    skip_ws(&p);
    if (p.pos != p.len) {
        fail(&p, "trailing content after the document");
        doe_json_free(out);
        return -1;
    }
    return 0;
}

void doe_json_free(doe_json_t *doc) {
    if (!doc) return;
    for (size_t i = 0; i < doc->count; i++) {
        free(doc->nodes[i].key);
        free(doc->nodes[i].string);
    }
    free(doc->nodes);
    memset(doc, 0, sizeof *doc);
}

const doe_json_node_t *doe_json_get(const doe_json_t *doc,
                                    const doe_json_node_t *obj, const char *key) {
    if (!doc || !obj || obj->type != DOE_JSON_OBJECT) return NULL;
    for (long i = obj->first; i >= 0; i = doc->nodes[i].next) {
        if (doc->nodes[i].key && strcmp(doc->nodes[i].key, key) == 0)
            return &doc->nodes[i];
    }
    return NULL;
}

const doe_json_node_t *doe_json_at(const doe_json_t *doc,
                                   const doe_json_node_t *arr, size_t index) {
    if (!doc || !arr || arr->type != DOE_JSON_ARRAY) return NULL;
    long i = arr->first;
    for (size_t n = 0; i >= 0; n++, i = doc->nodes[i].next)
        if (n == index) return &doc->nodes[i];
    return NULL;
}

double doe_json_number_of(const doe_json_t *doc, const doe_json_node_t *obj,
                          const char *key, double fallback) {
    const doe_json_node_t *n = doe_json_get(doc, obj, key);
    return (n && n->type == DOE_JSON_NUMBER) ? n->number : fallback;
}

const char *doe_json_string_of(const doe_json_t *doc, const doe_json_node_t *obj,
                               const char *key, const char *fallback) {
    const doe_json_node_t *n = doe_json_get(doc, obj, key);
    return (n && n->type == DOE_JSON_STRING) ? n->string : fallback;
}
