# What every step produced

The whole study, committed — so you can read the results without building or
running anything. Every file here was produced by
[`../regenerate.sh`](../regenerate.sh), which runs exactly the commands in
[the walkthrough](../README.md).

**[`study.html`](study.html) is the one to open first** — the finished report,
self-contained, with a Pareto chart of effects.

| step | files | what to look at |
|---|---|---|
| 1. screen | `1-screen-*` | `analysis.txt` — temp, butter and time do the work; eggs do not |
| 2. attribute | `2-attribute-*` | `analysis.txt` — `ST` well above `S1` means interactions |
| 3. resolve | `3-grid-temp-x-time.txt` | the interaction, measured: 68% of the larger main effect |
| | `3-ofat-butter.txt` | one factor swept on its own, to confirm it is real |
| 4. optimize | `4-optimize-analysis.txt` | level means and a recommended recipe |
| | `4-confirm-*.txt` | the prediction, and what happens when you test it |
| 5. peak | `5-rsm-analysis.txt` | 369 °F — a temperature no design contained |
| 6. robust | `6-robust-analysis.txt` | where the robust and best-average answers part company |
| 7. trade-offs | `7-desirability.csv` | taste, cost and time folded into one column |
| | `7-pareto-front.csv` | the recipes you cannot improve without giving something up |
| 8. report | `study.html` | all of it, on one page |

`*-design.csv` is what to run; `*-results.csv` is what came back. In a real
study you would run the design yourself; here `../bake.sh` does it with the
`model.sh` stand-in.

## These files are tested

`examples/tests/test_examples.sh` regenerates them and fails the build if
what the tools produce no longer matches what is committed here. A worked
example that quietly stops matching the software is worse than none, so this
directory is a fixture, not documentation that rots.

Regenerate after an intentional change:

```sh
cd examples/cookies && ./regenerate.sh
```
