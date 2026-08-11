# report — a self-contained HTML dashboard

Renders the `--json` output of the analyze stages as one page: a **Pareto chart
of effects** per document, with the ranked numbers beside it.

```sh
morris analyze model.space results.csv --json > morris.json
sobol  analyze model.space results.csv --json > sobol.json
report morris.json sobol.json --html study.html
```

It **consumes** documents rather than recomputing anything, which is the point:
the analyze tools own the arithmetic, and this owns the presentation. Each
document carries a `schema`, so a version this build does not understand is
refused instead of charted from fields that moved.

| document | ranked by |
|---|---|
| `morris analyze --json` | μ\* (importance) |
| `sobol analyze --json` | S_T (total index) |
| `taguchi analyze/effects --json` | range (max − min of level means) |

## The Pareto chart of effects

Contribution bars largest-first, with the cumulative share as a line and the
80% mark drawn in. It is the "vital few vs trivial many" read of a screening
result, and the one place a keep/drop decision becomes visible rather than
arithmetic — you can see where the mass runs out and whether your cut lands
somewhere the data supports.

## Self-contained, on purpose

No CDN, no fonts, no scripts, no network. The file opens from a flash drive in
ten years, which is the only report format worth writing for an experiment you
may have to defend. It also means nothing about your study leaks to whoever
hosts a resource.

Factor names are HTML-escaped: they come from a `.space` or `.tgu` a person
wrote, and a name is not markup.

## Inputs are untrusted

`report` reads files by path, so the JSON reader (`core/src/json_parse.c`) is
strict — no comments, no trailing commas, no `NaN`/`Infinity`, a nesting cap,
and a size cap. It is fuzzed under ASan/UBSan as part of `make fuzz`.
