# analyze — tools that consume results

Everything here reads a results CSV (or a `.space` plus responses) and computes
or presents. **Nothing in this directory generates a design or spends a model
run** — that is what `screen/`, `attribute/`, `resolve/` and `optimize/` are
for. `EXPANSION.md`'s E1 tier states the rule for its members directly: *"No
new sampling; every item consumes the existing Morris/Sobol responses."*

| Tool | Purpose | Status |
|---|---|---|
| [`pareto`](pareto/) | Non-dominated frontier over several metric columns — a stateless filter *and* a `.front` store that accumulates across experiment batches. | **Built** (E1) |
| `regress` | SRC/SRRC + R² — the *direction* of each effect, which variance shares cannot give. | E1 |
| `uq` | Output distribution summary: mean, variance, P5/P50/P95, histogram + empirical CDF. | E1 |
| `report` | Standalone unified HTML/SVG: Morris μ\*–σ scatter, Sobol Sᵢ/S_Tᵢ tornado bars, Taguchi main-effects + S/N. Also callable as `robust report`. | M4 |

Filters emit the same results-CSV dialect they consume, so they compose in
pipelines and every downstream tool works unchanged:

```sh
desire --max yield --min cost results.csv | sobol analyze model.space - --metric desirability
pareto --max yield --min cost results.csv > front.csv
```

All build on `core/libdoe`. See [../EXPANSION.md](../EXPANSION.md).
