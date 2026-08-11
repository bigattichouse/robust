# Expansion — Methods & Post-Method Analysis Roadmap

*What comes after the funnel. Companion to DESIGN.md (build plan, M0–M7) and
SECURITY.md (hardening discipline). Written 2026-07-16.*

The funnel today answers *"what matters?"* (Morris), *"how much, and which
interactions?"* (Sobol), and hands survivors to the bench (taguchi). This plan
adds the questions it can't yet answer — *"in which direction?"*, *"have I
sampled enough?"*, *"what does the output distribution look like?"*, *"which
pair interacts?"*, *"what setting is optimal?"*, *"is it robust to noise?"*,
*"what trade-off am I making between objectives?"* — in order of value per
effort.

Every addition inherits the house rules: C99 `-Werror`, valgrind/ASan-clean,
deterministic from the `.space` seed, the clean-error invariant of SECURITY.md,
and validation against a closed-form reference before it ships.

**Additive only.** New capability lands as a *new* tool or a *new* mode
alongside what exists — never as a replacement for it. If a method turns out to
subsume another, both stay: users have processes, scripts and review
requirements built on the current tools, and a better estimator is not a reason
to break them. Where two tools answer the same question, the docs say which is
cheaper and which is more informative, and the user chooses. Nothing is retired
from the shipped surface by this roadmap.

**Claims get validated too, not just code.** The closed-form rule applies to
assertions taken from papers as much as to C. `make validate`
([`validation/`](validation/README.md)) reproduces the published screening
results this roadmap leans on against analytic ground truth — and has already
corrected two of them. Items below cite it where a decision rests on a measured
result rather than on a source's summary.

**Delivery shape — standalone Unix tools.** Each capability ships as its own
small binary in the DESIGN.md mold: read a `.space` and/or a results CSV (file
or stdin), write text/CSV/JSON to stdout, exit nonzero with a clean error on
stderr. Filters (`pareto`, `desire`) emit the same CSV dialect they consume, so
they compose in pipelines and every downstream tool works unchanged. `robust`
and `report` are *consumers* of these tools' output, never the only door to a
feature. Analysis flags land on an existing binary only when the math is
inseparable from that tool's design (e.g. DGSM needs the Morris trajectories).

---

## E0. Prerequisites (do first, small)

- ~~**`second_order:` honesty.**~~ ✓ **RESOLVED 2026-08-06 by implementing it.**
  It first became an explicit rejection (better than a silent no-op), and is
  now real: `sobol` computes second-order indices when the flag is set, at
  `N(k+2+k(k-1)/2)` runs. Validated against the g-function's exact
  decomposition — `V_ij = V_i·V_j` for a product function — in `make validate`
  check F, worst error 0.0007 across all pairs (0.005 before the QR sequence
  landed).
- ~~**M5's Joe-Kuo sequence has a trap, now documented.**~~ ✓ **RESOLVED
  2026-08-07 by building it, trap intact.** Saltelli et al. (2010) §5.1 p.263
  requires that `A` and `B` be the **left and right halves of a single
  2k-dimensional** quasi-random sequence — not two k-dimensional draws taken in
  sequence, which is what LHS correctly does and what a QR sequence must *not*
  do (it is deterministic, so restarting reproduces points and continuing
  correlates them). `doe_sample_sobol_dims()` exists precisely so the two
  halves can be expressed without materialising an N×2k matrix, and
  `test_qr_design_uses_one_sequence` pins the construction against the shipped
  design rather than against the comment. The warning comment stayed where it
  was, rewritten to say what was done and why the shape below it is still
  correct for LHS.
- **Finish M6–M7** (DESIGN.md §11): confirmation checker, Python bindings.
  M5 is complete. Several E-items below build on M5/M6, noted per item.
  (M7's CI half shipped 2026-07-16.)

---

## E1. Post-run analysis pack — reuse the runs you already paid for

*No new sampling; every item consumes the existing Morris/Sobol responses.*

| Item | What it adds | Sketch | Validation |
|---|---|---|---|
| **`regress`** — SRC/SRRC + R² ✓ **BUILT** | *Direction* of each effect (raise `temp` → response up or down?) — the one thing variance shares can't say. R² doubles as a trust diagnostic: ≈1 ⇒ the linear story suffices; low ⇒ the variance-based indices were needed. | Standalone: `regress <file.space> <results.csv>` → ranked coefficient table (`--json` for machines). OLS on the standardized sample matrix; SRRC = same on ranks. `core/stats.c` grows the small least-squares core. | Exact on a known linear model (`y = 10x0 + 5x1`): SRC ∝ coefficients, R² = 1. |
| **`uq`** — output distribution summary ✓ **BUILT** | What the output *distribution* looks like, not just who drives it: mean, variance, P5/P50/P95, histogram + empirical CDF. | Standalone: `uq <results.csv>` (or stdin) → text summary; `--svg` emits the histogram/CDF panel that `report` embeds. Sort + percentiles + binning. | Percentiles of a uniform-driven linear model match closed form. |
| **Morris μ\* bootstrap CIs** ✓ **BUILT** | Error bars on the keep/drop cut, so `--keep-fraction` decisions aren't made on point estimates. | Flag on `morris analyze` — inseparable from the trajectory structure (the CI machinery already exists in `sobol`). | CI shrinks ~1/√r; covers the analytic μ\* on a linear model. |
| **Pareto chart of effects** ✓ **BUILT** | The classic DOE view: contribution bars ranked largest-first with a cumulative-share line — the "vital few vs trivial many" read of μ\* or Sᵢ at a glance. | Ships in the standalone `report` tool, which consumes the `--json` documents rather than recomputing. | Cumulative line reaches 100%; bar order matches the ranked indices exactly. Both asserted in `analyze/report/tests`. |
| **`pareto`** — frontier filter **and store** ✓ **BUILT** | The trade-off set across several metric columns (yield ↑ vs cost ↓), plus a `.front` file that accumulates a frontier across experiment batches so it survives as a study artifact instead of being recomputed and lost. *Moved here from E7:* the filter depends on neither E1's least-squares core nor E4's RSM. O(n²) dominance on a CSV is the cheapest binary in the roadmap and was scheduled last. | Spec: [`spec/pareto.bp`](spec/pareto.bp). `.front` is itself a valid results CSV (comment preamble), so the whole pipeline composes and `pareto list` is only a convenience. | Analytic front `y1 = x`, `y2 = 1 − x²` — including 500 planted interior points that must *all* be dropped, since the all-pass case cannot detect an over-permissive filter. Merge is idempotent and order-independent, and must equal the one-shot filter over concatenated batches. |
| **Cut-gap diagnostic** ✓ **BUILT** | Report the gap in μ\* at the keep/drop boundary, and warn when the cut falls inside a near-tie. | A few lines in `morris analyze`; `gap_at_cut` + `cut_is_tie` in `--json`. | Fires on a 1% gap, silent on a 5× gap. |
| **`analyze --json`** ✓ **BUILT** | A machine-readable contract for `morris analyze` and `sobol analyze`, separate from the human tables so a display change stops being an API change. Schema-versioned. | `--json` on both, mirroring `regress`/`uq`. Carries the cut-gap keys above, which the rows here assumed existed before they did. | A real parser loads the document; the table stays positionally parseable in the same suites. |

Deliverable: three new binaries (`regress`, `uq`, `pareto`) under `analyze/`, CI
columns and the cut-gap diagnostic on `morris analyze`, `--json` on the two
analyze stages that lacked it, and the Pareto panel in `report`. The funnel also gains a Pareto-style keep rule — `--keep-share S`
keeps top factors until cumulative μ\*-share ≥ S (an 80/20 cut, vs
`--keep-fraction`'s point threshold). **This is the highest value-per-effort
tier.**

**Why `--keep-share` and the cut-gap diagnostic are not cosmetic.**
`make validate` check C measured μ\*'s agreement with the analytic total index
on the 12-factor g-function: the top-5 keep decision is 85% correct at r=5 and
**100% from r=20 onward**, while the top-3 decision sits near a coin flip
(35–55%) and **never improves, even at 2600 runs**. The cause is not budget —
the top-3 boundary falls between two factors whose true S_T differ by 1.0%,
which no amount of sampling resolves, while the top-5 boundary falls in a 5.3×
gap. A fixed-count cut is therefore trustworthy or not depending on where it
lands, which the user cannot know in advance and the tool can.

## E2. Convergence — "have I sampled enough?"

Sequential sampling with a CI-width target: `--target-ci W` keeps doubling
`samples:` (Sobol) or `trajectories:` (Morris) until every bootstrap CI is
narrower than `W` or a hard cap is hit (caps per SECURITY.md H1). Removes the
guess-a-number step from `.space` authoring. Deterministic: doubling reuses the
seed stream, so a converged run is still regenerable from the file alone.

Validation: on Ishigami, the reported N at convergence matches the N found by
manual doubling; capped runs error cleanly.

## E3. New sensitivity methods

| Item | When it earns its place | Sketch | Validation |
|---|---|---|---|
| **PAWN (moment-independent)** | Output skewed/heavy-tailed, where variance misleads. | Conditional-vs-unconditional empirical CDF distances via KS statistics — no density estimation, very C-friendly. New peer tool `pawn/` sharing the `common` sampler. | Published PAWN values for Ishigami. |
| **`morris --groups`** — group screening ✓ **BUILT** | Screens *groups* of factors at `r(G+1)` cost instead of `r(k+1)`, then splits survivors and re-screens (`morris bifurcate`). **Reach is 1024 factors** (`DOE_MAX_FACTORS`), 12.8× cheaper than per-factor screening at that size. Needs `trajectories >= 20`; below that a group holding equal-and-opposite factors can be dropped silently, and `morris bifurcate` warns. Reaches factor counts in the thousands, which is the gap EXPANSION_NOTE.md was opened to address. Unlike sequential bifurcation it assumes **neither monotonicity nor known signs**, because the measure is the absolute value of the *group* effect — so a group holding two important factors with opposing signs still registers instead of being silently discarded. | Spec: [`spec/morris-groups.bp`](spec/morris-groups.bp). A `groups:` section in `.space` (must partition the factors); new mode on the existing binary, per-factor path untouched. Source: Campolongo et al. 2007 §3.3. | **Already reproduced** in `make validate` check B: all three of the paper's Table 1 cases, 9 factors in 3 groups at 40 runs where per-factor needs 100. Plus: singleton groups must equal per-factor μ\* exactly, and `y = 10x₀ − 10x₁` in one group must *not* cancel. |
| **DGSM** | Cheap upper bounds on total indices from Morris-style sampling — a bridge between the two existing tools. | Mean-square elementary effects with the Poincaré constant; lands inside `morris analyze --dgsm`. | Bound property: DGSM-derived bound ≥ S_Tᵢ on Ishigami. |
| **eFAST** | Independent variance-based estimator to cross-check Sobol. Optional — largely duplicates what exists. | Frequency-assigned sinusoidal sampling + spectrum sums (direct sums; no FFT dependency). | Ishigami first-order indices. |

### Where μ\* stops, and why `sobol` stays

Campolongo, Cariboni & Saltelli (2007) §6 concludes that "the use of the EE
sensitivity measure μ\* as a proxy of the variance-based total index is
acceptable and convenient." `make validate` check A pins exactly how far that
goes, on the 12-factor g-function at the paper's own budget (r=10, 130 runs):

| Measured | Value |
|---|---|
| Spearman(μ\*, S_T) | **0.923** |
| spread of the ratio μ\*/S_T across factors | **118.6×** |

μ\* **ranks** like the total index. It does not give its **magnitude** — with
the ratio varying 119× there is no constant that recovers S_T's value, so the
`morris analyze --st-estimate` flag speculated about in EXPANSION_NOTE.md §5 is
not buildable and is dropped.

The practical reading: **the `sobol` stage is optional when you only need a
ranking or a keep/drop list, and required when you need variance shares** —
budgeting, "what fraction of output variance does this factor own?", additivity
checks via `Σ Sᵢ ≈ 1`, or interaction detection via `S_T − Sᵢ`. It is also
simply required whenever a process, protocol or review standard calls for
variance attribution, which is reason enough on its own. Per the additive rule
above, nothing here retires `sobol`; the docs just say when the cheaper path
suffices.

## E4. RSM stage — from "who matters" to "what setting is best"

Central composite (or Box–Behnken) design on the 2–3 funnel survivors →
quadratic fit → stationary-point + canonical analysis → predicted optimum.
New tool `rsm/`; `robust funnel --optimize` chains it as the final stage.
Depends on the E1 least-squares core and pairs naturally with M6's `grid`.

Validation: recovers the known optimum of a synthetic quadratic bowl to
tolerance; degenerate fits (saddle, rank-deficient) produce clean errors.

## E5. Robust parameter design — noise factors ✓ **BUILT**

The classic Taguchi "robust" the repo is named for: a `noise:` section in
`.space`, crossed (inner × outer) designs, and S/N ratios
(larger-better / smaller-better / nominal-best) so the recommendation is
*"the setting least sensitive to what you can't control"*, not just the best
mean. Touches the `.space` grammar (parser hardening rules apply — see
SECURITY.md H6/H7), the runner, and the taguchi bench leg.

Validation: on a model with a known control×noise interaction, the S/N-optimal
setting differs from the mean-optimal one exactly as constructed.

## E6. Surrogates & frontier items

- **Polynomial chaos expansion (PCE):** regression on few runs → Sobol indices
  *analytically* from the coefficients. Biggest payoff for expensive models,
  biggest lift (orthogonal polynomial bases, degree/overfit control). Validate
  against Ishigami with far fewer runs than Saltelli needs.
- **Shapley effects:** only correct answer once inputs are *correlated* — which
  the `.space` format deliberately does not support today. Gated on a
  correlated-inputs decision; explicitly a non-goal until then.

## E7. Multi-response & the Pareto front

The results CSV already carries multiple metric columns; the funnel analyzes
one. Real experiments trade objectives off (yield ↑ vs cost ↓ vs cycle time ↓):

**`pareto` has moved to E1** — see above. It needed nothing from E1 or E4, and
holding the roadmap's cheapest tool behind six milestones was a scheduling
mistake. What remains here is the work that genuinely depends on other tiers:

- **`desire`** — Derringer–Suich desirability: maps each metric to [0,1],
  combines by geometric mean, and *appends a `desirability` column* — so the
  entire existing single-response pipeline (screen → attribute → RSM optimize)
  runs on its output unchanged. The scalar path and the front view complement
  each other: one recommends, the other shows the trade-off space.

```
desire --max yield --min cost results.csv > scored.csv     # adds a column
sobol analyze model.space scored.csv --metric desirability # pipeline unchanged
pareto --max yield --min cost results.csv > front.csv      # the trade-off set
```

Supporting changes:

- **`objectives:` in `.space`** — `yield: max`, `cost: min` — the declared
  default for both filters (CLI flags override; parser hardening rules apply).
- **Per-metric analysis:** run the screen/attribution once per declared metric;
  report indices side by side (a factor inert for yield may drive cost).
- **E5 tie-in:** mean performance vs S/N robustness is itself a two-objective
  problem — pipe the robust-design stage's output through `pareto` too.

Validation: on a synthetic bi-objective with a known front (e.g. `y1 = x`,
`y2 = 1 − x²`), the extracted set matches the analytic front; the desirability
optimum matches closed form; a dominated point never appears in the front.

---

## Roadmap

| Milestone | Deliverable | Depends on | |
|---|---|---|---|
| **E0** | ✓ `second_order` **implemented**, not merely rejected. **M5 complete** (second-order ✓, Joe-Kuo sequence ✓). M6 built (`ofat`, `grid`); confirmation checker and M7 pending. | — | |
| **E1** | ~ `pareto`, `regress`, `uq`, Morris μ\* CIs, `--keep-share` and the cut-gap diagnostic all ✓ **built**. **One left: the Pareto chart of effects**, which has nowhere to live until `report` exists. Recorded as COMPLETE here until 2026-08-09; it was not. | E0 | |
| **E2** ✓ **BUILT** | `--target-ci` sequential convergence: `morris converge` and `sobol converge`. | E1 (CIs) | Intervals must narrow as the budget doubles, the reported r/N must reproduce the run, and a capped run must exit non-zero. |
| **E3** | ~ ~~`morris --groups` + recursive splitting~~ ✓ both built and measured. **Pending: `pawn` tool, `morris analyze --dgsm`**, (optional) eFAST cross-check. | E1 | |
| **E4** | `rsm` tool + `robust funnel --optimize`. | E1 (LSQ), M6 | |
| **E5** | `noise:` factors, crossed designs, S/N analysis. **The namesake** — nothing in the toolkit does robustness-to-noise today, and this is the largest single piece of unbuilt method. | E4 | |
| **E6** | PCE surrogate; Shapley (gated on correlated inputs). | E3 | |
| **E7** | `desire` filter tool, `objectives:` in `.space`, per-metric analysis. (`pareto` moved to E1.) | E1; E4 for the optimize path | |

**Recommended order: E0 → E1 → E2, then E3 and E4 in either order.** E1 is the
cheapest large win (direction + distribution from runs already paid for, plus
`pareto` for free); E4 (RSM) is the headline feature that completes the
funnel's story — screen, attribute, *optimize*; E5 delivers the promise in the
project's name.

Within E1, build `pareto` **first**: it has no dependency on the least-squares
core that `regress` and `uq` share, so it is not blocked by anything, and its
analytic-front validation is a day's work.
