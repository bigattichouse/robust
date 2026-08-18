# analyze — tools that consume results

Everything here reads a results CSV (or a `.space` plus responses) and computes
or presents. **Nothing in this directory generates a design or spends a model
run** — that is what `screen/`, `attribute/`, `resolve/` and `optimize/` are
for. `EXPANSION.md`'s E1 tier states the rule for its members directly: *"No
new sampling; every item consumes the existing Morris/Sobol responses."*

| Tool | Purpose | Status |
|---|---|---|
| [`pareto`](pareto/) | Non-dominated frontier over several metric columns — a stateless filter *and* a `.front` store that accumulates across experiment batches. | **Built** (E1) |
| [`regress`](regress/) | **Built.** SRC/SRRC + R² — the *direction* of each effect, which variance shares cannot give. `--ranks` for a monotone-but-curved relationship. | E1 ✓ |
| [`uq`](uq/) | **Built.** Output distribution: mean, sd, P05/P25/P50/P75/P95, histogram with cumulative CDF, and a skew note when the mean misleads. | E1 ✓ |
| `report` | Standalone unified HTML/SVG: Morris μ\*–σ scatter, Sobol Sᵢ/S_Tᵢ tornado bars, Taguchi main-effects + S/N. Also callable as `robust report`. | M4 |

Filters emit the same results-CSV dialect they consume, so they compose in
pipelines and every downstream tool works unchanged:

```sh
desire --max yield --min cost results.csv | sobol analyze model.space - --metric desirability
pareto --max yield --min cost results.csv > front.csv
```

**`--json` is the machine-readable contract**, here and on the two analyze
stages outside this directory (`morris analyze`, `sobol analyze`). Text output
is a display and will keep changing; parse the JSON, and check its `schema`
number. Every document in the suite leads with `tool`, `command` and `schema`,
so a consumer can identify what it is holding before it reads a field —
`regress` and `uq` were outside that until 2026-08-18, which meant a consumer
checking `schema` had to special-case exactly the two documents it could not
identify. `core/src/json.c` provides the shared escape and number formatting —
use `doe_json_string` / `doe_json_number` rather than interpolating a name or
a double into a format string, because a factor name may contain a quote and a
non-finite double has no JSON literal.

All build on `core/libdoe` — including how they read results. `doe_csv_read_metric`
serves a tool that holds a design ("one metric, keyed by run id"), and
`doe_table` serves one that needs several columns by name: `regress` wants one
per factor plus the metric, `desire` wants every objective plus each row back
verbatim to echo. Both used to carry their own line splitters, and desire's
refused a file past 100000 rows — a ceiling belonging to the reader rather than
the data. If you add a tool here, read through core rather than writing a
fourth parser.

See [../EXPANSION.md](../EXPANSION.md).
