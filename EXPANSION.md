# Expansion — Methods & Post-Method Analysis Roadmap

*What comes after the funnel. Companion to DESIGN.md (build plan, M0–M7) and
SECURITY.md (hardening discipline). Written 2026-07-16.*

The funnel today answers *"what matters?"* (Morris), *"how much, and which
interactions?"* (Sobol), and hands survivors to the bench (taguchi). This plan
adds the questions it can't yet answer — *"in which direction?"*, *"have I
sampled enough?"*, *"what does the output distribution look like?"*, *"which
pair interacts?"*, *"what setting is optimal?"*, *"is it robust to noise?"* —
in order of value per effort.

Every addition inherits the house rules: C99 `-Werror`, valgrind/ASan-clean,
deterministic from the `.space` seed, the clean-error invariant of SECURITY.md,
and validation against a closed-form reference before it ships.

---

## E0. Prerequisites (do first, small)

- **`second_order:` honesty.** The parser accepts the flag; nothing implements
  it — a silent no-op today. Until M5 lands, `sobol` must *reject* a space that
  sets it ("second_order not yet implemented") rather than quietly ignore it.
- **Finish M5–M7** (DESIGN.md §11): Joe-Kuo sequence + second-order indices,
  `ofat`/`grid`/confirmation checker, Python bindings. Several E-items below
  build on M5/M6, noted per item. (M7's CI half shipped 2026-07-16.)

---

## E1. Post-run analysis pack — reuse the runs you already paid for

*No new sampling; every item consumes the existing Morris/Sobol responses.*

| Item | What it adds | Sketch | Validation |
|---|---|---|---|
| **SRC / SRRC + R²** | *Direction* of each effect (raise `temp` → response up or down?) — the one thing variance shares can't say. R² doubles as a trust diagnostic: ≈1 ⇒ the linear story suffices; low ⇒ the variance-based indices were needed. | OLS on the (standardized) sample matrix; SRRC = same on ranks for monotone-nonlinear models. A few hundred lines in `common/` (stats.c grows a small least-squares). | Exact on a known linear model (`y = 10x0 + 5x1`): SRC ∝ coefficients, R² = 1. |
| **Output UQ summary** | What the output *distribution* looks like, not just who drives it: mean, variance, P5/P50/P95, histogram + empirical CDF as SVG in the report. | Sort + percentile + binning over the already-captured responses; render in `report.c`. | Percentiles of a uniform-driven linear model match closed form. |
| **Morris μ\* bootstrap CIs** | Error bars on the keep/drop cut, so `--keep-fraction` decisions aren't made on point estimates. | Bootstrap over trajectories (the CI machinery already exists in `sobol`). | CI shrinks ~1/√r; covers the analytic μ\* on a linear model. |

Deliverable: `robust` report gains a regression table + distribution panel;
`morris analyze` gains CI columns. **This is the highest value-per-effort tier.**

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
| **DGSM** | Cheap upper bounds on total indices from Morris-style sampling — a bridge between the two existing tools. | Mean-square elementary effects with the Poincaré constant; lands inside `morris analyze --dgsm`. | Bound property: DGSM-derived bound ≥ S_Tᵢ on Ishigami. |
| **eFAST** | Independent variance-based estimator to cross-check Sobol. Optional — largely duplicates what exists. | Frequency-assigned sinusoidal sampling + spectrum sums (direct sums; no FFT dependency). | Ishigami first-order indices. |

## E4. RSM stage — from "who matters" to "what setting is best"

Central composite (or Box–Behnken) design on the 2–3 funnel survivors →
quadratic fit → stationary-point + canonical analysis → predicted optimum.
New tool `rsm/`; `robust funnel --optimize` chains it as the final stage.
Depends on the E1 least-squares core and pairs naturally with M6's `grid`.

Validation: recovers the known optimum of a synthetic quadratic bowl to
tolerance; degenerate fits (saddle, rank-deficient) produce clean errors.

## E5. Robust parameter design — noise factors

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

---

## Roadmap

| Milestone | Deliverable | Depends on | |
|---|---|---|---|
| **E0** | `second_order` rejected until implemented; M5–M7 closed out. | — | |
| **E1** | SRC/SRRC + R², output UQ panel, Morris μ\* CIs — in report + analyzers. | E0 | |
| **E2** | `--target-ci` sequential convergence for morris + sobol. | E1 (CIs) | |
| **E3** | `pawn` tool; `morris analyze --dgsm`; (optional) eFAST cross-check. | E1 | |
| **E4** | `rsm` tool + `robust funnel --optimize`. | E1 (LSQ), M6 | |
| **E5** | `noise:` factors, crossed designs, S/N analysis. | E4 | |
| **E6** | PCE surrogate; Shapley (gated on correlated inputs). | E3 | |

**Recommended order: E0 → E1 → E2, then E3 and E4 in either order.** E1 is the
cheapest large win (direction + distribution from runs already paid for); E4
(RSM) is the headline feature that completes the funnel's story — screen,
attribute, *optimize*; E5 delivers the promise in the project's name.
