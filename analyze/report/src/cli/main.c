/*
 * report — a self-contained HTML dashboard from the tools' --json documents.
 *
 * Consumes what the analyze stages emit rather than recomputing anything:
 *
 *     morris analyze model.space r.csv --json > morris.json
 *     sobol  analyze model.space r.csv --json > sobol.json
 *     report morris.json sobol.json --html study.html
 *
 * That composition is the point. The documents carry a `schema`, so this can
 * refuse one it does not understand instead of drawing a chart from fields
 * that moved.
 *
 * The centrepiece is the PARETO CHART OF EFFECTS: contribution bars ranked
 * largest-first with a cumulative-share line. It is the classic "vital few vs
 * trivial many" read of a screening result, and it is the one place a
 * keep/drop decision becomes visible rather than arithmetic — you can see
 * where the mass runs out.
 *
 * No external resources: no CDN, no fonts, no scripts. The file works from a
 * flash drive in ten years, which is the only report format worth writing for
 * an experiment you may need to defend.
 */

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_MAX_BYTES (16u * 1024u * 1024u)   /* refuse absurd inputs */

/* One ranked series: a Pareto chart's worth of data, whatever produced it. */
typedef struct {
    char   title[128];
    char   metric[128];
    char   subtitle[192];
    char   value_label[64];
    char **names;
    double *values;
    size_t  count;
} series_t;

static void series_free(series_t *s) {
    for (size_t i = 0; i < s->count; i++) free(s->names[i]);
    free(s->names);
    free(s->values);
    memset(s, 0, sizeof *s);
}

static char *read_file(const char *path, char *err) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, DOE_ERR_SIZE, "cannot open '%s'", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); snprintf(err, DOE_ERR_SIZE, "cannot seek '%s'", path); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || (unsigned long)sz > REPORT_MAX_BYTES) {
        fclose(f);
        snprintf(err, DOE_ERR_SIZE, "'%s' is empty or larger than %u bytes", path, REPORT_MAX_BYTES);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); snprintf(err, DOE_ERR_SIZE, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Sort a series descending by value, so the Pareto reading is the ranking. */
static void series_sort(series_t *s) {
    for (size_t i = 0; i < s->count; i++)
        for (size_t j = i + 1; j < s->count; j++)
            if (s->values[j] > s->values[i]) {
                double v = s->values[i]; s->values[i] = s->values[j]; s->values[j] = v;
                char *n = s->names[i];   s->names[i] = s->names[j];   s->names[j] = n;
            }
}

static int series_alloc(series_t *s, size_t n, char *err) {
    s->names  = calloc(n, sizeof *s->names);
    s->values = calloc(n, sizeof *s->values);
    if (!s->names || !s->values) {
        snprintf(err, DOE_ERR_SIZE, "out of memory");
        series_free(s);
        return -1;
    }
    return 0;
}

/*
 * Turn one document into a ranked series. Which field carries "importance"
 * depends on the tool, and that mapping is the only tool-specific knowledge
 * here -- everything downstream draws the same chart.
 */
static int series_from_doc(const doe_json_t *doc, series_t *out, char *err) {
    const doe_json_node_t *root = &doc->nodes[0];
    if (root->type != DOE_JSON_OBJECT) {
        snprintf(err, DOE_ERR_SIZE, "document root is not an object");
        return -1;
    }

    double schema = doe_json_number_of(doc, root, "schema", 1);
    if (schema > 1) {
        snprintf(err, DOE_ERR_SIZE,
                 "document reports schema %.0f; this build understands 1", schema);
        return -1;
    }

    const char *tool = doe_json_string_of(doc, root, "tool", "");
    const char *cmd  = doe_json_string_of(doc, root, "command", "");
    memset(out, 0, sizeof *out);
    snprintf(out->metric, sizeof out->metric, "%s",
             doe_json_string_of(doc, root, "metric", "response"));

    const doe_json_node_t *arr = NULL;
    const char *name_key = NULL, *value_key = NULL;

    if (strcmp(tool, "morris") == 0 && strcmp(cmd, "analyze") == 0) {
        arr = doe_json_get(doc, root, "factors");
        name_key = "factor"; value_key = "mu_star";
        snprintf(out->title, sizeof out->title, "Morris screening");
        snprintf(out->value_label, sizeof out->value_label, "mu* (importance)");
        snprintf(out->subtitle, sizeof out->subtitle, "%.0f trajectories, %.0f runs",
                 doe_json_number_of(doc, root, "trajectories", 0),
                 doe_json_number_of(doc, root, "runs", 0));
    } else if (strcmp(tool, "sobol") == 0 && strcmp(cmd, "analyze") == 0) {
        arr = doe_json_get(doc, root, "indices");
        name_key = "factor"; value_key = "st";
        snprintf(out->title, sizeof out->title, "Sobol attribution");
        snprintf(out->value_label, sizeof out->value_label, "ST (total index)");
        snprintf(out->subtitle, sizeof out->subtitle, "N=%.0f, %.0f runs, sampler: %s",
                 doe_json_number_of(doc, root, "samples", 0),
                 doe_json_number_of(doc, root, "runs", 0),
                 doe_json_string_of(doc, root, "sampler", "?"));
    } else if (strcmp(tool, "taguchi") == 0 &&
               (strcmp(cmd, "analyze") == 0 || strcmp(cmd, "effects") == 0)) {
        arr = doe_json_get(doc, root, "effects");
        name_key = "factor"; value_key = "range";
        snprintf(out->title, sizeof out->title, "Taguchi main effects");
        snprintf(out->value_label, sizeof out->value_label, "range (max - min of level means)");
        snprintf(out->subtitle, sizeof out->subtitle, "%.0f factors",
                 doe_json_number_of(doc, root, "factor_count", 0));
    } else {
        snprintf(err, DOE_ERR_SIZE,
                 "don't know how to chart tool '%s' command '%s'", tool, cmd);
        return -1;
    }

    if (!arr || arr->type != DOE_JSON_ARRAY || arr->length == 0) {
        snprintf(err, DOE_ERR_SIZE, "%s document has no data to chart", tool);
        return -1;
    }
    if (series_alloc(out, arr->length, err) != 0) return -1;

    size_t i = 0;
    for (long ni = arr->first; ni >= 0; ni = doc->nodes[ni].next, i++) {
        const doe_json_node_t *item = &doc->nodes[ni];
        const char *nm = doe_json_string_of(doc, item, name_key, "?");
        out->names[i] = malloc(strlen(nm) + 1);
        if (!out->names[i]) {
            snprintf(err, DOE_ERR_SIZE, "out of memory");
            series_free(out);
            return -1;
        }
        strcpy(out->names[i], nm);
        out->values[i] = doe_json_number_of(doc, item, value_key, 0.0);
        out->count++;
    }
    series_sort(out);
    return 0;
}

/* ---- rendering ---------------------------------------------------------- */

static void emit_head(FILE *f) {
    fprintf(f,
        "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>robust — study report</title>\n<style>\n"
        ":root{color-scheme:light dark}\n"
        "body{font:14px/1.5 system-ui,-apple-system,Segoe UI,sans-serif;"
        "max-width:900px;margin:2rem auto;padding:0 1rem;"
        "background:#fff;color:#111}\n"
        "@media(prefers-color-scheme:dark){body{background:#0d1117;color:#c9d1d9}"
        "th{border-color:#30363d!important}td{border-color:#21262d!important}}\n"
        "h1{font-size:1.5rem;margin-bottom:.2rem}\n"
        "h2{font-size:1.15rem;margin-top:2.5rem}\n"
        ".sub{opacity:.7;margin-top:0}\n"
        "table{border-collapse:collapse;width:100%%;margin:1rem 0}\n"
        "th,td{text-align:left;padding:.35rem .5rem;border-bottom:1px solid #ddd}\n"
        "td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}\n"
        "figure{margin:1rem 0}\n"
        "figcaption{opacity:.7;font-size:.9rem;margin-top:.4rem}\n"
        "</style></head><body>\n");
}

/*
 * The Pareto chart of effects: bars descending, plus the cumulative share as a
 * line with its own right-hand axis. Inline SVG, no script.
 */
static void emit_pareto(FILE *f, const series_t *s) {
    const double W = 860, H = 300, L = 130, R = 46, T = 16, B = 54;
    double plot_w = W - L - R, plot_h = H - T - B;

    double total = 0.0, top = 0.0;
    for (size_t i = 0; i < s->count; i++) {
        total += s->values[i];
        if (s->values[i] > top) top = s->values[i];
    }
    if (top <= 0.0) top = 1.0;

    double slot = plot_w / (double)s->count;
    double bw = slot * 0.62;

    fprintf(f, "<figure>\n<svg viewBox=\"0 0 %.0f %.0f\" width=\"100%%\" "
               "role=\"img\" aria-label=\"Pareto chart of effects\">\n", W, H);

    /* axes */
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" "
               "stroke=\"currentColor\" stroke-opacity=\".35\"/>\n",
            L, T + plot_h, L + plot_w, T + plot_h);
    fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" "
               "stroke=\"currentColor\" stroke-opacity=\".35\"/>\n", L, T, L, T + plot_h);

    /* bars */
    for (size_t i = 0; i < s->count; i++) {
        double h = (s->values[i] / top) * plot_h;
        if (h < 0.0) h = 0.0;
        double x = L + slot * (double)i + (slot - bw) / 2.0;
        char *nm = doe_html_escape(s->names[i]);
        fprintf(f, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" "
                   "fill=\"#3b82f6\" fill-opacity=\".85\"><title>%s: %.6g</title></rect>\n",
                x, T + plot_h - h, bw, h, nm ? nm : "", s->values[i]);
        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"11\" "
                   "text-anchor=\"end\" transform=\"rotate(-35 %.1f %.1f)\" "
                   "fill=\"currentColor\" fill-opacity=\".8\">%s</text>\n",
                x + bw / 2.0, T + plot_h + 14, x + bw / 2.0, T + plot_h + 14,
                nm ? nm : "");
        doe_free(nm);
    }

    /* cumulative share line */
    if (total > 0.0) {
        fprintf(f, "<polyline fill=\"none\" stroke=\"#f59e0b\" stroke-width=\"2\" points=\"");
        double cum = 0.0;
        for (size_t i = 0; i < s->count; i++) {
            cum += s->values[i];
            double y = T + plot_h - (cum / total) * plot_h;
            fprintf(f, "%.1f,%.1f ", L + slot * ((double)i + 0.5), y);
        }
        fprintf(f, "\"/>\n");

        cum = 0.0;
        for (size_t i = 0; i < s->count; i++) {
            cum += s->values[i];
            double y = T + plot_h - (cum / total) * plot_h;
            fprintf(f, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"3\" fill=\"#f59e0b\">"
                       "<title>cumulative %.1f%%</title></circle>\n",
                    L + slot * ((double)i + 0.5), y, 100.0 * cum / total);
        }
        /* the 80% line: where "vital few" is conventionally drawn */
        double y80 = T + plot_h - 0.8 * plot_h;
        fprintf(f, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" "
                   "stroke=\"currentColor\" stroke-opacity=\".3\" stroke-dasharray=\"4 4\"/>\n",
                L, y80, L + plot_w, y80);
        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"10\" fill=\"currentColor\" "
                   "fill-opacity=\".6\">80%%</text>\n", L + plot_w + 4, y80 + 3);
    }

    /* left axis label + max tick */
    {
        char *lbl = doe_html_escape(s->value_label);
        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"11\" fill=\"currentColor\" "
                   "fill-opacity=\".7\">%.6g</text>\n", 6.0, T + 10, top);
        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"11\" fill=\"currentColor\" "
                   "fill-opacity=\".7\">%s</text>\n", 6.0, T + plot_h, lbl ? lbl : "");
        doe_free(lbl);
    }

    fprintf(f, "</svg>\n<figcaption>Bars: contribution, largest first. "
               "Line: cumulative share. Where the line flattens is where the "
               "remaining factors stop paying for themselves.</figcaption>\n</figure>\n");
}

static void emit_table(FILE *f, const series_t *s) {
    double total = 0.0;
    for (size_t i = 0; i < s->count; i++) total += s->values[i];

    char *lbl = doe_html_escape(s->value_label);
    fprintf(f, "<table><thead><tr><th>#</th><th>factor</th>"
               "<th class=\"n\">%s</th><th class=\"n\">share</th>"
               "<th class=\"n\">cumulative</th></tr></thead><tbody>\n",
            lbl ? lbl : "value");
    doe_free(lbl);

    double cum = 0.0;
    for (size_t i = 0; i < s->count; i++) {
        cum += s->values[i];
        char *nm = doe_html_escape(s->names[i]);
        fprintf(f, "<tr><td class=\"n\">%zu</td><td>%s</td><td class=\"n\">%.6g</td>"
                   "<td class=\"n\">%.1f%%</td><td class=\"n\">%.1f%%</td></tr>\n",
                i + 1, nm ? nm : "", s->values[i],
                total > 0 ? 100.0 * s->values[i] / total : 0.0,
                total > 0 ? 100.0 * cum / total : 0.0);
        doe_free(nm);
    }
    fprintf(f, "</tbody></table>\n");
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <doc.json> [<doc.json> ...] [--html PATH]\n"
        "\n"
        "  Renders the --json output of morris/sobol/taguchi analyze as a\n"
        "  self-contained HTML page: a Pareto chart of effects per document,\n"
        "  with the ranked numbers beside it.\n"
        "\n"
        "  --html PATH   where to write (default: stdout)\n"
        "\n"
        "  Produce the inputs with, e.g.:\n"
        "    morris analyze model.space results.csv --json > morris.json\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }

    const char *out_path = NULL;
    const char *inputs[64];
    int n_inputs = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--html") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            if (n_inputs == (int)(sizeof inputs / sizeof *inputs)) {
                fprintf(stderr, "Error: too many input documents (max %zu)\n",
                        sizeof inputs / sizeof *inputs);
                return 2;
            }
            inputs[n_inputs++] = argv[i];
        }
    }
    if (n_inputs == 0) { fprintf(stderr, "Error: no input documents\n"); usage(argv[0]); return 2; }

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { fprintf(stderr, "Error: cannot write '%s'\n", out_path); return 1; }
    }

    emit_head(out);
    fprintf(out, "<h1>Study report</h1>\n<p class=\"sub\">%d document%s</p>\n",
            n_inputs, n_inputs == 1 ? "" : "s");

    int rc = 0;
    for (int i = 0; i < n_inputs; i++) {
        char err[DOE_ERR_SIZE];
        char *text = read_file(inputs[i], err);
        if (!text) { fprintf(stderr, "Error: %s\n", err); rc = 1; break; }

        doe_json_t doc;
        if (doe_json_parse(text, &doc, err) != 0) {
            fprintf(stderr, "Error: %s: %s\n", inputs[i], err);
            free(text);
            rc = 1;
            break;
        }
        free(text);

        series_t s;
        if (series_from_doc(&doc, &s, err) != 0) {
            fprintf(stderr, "Error: %s: %s\n", inputs[i], err);
            doe_json_free(&doc);
            rc = 1;
            break;
        }
        doe_json_free(&doc);

        char *t = doe_html_escape(s.title);
        char *m = doe_html_escape(s.metric);
        char *sub = doe_html_escape(s.subtitle);
        fprintf(out, "<h2>%s — %s</h2>\n<p class=\"sub\">%s</p>\n",
                t ? t : "", m ? m : "", sub ? sub : "");
        doe_free(t); doe_free(m); doe_free(sub);

        emit_pareto(out, &s);
        emit_table(out, &s);
        series_free(&s);
    }

    fprintf(out, "</body></html>\n");
    if (out != stdout) fclose(out);
    return rc;
}
