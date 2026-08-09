# Sources

Reference papers for the leads in `../EXPANSION_NOTE.md`. PDFs are in `pdf/`;
`fetch.sh` re-downloads the ones with public URLs.

Every entry says *why it matters to this suite*, not just what it argues. Where
a paper already settles something, that is stated so nobody re-derives it.

**Provenance:** these were gathered from
`../../Activation-Geometry/`, which hit the gaps described in the note while
doing ordinary pruning and quantization work. Several were found *after* the
relevant note section was drafted and changed what it said — that is recorded
in each entry rather than quietly folded in.

---

## 1. Screening when the factor count is large (`EXPANSION_NOTE.md` §§1–3)

| Paper | ID | Bearing |
|---|---|---|
| Campolongo, Cariboni & Saltelli, **An effective screening design for sensitivity analysis of large models**, EMS 2007 | [local PDF](pdf/campolongo-2007-morris-screening.pdf) | **Read in full 2026-08-06; the single most load-bearing source here.** Defines μ\* and the improved sampling behind the `morris` tool; cost is **r(k+1)**. Two things the earlier summary of this entry got wrong or missed — see the correction block below. §3.3 (p.1512) extends μ\* to **groups of factors**, which is the group-screening method the note went looking for. §6 (p.1517) endorses μ\* as a proxy for the total index, but only for **ranking**. Also contains a typo in Table 1; see below. |
| Saltelli, Annoni, Azzini, Campolongo, Ratto & Tarantola, **Variance based sensitivity analysis of model output. Design and estimator for the total sensitivity index**, Comp. Phys. Comm. 181(2) 259–270, 2010 | [local PDF](pdf/saltelli-2010-total-index-estimator.pdf) · [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0010465509003087) | **The estimator `sobol` implements.** `S_T − S_i` is the interaction share — the quantity that tests whether an additive model is valid at all. Paywalled; the local copy was supplied manually and `fetch.sh` cannot retrieve it. **Read in full 2026-08-06; our `sobol` implements it correctly.** Verified: `S_i` is Table 2 formula (b) p.262, which §5.1 p.263 recommends for our A/B/A_B^(i) triplet; `S_Ti` is Table 2 formula (f) p.262 (Jansen 1999), which the Table 2 caption and §7 both name best practice; the A_B^(i) convention matches Table 1 p.260; cost N(k+2) matches §3. Numerically confirmed against the g-function closed form (the paper's own Appendix A.1) in `make validate` check E. **§7's fourth conclusion, quasi-random sampling, landed 2026-08-07** and is now the default — §5.1 p.263's construction (A and B as the left and right halves of one 2k-dimensional sequence) is implemented as written, and check E's worst error fell from 0.0078 to **0.0001**. See §4 below for the sequence itself. |
| Feng, Lu & Yang, **Enhanced Morris method for global sensitivity analysis: good proxy of Sobol' index**, Struct. Multidisc. Optim. 59, 373–387, 2019 · **Correction** 62, 3539–3540, 2020 | [local PDF of the correction](pdf/feng-2020-enhanced-morris-correction.pdf) · [10.1007/s00158-018-2071-7](https://doi.org/10.1007/s00158-018-2071-7) | Revisits the μ\* ↔ Sobol relationship for **non-uniform** inputs, arguing the usual quantitative link assumes standard-uniform factors. Checked because it is the nearest prior art to `make validate` check A; it does not pre-empt our finding (ranking yes, magnitude no), which is measured here rather than argued. **The 2020 correction is citations only** — 45 items, no change to any equation, result or figure, so nothing in check A or `EXPANSION.md` is affected. **But treat the paper as unreliable for attribution:** the corrected items are substantive misattributions, not formatting, and many land in exactly this literature (#4 Campolongo & Saltelli 1997 → Wang et al. 2017; #5 Campolongo et al. 2007 → Saltelli et al. 2008; #19 Saltelli et al. 2008 → Morris 1991). Its own claims stand; its pointers to other people's did not. Only the correction is held locally — the original is paywalled. |
| Kleijnen et al., **Sequential bifurcation** | [Springer chapter](https://link.springer.com/chapter/10.1007/0-387-28014-6_13) · [Bettonvil & Kleijnen, EJOR 1997](https://www.sciencedirect.com/science/article/abs/pii/S0377221796001567) | Group screening by recursive splitting: reportedly 92 factors → 10 in **19 runs**, 281 inputs → 15. Logarithmic in factor count *when importance is sparse*. **SUPERSEDED for this suite (2026-08-06):** group μ\* (Campolongo §3.3, above) gives the same cost reduction with no monotonicity or known-sign assumption, so it cannot produce SB's silent sign-cancellation false negative. `morris --groups` is being built instead; see `../spec/morris-groups.bp`. **Both papers remain paywalled with no local copy — the run counts above are still unverified secondary-source figures and should not be quoted.** |

## 2. What the screening was for — and the measured sparsity that undercuts it

| Paper | ID | Bearing |
|---|---|---|
| Liu et al., **The Unreasonable Effectiveness of Random Pruning**, ICLR 2022 | [2202.02643](https://arxiv.org/abs/2202.02643) | **The control any screening claim needs.** Finds the *layer-wise ratio* matters far more than which units are chosen inside a layer, and the effect strengthens with width and depth. Confirmed independently in Activation-Geometry: at matched allocation, random selection beat a carefully derived criterion by 5.8×–15.6×. Any factor-importance result must beat random-at-matched-allocation before it is evidence. |
| Yin et al., **OWL: Outlier Weighed Layerwise Sparsity**, ICLR 2024 | [2310.05175](https://arxiv.org/abs/2310.05175) | Sets each layer's sparsity from its *activation outlier ratio* — i.e. a cheap statistic replaces the sweep entirely. Reports 60+ perplexity improvement over uniform at 70% sparsity. Independently reproduced: those statistics predict measured per-layer sensitivity at ρ = 0.638, **at the reliability ceiling** of the ground truth. Relevant because it is the case where a DOE was *not needed* — a measurable covariate did the job. |
| Kolawole, Dery et al., **Bonsai: structured pruning with only forward passes**, 2024 | [2402.05406](https://arxiv.org/abs/2402.05406) | Perturbative, forward-pass-only, structured — the DOE-shaped method, already built and published. Samples sub-models and regresses module importance from their performance, reaching **50% sparsity on 7B/8B models on one A6000**. `EXPANSION_NOTE.md` §1 proposes something close to this; read before treating any of it as novel. |

## 4. Quasi-random sampling for the Sobol design (M5)

Saltelli et al. 2010 §7 lists quasi-random sampling as one of its four best
practices but does not say which sequence to build. These are the sources for
the one we ship.

| Source | ID | Bearing |
|---|---|---|
| Joe, S. & Kuo, F. Y., **Constructing Sobol sequences with better two-dimensional projections**, SIAM J. Sci. Comput. 30, 2635–2654 (2008) | [10.1137/070709359](https://dx.doi.org/10.1137/070709359) · [author page](https://web.maths.unsw.edu.au/~fkuo/sobol/) | **The direction numbers `sobol` samples with.** The paper is paywalled and no local copy is held; nothing here depends on reading it, because the artefacts it produced are published openly on the author page and are what we actually use. We ship the first 1024 dimensions of their recommended D(6) set as `core/src/sobol_dirnum.h`, generated by `core/tools/gen_sobol_dirnum.sh`. |
| Joe, S. & Kuo, F. Y., **Notes on generating Sobol′ sequences** (2008), 3pp | [joe-kuo-notes.pdf](https://web.maths.unsw.edu.au/~fkuo/sobol/joe-kuo-notes.pdf) · [local](pdf/joe-kuo-notes.pdf) | **Read in full 2026-08-07; the algorithm `core/src/sample.c` implements.** Eq. (2) is the direction-number recurrence, eq. (3) the direct form, eq. (5) the Antonov–Saleev Gray-code form we use. §3 defines the `d s a m_i` file format. §4 settles a question we would otherwise have guessed at: on the common advice to skip an initial portion of the sequence, the authors write "we are less persuaded by such recommendation ourselves" — so we keep point 0. Saltelli §5.1 consideration 1 independently agrees (uniformity is a property of aligned blocks of 2^m points, so skipping rows is a mistake). |
| **sobol.cc** — the authors' reference generator | [sobol.cc](https://web.maths.unsw.edu.au/~fkuo/sobol/sobol.cc) · [local](pdf/joe-kuo/sobol.cc) | **The ground truth our tests are pinned to.** Compiled and run against the D(6) file, it produced every reference constant in `core/tests/test_doe.c` — our output is bit-for-bit identical to it over 4096×300 points, 65536×8 points, and dimensions 513–1024. The constants are theirs, not ours, which is the point. |
| **new-joe-kuo-6.21201** — the D(6) direction numbers | [data file](https://web.maths.unsw.edu.au/~fkuo/sobol/new-joe-kuo-6.21201) · [licence](https://web.maths.unsw.edu.au/~fkuo/sobol/licence) | 21201 dimensions; we vendor the first 1024. **Unlike every other source in this file, this one is redistributable** — BSD-3-clause, Copyright (c) 2008 Frances Y. Kuo and Stephen Joe — so the generated header carries the notice in full and ships in-repo. `sources/fetch.sh` re-downloads the original. |

**Verified, not cited (2026-08-07).** The author page states of this file that
"Property A is satisfied up to dimension 1111". That is a claim about a table
we ship a slice of and that sets a user-visible limit, so it was reproduced
rather than trusted. Property A holds exactly when the s×s matrix of leading
direction-number bits is nonsingular over GF(2), which makes it an s³ rank
computation instead of a 2^s-point simulation. Measured: it holds at every
dimension through 1111 and **first fails at 1112** — the authors' figure is
exact. `make validate` check H reproduces both halves (the full range needs
the complete file, so that part *skips*, visibly, when `fetch.sh` has not run).

This is what sets `DOE_SOBOL_MAX_FACTORS = 512`: `sobol` needs 2 dimensions per
factor, so 1024 dimensions is 512 factors, and every dimension shipped is
inside the region where Property A holds. Above it the tool **errors and names
the limit** rather than falling back to LHS, because a silent substitution
would leave no way to tell from the output which method produced the numbers.

**Measured, not assumed (2026-08-07):** quasi-random sampling is worth its
complexity, and the gap widens with N as a convergence-rate difference must —
3.0× the accuracy of LHS at N=256, 11× at 4096, **66× at 65536**, on the same
estimator and the same g-function with only the sampler changed (`make
validate` check G). One practical consequence falls out of Saltelli §5.1
consideration 1: because the sequence's uniformity is a property of aligned
2^m blocks, a `samples:` that is not a power of two can buy more runs and less
accuracy — N=20000 gave **4.5× the error of N=16384 while costing 22% more**.
The `sobol` CLI notes this when it sees one.

---

**Corrections the Campolongo paper forced, after reading it (2026-08-06).**
All three are reproducible with `make validate`; see
[`../validation/README.md`](../validation/README.md).

1. **The "sobol may be skippable" line that used to be in this table was an
   overstatement.** The paper endorses μ\* as a proxy for the total index only
   for *ranking* (§6, p.1517: "acceptable and convenient"). Measured on the
   12-factor g-function at the paper's own budget: Spearman(μ\*, S_T) = 0.923,
   but the ratio μ\*/S_T varies **118.6×** across factors. So μ\* recovers the
   *order* of S_T and never its *value*. `sobol` is **optional** when a ranking
   is all you need and **required** for variance shares, additivity checks
   (`Σ Sᵢ ≈ 1`), interaction detection (`S_T − Sᵢ`), or any process that calls
   for variance attribution.
2. **The paper's best idea was not in the summary at all.** §3.3 (p.1512)
   extends μ\* to *groups*, at `r(G+1)` cost, with the absolute value of the
   group effect defeating the sign cancellation that makes sequential
   bifurcation unsafe. This is the group screening `EXPANSION_NOTE.md` §2 was
   hunting for, and it retires the `sb` proposal.
3. **Table 1 has an erratum.** Test case 3, group v = {X3,X5,X9}, prints
   S_T = 0.393. The correct value is **0.417**: our closed form gives 0.41707
   and an independent 2,000,000-sample Jansen Monte Carlo gives 0.41709, while
   groups u and w in the same row reproduce the printed values exactly by all
   three routes. 0.393 is not the first-order group index either (0.239), and
   no 3-subset of that parameter vector has S_T = 0.393. Anyone validating
   group-screening code against that table will otherwise chase a phantom bug.
   → **[`campolongo-2007-morris-screening_erratum/`](campolongo-2007-morris-screening_erratum/)**
   holds the write-up (`errata.md`) and a self-contained, dependency-free
   reproduction in [`table1/`](campolongo-2007-morris-screening_erratum/table1/)
   (one C file, `make run`, ~1 s). It stands alone — shareable without the rest
   of this project.

**Correction this forced on the note:** §2's sparsity premise is weaker than
assumed. Measured, the top 1% of units carry only ~4.7% of activation volume and
the top 10% carry 24.6% — about 2.5× top-heavy, *not* sparse — and with smooth
activations (SiLU, GELU) **no unit is ever inactive**. Sequential bifurcation's
advantage is largest under genuine sparsity, so it should be tested against that
measured profile rather than against a 10-of-92 case.

## 3. Cascades and structured parameter blocks (`EXPANSION_NOTE.md` §8)

| Paper | ID | Bearing |
|---|---|---|
| Li et al., **Measuring the Intrinsic Dimension of Objective Landscapes**, ICLR 2018 | [1804.08838](https://arxiv.org/abs/1804.08838) | **The nearest existing thing to §8.2's reduction.** Optimises inside a *frozen random d-dimensional projection* of a D-dimensional parameter space and finds solutions appear at surprisingly small d. That is the "design over a low-dimensional shadow of the real parameter block" idea, already formalised. It projects randomly and optimises by gradient descent; §8 wants a *constructed* family and a DOE over it, so the open question is whether the intrinsic-dimension result survives a design-based estimator. |
| **Beyond Layer Importance in Layer-wise Sparsity: An Inter-Layer Perturbation-Absorption Perspective**, 2026 | [2606.15161](https://arxiv.org/abs/2606.15161) | **The measured mechanism behind §8.3's cascade problem.** Stages respond heterogeneously: **early stages amplify perturbations, middle and late stages absorb them**, with relative drift falling monotonically along the chain, and absorption is specifically a *large*-perturbation phenomenon. This is exactly why a main-effect estimated for one stage is not portable — the same nominal change has different consequences by position. Any conditional or sequential design for cascades needs to model this. |

**Also relevant, not yet obtained:** the layer-wise reconstruction objective
that most compression methods minimise is documented as a *misaligned
surrogate* — cheap, but not the quantity being optimised, with errors that
compound along the chain and worst when the loss budget is tight. Measured
first-hand: a criterion beat its alternative by 5.4× on the surrogate and lost
by 10× end to end. This is the strongest argument for why a DOE over the *real*
response is worth its cost even when a closed form for the surrogate exists.

---

## Honest status

*Updated 2026-08-07.*

**Verified by code** — reproducible with `make validate`, sources cited inline
in `../validation/`: the μ\* ↔ S_T relationship and its limit; group μ\* against
Campolongo Table 1; the ranking-fidelity-vs-budget curve; the Table 1
erratum, confirmed by two independent routes; the quasi-random-vs-LHS gap and
its growth with N; and Joe & Kuo's Property A boundary at dimension 1111/1112.

**Verified against the authors' own reference implementation:** our Sobol
sequence is bit-for-bit identical to `sobol.cc` across every dimension and
sample count we ship, including the published 10×3 sample on their page.

**Verified by reading the primary source:** the Morris and Sobol cost formulas;
the group-μ\* definition and why its absolute value is what makes grouping safe;
the Sobol direction-number recurrence and the decision not to skip initial
points.

**Verified from source in another repo:** that TSQ's array is strength 2.

**Still not verified:** the 19-run and 281-input figures for sequential
bifurcation and the logarithmic cost claim — both papers remain paywalled with
no local copy. These are no longer load-bearing, since the method is not being
built, but they are still secondary-source figures and should not be quoted.

**Taken from abstracts only:** the intrinsic-dimension (1804.08838) and
perturbation-absorption (2606.15161) results. Neither has been read in full,
and §3's entries rest on them.

**Standing rule this section exists to enforce:** a claim from a source is a
lead until code reproduces it. Two of the three claims that were load-bearing
here turned out to be wrong or overstated, and the paper that settled them was
sitting unread in `pdf/` the whole time.
