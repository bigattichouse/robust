# validation — published results vs closed-form ground truth

*Run with `make validate` (~20 s). Exits nonzero if any check fails.*

The unit suites under `*/tests/` pin **our code**. This suite pins the
**claims the roadmap rests on** — the ones taken from papers rather than
derived here. It exists because `EXPANSION_NOTE.md` was written from search
summaries and abstracts, carried three load-bearing unverified claims, and the
house rule in `EXPANSION.md` is *validation against a closed-form reference
before it ships*. That rule should apply to a claim about a method just as much
as to a line of C.

Every number below is produced by `validate.c` from `sources/pdf/`, not quoted
from a summary. Sources are indexed in [`../sources/README.md`](../sources/README.md).

## The benchmark

The Sobol' g-function ([`gfunction.c`](gfunction.c)):

```
g(X) = prod_i (|4*X_i - 2| + a_i) / (1 + a_i),   X_i ~ U[0,1]
```

Smaller `a_i` ⇒ more important `X_i`. It has closed-form Sobol indices, and it
is **non-monotonic in every factor** (the absolute value folds each axis at
`X_i = 0.5`) — which is exactly why it is the right benchmark for a screening
method that must not assume monotonicity. Group indices extend naturally:

```
S_T(u) = 1 - [ prod_{j not in u} (1 + V_j) - 1 ] / V,   V_i = (1/3)/(1+a_i)^2
```

## What it checks, and what came out

### A. μ\* ranks like S_T, but gives no usable magnitude

12-factor g-function, `a` from [C07] Table 2, r=10 ⇒ 130 runs — the same budget
as [C07] Fig. 7. Driven through the repo's real `morris` library, so this also
pins the shipped implementation.

| Measured | Value |
|---|---|
| Spearman(μ\*, S_T) | **0.923** |
| spread of the ratio μ\*/S_T across factors | **118.6×** |

[C07] §6 concludes that "the use of the EE sensitivity measure μ\* as a proxy of
the variance-based total index is acceptable and convenient." That is confirmed
**for ranking**. It does not extend to magnitude: with the ratio varying 119×
across factors, no constant of proportionality recovers S_T's *value* from μ\*.

**Consequence:** the `--st-estimate` flag speculated about in `EXPANSION_NOTE.md`
§5 is not buildable, and `sobol` is not redundant. Variance *shares* — the
budgeting question, "what fraction of output variance does this factor own?" —
require the variance-based estimator. `sobol` is optional when you only need a
ranking, and required when you need shares.

### B. Group μ\* works, at r(G+1) instead of r(k+1)

[C07] §3.3 (p.1512) defines the absolute group elementary effect

```
|d_u(X)| = | y(X~) - y(X) | / Delta
```

where `X~` moves **every** factor of group `u` by ±Δ. Reproducing all three
[C07] Table 1 test cases: 9 factors in 3 groups, r=10 ⇒ **40 runs** where
per-factor screening would need 100.

The property that matters: factors inside a group may move in opposite
directions, and taking the absolute value of the *group* effect is what makes
that safe. A group containing offsetting important factors still registers. So
this cannot produce the silent sign-cancellation false negative that sequential
bifurcation is vulnerable to — which is why it supersedes the `sb` proposal.

### C. μ\*'s reliability is set by the index gap, not the budget

Same 12-factor case, 40 independent seeds per row:

| r | runs | Spearman | top-5 set correct | top-3 set correct |
|---|---|---|---|---|
| 5 | 65 | 0.942 | 85% | 47% |
| 10 | 130 | 0.959 | 95% | 35% |
| 20 | 260 | 0.971 | **100%** | 50% |
| 50 | 650 | 0.980 | 100% | 55% |
| 100 | 1300 | 0.986 | 100% | 45% |
| 200 | 2600 | 0.988 | 100% | 55% |

Top-5 reaches 100% at r=20 and stays. Top-3 sits near a coin flip and **never
improves**, because the top-3 boundary falls between x5 (S_T = 0.1678) and
x8 (0.1661) — a **1.0% tie**, unresolvable at any budget. The top-5 boundary
falls in a **5.3× gap**, hence perfect.

**Consequence:** μ\*'s trustworthiness is a property of *where the keep/drop
line falls*, not of how much you spent. This is measured support for
`--keep-share` (cumulative-share cut) over `--keep-fraction` (fixed count), and
it motivates a diagnostic neither roadmap doc had: **report the gap width at
the cut**, so the tool can say when its own boundary is not trustworthy.

### D. An erratum in [C07] Table 1

Test case 3, group v = {X3, X5, X9}: the paper prints **S_T = 0.393**.

| Route | Value |
|---|---|
| closed form (`gfunction.c`) | 0.41707 |
| Monte Carlo, Jansen estimator [J99], N = 2,000,000 | 0.41709 |
| [C07] printed | 0.393 |

Groups u and w in the same table row reproduce exactly by all three routes
(0.436 and 0.429), and 8 of the 9 printed analytic values across the three test
cases reproduce exactly. The Monte Carlo depends on neither the closed form nor
the paper. Additional diagnostics in check D: 0.393 is not the first-order group
index for that group (0.239), and **no** 3-subset of that `a`-vector has
S_T = 0.393 — so it is not a mislabelled group either.

Conclusion: **the printed 0.393 is a typo; the correct value is 0.417.**
`validate.c` asserts against the corrected value, with the printed one kept
alongside and annotated, so the suite is green against the truth rather than
green against the typo.

A standalone version of this check — one C file, no dependencies on this
project, `make run` — lives in
[`../sources/campolongo-2007-morris-screening_erratum/table1/`](../sources/campolongo-2007-morris-screening_erratum/table1/),
with the write-up and errata summary in `errata.md` alongside it.
That directory is meant to be shared on its own, so anyone can rerun the check
without cloning the rest of this repo.

### E. `sobol`'s estimators are the ones the source recommends

The Saltelli et al. (2010) paper — the estimator `sobol` implements — was read
in full on 2026-08-06 and the implementation checked line by line against it.
All four core choices are correct, and each is the paper's *own* recommendation
rather than merely a defensible option:

| What `sobol.c` does | Where the paper says so |
|---|---|
| `S_i = 1/N Σ f(B)ⱼ(f(A_B⁽ⁱ⁾)ⱼ − f(A)ⱼ) / V` | Table 2 formula (b), p.262; §5.1 p.263 recommends it for our triplet |
| `S_Tᵢ = 1/(2N) Σ (f(A)ⱼ − f(A_B⁽ⁱ⁾)ⱼ)² / V` | Table 2 formula (f), p.262 (Jansen 1999) — caption calls it "best practice so far"; §7 conclusion 1 |
| `A_B⁽ⁱ⁾` = A with column i from B | Table 1, p.260; §3 p.261 prefers this triplet over `B, A, B_A⁽ⁱ⁾` |
| cost `N(k+2)`, radial, n=1 | §3 p.261; §7 conclusions 3 and 4 |

Check E confirms it numerically on the 6-factor g-function (the paper's own
Appendix A.1 test function) at N=65536:

| | worst absolute error, LHS | with the QR sequence |
|---|---|---|
| `S_i` vs closed form | 0.0078 | **0.0001** |
| `S_T` vs closed form | 0.0055 | **0.0001** |

plus the structural property `Σ Sᵢ ≤ 1 ≤ Σ S_Tᵢ` (Eq. 11, p.260), measured at
0.890 and 1.112.

The right-hand column is the paper's remaining conclusion, quasi-random
sampling, which landed on 2026-08-07 and is now the default. See G and H.

### F. Second-order indices against the exact decomposition

The g-function is a product of one-dimensional terms, so `V_ij = V_i·V_j`
exactly and every pair has a closed form — a reference, not a benchmark.
Also pins the cost at exactly `N(k+2+k(k-1)/2)`.

### G. Quasi-random sampling beats LHS, by a widening margin

The paper asserts the advantage (§7 conclusion 3) but does not quantify it for
this estimator on this benchmark. Everything except the sampler is held fixed —
same g-function, same seed, same estimators, same N — and both arms are driven
through the shipped `sobol_design_build`/`sobol_analyze`:

| N | LHS | Sobol QR | ratio |
|---|---|---|---|
| 256 | 0.0798 | 0.0269 | 3.0× |
| 1024 | 0.0333 | 0.0121 | 2.8× |
| 4096 | 0.0352 | 0.0031 | 11.3× |
| 16384 | 0.0119 | 0.00028 | 43.0× |
| 65536 | 0.0078 | 0.00012 | **65.8×** |

The gap *grows*, which is the signature of a convergence-rate difference rather
than a constant offset — that is the check, not the single-point comparison.

It also measures the consequence practitioners trip over. Because the
sequence's uniformity is a property of **aligned blocks of 2^m points** (§5.1
consideration 1), a `samples:` that is not a power of two can buy more runs and
less accuracy: N=20000 gave **4.5× the error of N=16384 while costing 22%
more**. The `sobol` CLI notes this when it sees one.

### H. Property A of the vendored direction numbers, and where it stops

Joe & Kuo state that their recommended D(6) set satisfies Sobol' *Property A*
"up to dimension 1111". That is a secondary-source claim about a table we ship
a slice of, and it sets a user-visible limit, so it is reproduced rather than
cited.

Property A holds exactly when the s×s matrix of leading direction-number bits
is nonsingular over GF(2) — an s³ rank computation instead of a 2^s-point
simulation. Measured: it holds at **every dimension through 1111 and first
fails at 1112**. The authors' figure is exact.

This is what caps `sobol` at **512 factors**: it needs 2 dimensions per factor,
so the 1024 dimensions we vendor are all inside the region where Property A
holds. Above that the tool errors and names the limit rather than falling back
to LHS, because a silent substitution would leave no way to tell from the
output which method produced the numbers.

The boundary itself lies past dimension 1024, so it needs the full published
file, which is gitignored. That half of the check **skips, visibly**, when
`sources/fetch.sh` has not been run — a skip is never reported as a pass.

## Layout

| File | Contents |
|---|---|
| `gfunction.h` / `.c` | The g-function and its closed-form single-factor and group indices. Sources cited in the header. |
| `validate.c` | Checks A–H. Also carries the reference implementation of group μ\*, which is the prototype for `morris --groups` (see [`../spec/morris-groups.bp`](../spec/morris-groups.bp)). |

## House rules this suite follows

- **Cite in code.** Every formula carries its source, section and page in a
  comment. A reader should never have to trust a number's provenance.
- **Pin to the authors' implementation where one exists.** The Sobol-sequence
  constants in `core/tests/test_doe.c` were produced by Joe & Kuo's own
  `sobol.cc`, not by ours. A checksum taken from our generator would have
  passed on day one and pinned nothing.
- **A skip is not a pass.** Check H's second half needs a data file that is
  gitignored; when it is absent the suite says so in those words.
- **Two routes for anything surprising.** Check D only claims an erratum
  because an independent estimator agrees with the closed form.
- **Assert against truth, not against print.** Where a source is wrong, encode
  the correction and say why, in the place where a future "fix" would break.
- **Tie-aware assertions.** Never assert an ordering that the ground truth does
  not actually separate — finding C shows those are coin flips, and an
  assertion on one is a flaky test wearing a proof's clothing.
