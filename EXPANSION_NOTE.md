# Expansion Note — Screening at 1000+ Factors

*Two outside findings, offered as leads rather than specifications: §§1-7 on
screening at high factor counts, and §8 on designs for structured parameter
blocks and cascades. Companion to EXPANSION.md. Written 2026-08-04, §8 added
2026-08-05, both from `../Activation-Geometry/`. **Resolved 2026-08-06** —
see §0.*

**References are in [`sources/README.md`](sources/README.md)**, with PDFs
fetched by `sources/fetch.sh`, each entry noting why it matters to this suite,
and an explicit list at the end of which claims here are verified and which
are not.

**This note was written as a hypothesis to test, not a result to implement.**
Everything below §0 came from search summaries and abstracts, not from reading
the primary sources. It has now been tested. The verification said the note was
wrong about its headline proposal and right about the gap — which is exactly
what it was for.

---

## 0. Resolution — what the verification found

*Added 2026-08-06, after reading `sources/pdf/campolongo-2007-morris-screening.pdf`
and building `validation/` (`make validate`, 0.9 s, all checks green).*

The note's own §1 corrections had already undercut its §2 proposal. Testing
finished the job. **Three outcomes, in order of consequence:**

**1. Sequential bifurcation is superseded — do not build `sb`.** Not because
the cost claim was disproved, but because a method in the same family does the
same job without SB's dangerous assumption. Campolongo et al. 2007 §3.3 extends
μ\* to *groups* of factors: move every member of a group by ±Δ, take the
**absolute value of the group effect**. Cost is `r(G+1)` instead of `r(k+1)`,
and because the measure is an absolute value of the group's response change, a
group holding two important factors with opposing signs **still registers**.
That is precisely the silent false negative §2 identified as SB's worst failure
mode, and it is designed out rather than tested for. The paper is explicit
(§3.3, p.1512): the signed definition "could not be extended straightforwardly
to cases in which more than one factor are moved at the same time… In contrast,
if using μ\*, this problem is overcome."

Reproduced in `make validate` check B: all three of the paper's Table 1 test
cases, 9 factors in 3 groups at **40 runs where per-factor screening needs
100**. Spec: [`spec/morris-groups.bp`](spec/morris-groups.bp). It lands as a
new mode on the existing binary — the per-factor path is untouched.

So §2, §3 and §7's `sb` row are **retired as leads**. §3's instinct was right,
though: it bet on compressed sensing over SB, and §8.3's Bonsai update is that
bet paying off. The remaining open item from §3 is supersaturated designs.

**2. §5's μ\* ↔ S_T speculation is half right, and the half that fails is the
half it wanted.** Measured on the 12-factor g-function at the paper's own
budget (`make validate` check A):

| | |
|---|---|
| Spearman(μ\*, S_T) | **0.923** |
| spread of the ratio μ\*/S_T across factors | **118.6×** |

μ\* ranks like the total index; it does not give its magnitude. There is no
usable constant, so **`morris analyze --st-estimate` is not buildable** and is
dropped. What survives is a documentation point, now in EXPANSION.md: the
`sobol` stage is *optional* when you need a ranking and *required* when you
need variance shares — or when your process requires variance attribution,
which is reason enough by itself. Nothing is retired.

**3. A finding neither this note nor EXPANSION.md anticipated: μ\*'s
reliability is set by the index gap, not the budget.** `make validate` check C,
40 seeds per row:

| r | runs | top-5 set correct | top-3 set correct |
|---|---|---|---|
| 5 | 65 | 85% | 47% |
| 10 | 130 | 95% | 35% |
| 20 | 260 | **100%** | 50% |
| 200 | 2600 | 100% | 55% |

Top-3 never converges — the boundary falls between two factors whose true S_T
differ by **1.0%**, and no sampling resolves a tie. Top-5 falls in a 5.3× gap
and is perfect from r=20. A keep/drop cut is therefore trustworthy or not
depending on *where it lands*, which the user cannot know in advance and the
tool can. This became the **cut-gap diagnostic** in EXPANSION.md E1, and it is
independent evidence for `--keep-share` over `--keep-fraction`.

**Also: the paper has a typo.** Campolongo et al. 2007 Table 1, test case 3,
group v = {X3,X5,X9} prints S_T = 0.393. The correct value is **0.417** — our
closed form gives 0.41707 and an independent 2M-sample Jansen Monte Carlo gives
0.41709, while groups u and w in the same row reproduce the printed values
exactly by all three routes. 0.393 is also not the first-order group index
(0.239) and matches no 3-subset of that parameter vector, so it is not a
mislabelled group either. Anyone validating group screening against that table
will otherwise chase a phantom bug. See `make validate` check D.

**What did not change:** §1's corrections still stand and are still the reason
none of this becomes an LLM-pruning feature. §8.2's reduction pattern is still
the most generalizable thing in the note and is still unbuilt — see §8.4.

---

## 1. Where this came from

`../Activation-Geometry/` is measuring per-layer activation statistics in
transformer models, aiming to decide which parts of a network can be compressed.
Two granularities matter there:

- **Per-layer** — 28 to 64 factors. This suite already handles it; TSQ
  (`../Task-Specified-Quantization/`) uses `taguchi` for exactly this.
- **Per-neuron** — 3072 factors in *one* layer of a small model, ~86,000 across
  the model. Nothing in the funnel reaches this.

The per-neuron question looks like a natural fit for screening — the prior
expectation is that a small number of neurons carry most of the influence and
the rest are near-noise. That is the classic screening situation.

**Two corrections to that framing, both found after this note was first
drafted.** They matter for how you read the rest of it.

**Correction 1 — the sparsity premise is weaker than assumed.** Measured on
Qwen3-0.6B (`../Activation-Geometry/FINDINGS.md` §F): the top 1% of neurons
carry only 4.7% of total activation volume and the top 10% carry 24.6%, against
1% and 10% for a uniform distribution. Only ~2.5× top-heavy, and concentration
is much stronger in late layers than early ones. Also, **no neuron is ever
inactive** — SwiGLU's `silu(gate)·up` is smooth and reaches zero only on
underflow, so there is no dead-neuron population to find (§G.3). Sequential
bifurcation's advantage is largest when importance is *sparse*; this is
moderately top-heavy, not sparse. Test SB against measured sparsity of ~0.25
for the top decile, not against a 10-of-92 case.

**Correction 2 — cost is not the main objection.** Measured: one evaluation
(NLL over 50 GSM8K sequences, Qwen3-0.6B, MI50) is **3.48 s**.

| Method | Cost | k = 3072, r = 10 | Wall time |
|---|---|---|---|
| Morris | `r(k+1)` | 30,730 evals/layer | 29.7 h/layer, ~35 days for 28 layers |
| Morris | `r(k+1)`, r=4 | 12,292 evals/layer | 11.9 h/layer, ~14 days |
| Sobol | `N(k+2)` | ~3.1M evals/layer | infeasible |

So on a small model it is *reachable* — days, not never. It stops being
reachable at 27B (≈4,200 days for Morris at r=4), which is the size that
matters for the downstream application.

The stronger objection is structural: **within one layer, measured at block
output, neuron ablation effects are exactly additive.** Removing neuron *j*
changes the output by exactly `−W[:,j]·h_j` regardless of what else was
removed. There is no interaction for σ or `S_T − S_i` to detect, and the
variance attribution has a closed form in the activation covariance. Sampling
would be an expensive Monte Carlo estimate of an identity.

That objection lifts once the response is **end-to-end task loss** rather than
block output — downstream nonlinearity does create genuine interactions, they
matter at aggressive prune rates, and, decisively, **the metric people actually
care about (exact-match, pass@1) is not differentiable**, so gradient and
Hessian methods have to optimize a proxy while a DOE takes the real thing. That
is where this suite has an advantage no gradient method can copy.

The practical consequence is a granularity change, not an abandonment: use the
closed form (or one backward pass, which yields all 3072 sensitivities at once)
to *rank* neurons, and use the DOE to choose **per-layer prune rate** against
the real metric — 28–64 factors, squarely in Morris/Taguchi territory, and
structurally the same experiment TSQ already runs with width in place of bits.

## 2. The lead: sequential bifurcation

> **SUPERSEDED (2026-08-06) — see §0.** Group μ\* (Campolongo et al. 2007 §3.3)
> delivers the same `O(G)`-per-round cost reduction without SB's monotonicity
> and known-sign assumptions, because it takes the absolute value of the group
> effect. The silent false negative this section correctly identifies as SB's
> worst failure mode is designed out rather than tested for. Reproduced against
> the paper's Table 1 in `make validate`. **Do not build `sb`.** The section is
> kept because its analysis of the failure mode is what made the alternative
> recognisable as the better one, and because the paywalled run counts below
> are still unverified and should not be quoted by anyone.

**Sequential bifurcation (SB)** — Bettonvil (1990), Bettonvil & Kleijnen (1997),
reviewed by Kleijnen in the Springer handbook chapter. Group screening with
recursive splitting: evaluate a *group* of factors together, discard groups
showing no important effect, split the surviving groups in two and recurse.

The claim that makes it interesting is that **cost is logarithmic in factor
count when importance is sparse**, not linear like Morris.

### Numbers I found, which you should check

- A supply-chain simulation: **92 factors narrowed to a shortlist of 10 after
  simulating 19 combinations.**
- A case study: **281 inputs narrowed to 15 important ones.**

**Double check these.** I have them from search-result summaries of secondary
sources, not from the papers. Specifically worth confirming:

1. Are those counts *total* evaluations, or evaluations per stage, or
   replications excluded? A factor of 5 here changes whether this is worth
   building.
2. What was the sparsity in those cases? 10-of-92 is ~11% important. If the
   method degrades sharply above that, and neuron importance turns out to be
   30% rather than 5%, the win evaporates.
3. Is the cost genuinely `O(m log k)` for m important factors, or is that a
   gloss? Derive the bound yourself rather than trusting my phrasing.

Sources to start from, in `../Activation-Geometry/sources/README.md` §2:

- Kleijnen et al., *Screening for the Important Factors in Large Discrete-Event
  Simulation Models: Sequential Bifurcation and Its Applications* —
  https://link.springer.com/chapter/10.1007/0-387-28014-6_13
- Bettonvil & Kleijnen, *Searching for important factors in simulation models
  with many factors: Sequential bifurcation*, EJOR 1997 —
  https://www.sciencedirect.com/science/article/abs/pii/S0377221796001567
- Shi & Kleijnen, *Testing the assumptions of sequential bifurcation for factor
  screening* — https://doi.org/10.2139/ssrn.2627090

Both of the first two are paywalled. I have no local copies. The third exists
because the assumptions are known to be the weak point, which is the next
section.

### The assumptions are the whole question

SB buys its speed by assuming things Morris does not need:

- **Effects are monotone in each factor.**
- **Signs are known in advance** (or at least consistent within a group) — so
  that grouped effects add rather than cancel.
- **Effects are roughly additive**, at least enough that a group's aggregate
  effect indicates whether any member matters.

If signs are mixed within a group, important factors cancel and SB discards a
group that mattered. **That is a silent false negative** — the worst failure
mode for a screening tool, and much worse than Morris's failure modes, which
are noisy rather than silently wrong.

This is exactly why there is a literature on testing SB's assumptions, and it
is why I would not ship this as a `sb` binary on my say-so. It needs the
assumption check built in, not bolted on.

## 3. Go look for yourself — I may have found the wrong method

I searched for "group screening" and landed on SB because it is the name in the
simulation-DOE tradition this suite belongs to. That is a narrow slice of a
much larger literature, and there may be something better. Worth your own
search before committing:

- **Controlled sequential bifurcation (CSB)** — adds error-rate control (power
  and Type I) to SB. If it exists and works, it probably dominates plain SB for
  a tool that has to be trustworthy by default.
- **Group testing / Dorfman pooling** — the combinatorial-search tradition.
  Same problem, different century, large body of theory on optimal pool sizes.
- **Compressed sensing / sparse recovery** — arguably the modern framing:
  if the effect vector is k-sparse, `O(m log k)` *random* measurements recover
  it, with recovery guarantees SB does not have. This may be strictly better
  than recursive splitting and it composes with random designs the suite
  already generates. **If I were betting on one direction to investigate, it
  would be this one** — but I have not verified that the guarantees survive the
  noise model here, and that is the thing to check.
- **Supersaturated designs** — the DOE-native answer to "more factors than
  runs," closer to this suite's existing vocabulary than compressed sensing is.

If one of these beats SB, build that instead. Nothing here is a commitment to
sequential bifurcation specifically; the commitment is to *some* method that
breaks the linear-in-k barrier.

## 4. Build rigorous modeling to test it, before writing the tool

The house rule in EXPANSION.md is validation against a closed-form reference
before shipping. That rule is doing more work than usual here, because the
whole claim is a *cost* claim under a *sparsity assumption*, and both are easy
to accidentally assume into existence.

**Ishigami is the wrong test function for this.** It has three factors, and it
is deliberately non-monotonic — which violates SB's core assumption. Validating
SB on Ishigami would either fail for the wrong reason or, worse, be tuned until
it passes. Use it as a negative control if anything: SB *should* do badly there,
and a tool that reports success on Ishigami has a bug.

What I would want to see before believing the method:

**A. A synthetic model with known ground truth and tunable sparsity.**
Something like `y = Σ βᵢxᵢ + noise` where the β vector is exactly known — m
large coefficients, k−m near-zero. Then sweep:

- sparsity `m/k` across, say, 0.1% to 30%
- factor count k across 100, 1000, 10000
- sign structure: all-positive (SB's best case), mixed signs (its failure mode)
- noise level relative to the smallest "important" effect

and record, for SB and Morris both: evaluations spent, true positives, **false
negatives**, and false positives. The false-negative curve against mixed signs
is the deliverable. If it is bad, that is the answer and no binary gets
written.

**B. Non-additive and non-monotone variants.** Add interaction terms and a
non-monotone factor and watch where SB breaks. The point is to find the
boundary of validity and document it, so the tool can refuse to run outside it
rather than return a confident wrong answer.

**C. A cost model checked against measurement.** Derive the expected evaluation
count analytically, then confirm the implementation matches. A screening method
whose cost you cannot predict in advance is not usable for planning an
expensive experiment, which is the entire use case.

**D. The Sobol g-function** with sparse `a` coefficients is a standard
high-dimensional benchmark with analytic indices, and is a better fit than
Ishigami for the additive regime. Check whether its monotonicity properties
suit SB before relying on it.

Only after A–D would I write `sb/`. And the tool should carry the assumption
test as a first-class output — a `--check-assumptions` that runs the published
validation procedure and refuses, or loudly warns, when the response looks
non-monotone or sign-mixed.

## 5. A second, smaller finding — μ\* may already be a total-index surrogate

> **RESOLVED (2026-08-06) — see §0.** The paper was read; the claim is in it,
> at §6 p.1517, but only as a *ranking* proxy. Measured: Spearman 0.923, ratio
> spread **118.6×**. There is no usable constant, so `--st-estimate` is not
> buildable and is dropped. The surviving consequence is a docs point —
> `sobol` is optional for ranking, required for variance shares — now in
> EXPANSION.md. The DGSM overlap this section suspected is real and remains an
> open E3 question.

Campolongo, Cariboni & Saltelli (2007), the paper behind the μ\* measure in
`morris`, reportedly states that **μ\* is approximately proportional to the
total-order variance-based sensitivity index S_Tᵢ.**

Local copy:
`../Activation-Geometry/sources/pdf/campolongo-2007-morris-screening.pdf`
(this one is *not* paywalled, so it is directly checkable).

If that holds with a usable constant, `morris` alone gives a cheap `S_T`
surrogate at `r(k+1)` cost instead of `N(k+2)` — which would be worth saying
out loud in the docs, and possibly worth a `morris analyze --st-estimate` flag.
It also partly overlaps E3's DGSM item, which is already framed as "cheap upper
bounds on total indices from Morris-style sampling." Check whether these are
the same observation wearing two hats before building both.

**Double check this too.** "Approximately proportional" is doing a lot of work
in that sentence, and I read it in a summary, not in the paper. The paper is
sitting right there — read it.

## 6. A doc-level observation about design strength

Not a tool proposal, just something noticed from outside that may be worth a
line in the docs.

`../Task-Specified-Quantization/tsq/oa.py` builds `OA(243, 121, 3, 2)` via
GF(3)^k and uses it to attribute per-layer effects. That is a **strength-2**
array: it balances all *pairs* of columns, so main effects come out unbiased,
but two-factor interactions are confounded with each other. The design assumes
additivity and cannot test that assumption from its own runs.

That is standard, correct Taguchi practice and not a bug. But a user reading
per-factor effects off a strength-2 array can easily believe the numbers carry
more than they do. Two cheap possibilities:

- Have `taguchi` report the strength of the array it used alongside the effects
  table, so the limitation travels with the output.
- A doc line pointing out that Morris σ (or Sobol `S_T − S_i`) on the same
  factors tests the additivity the OA assumes — which is a nice illustration of
  why the suite has all three tools rather than one.

I have verified the array construction and its strength from the source in that
repo. I have *not* verified any claim about whether TSQ's specific conclusions
are affected — that would need the interaction test actually run, which is
`../Activation-Geometry/IDEA.md` §11.3, not this note.

## 7. Suggested roadmap placement

If it survives §4, it is an E3-class item (new sensitivity method) but with a
different shape from the others — it competes with Morris rather than adding a
new question.

| Item | When it earns its place | Sketch | Validation |
|---|---|---|---|
| ~~**`sb` — sequential bifurcation**~~ **RETIRED** | — | Superseded by `morris --groups` before it was built. §0. | — |
| **`morris --groups`** — group μ\* + recursive splitting | Factor count in the hundreds-to-thousands. Unlike SB it needs no sparsity, monotonicity or sign assumption, so it does not have a validity boundary to police. | New mode on the existing binary: a `groups:` section in `.space` that must partition the factors; survivors split and re-screen. Per-factor path untouched. [`spec/morris-groups.bp`](spec/morris-groups.bp). | Already done for the estimator: `make validate` check B reproduces Campolongo Table 1. Still to do: the §4A planted-factor sweep under mixed signs, and measured cost against the analytic model. |

Dependency: none on E0–E2, though E1's CIs would make the stopping rule better
founded. Landed in EXPANSION.md as an **E3** item.

**§4's test plan survives the change of method** and is not discharged by
check B. §4A (planted coefficients, swept sparsity, mixed signs, measured
false-negative rate) and §4C (cost model checked against measurement) apply to
group μ\* exactly as they did to SB, and are carried into
`spec/morris-groups.bp` as the `bifurcation_finds_planted_factors` integration
test. §4B's non-monotone requirement is already satisfied by construction: the
g-function used throughout `validation/` is non-monotonic in every factor,
which is why it is the right benchmark and why §4's warning about Ishigami —
correct for SB — does not bind here. §4D's Sobol g-function suggestion turned
out to be the right call and is what the whole suite is built on.

---

**Summary of what to trust here.** *Rewritten 2026-08-06 — the test plan
decided.*

**Verified by code, reproducible with `make validate`:** the μ\* ↔ S_T
relationship, and its limit (ranking yes, magnitude no — 118.6× ratio spread);
group μ\* against Campolongo Table 1; the ranking-fidelity curve and the
gap-not-budget finding; and the Table 1 erratum, by two independent routes.

**Verified by reading the primary source:** Morris and Sobol cost formulas;
that μ\* extends to groups and why the absolute value is what makes it safe.

**Still unverified, and still should not be quoted:** the 19-run and
281-input sequential-bifurcation figures, and the logarithmic cost claim.
Both papers remain paywalled. This no longer blocks anything, because the
method they describe is not being built — but if anyone cites those numbers
from this note, they are citing a secondary source at two removes.

**Verified from the source in another repo:** that TSQ's array is strength 2.
Unchanged.

**What this note got right:** that the gap was real, that §2's assumptions were
the whole question, that Ishigami was the wrong benchmark and the Sobol
g-function the right one, and that §3's compressed-sensing instinct beat its own
sequential-bifurcation lead. **What it got wrong:** the specific method, and the
hope for a usable μ\* → S_T constant. Both were wrong in the way the note asked
to be corrected, which is the outcome it was written for.

---

## 8. A gap this suite does not cover: designs for 10⁵–10⁶ structured parameters

*Added 2026-08-05, from a case that came up in `../Activation-Geometry/`.*

Everything in `EXPANSION.md` and in §§1–7 above assumes the factor count is in
the tens, or at worst the hundreds with group screening. There is a class of
problem the funnel simply cannot reach, and it is not exotic — it showed up
immediately in ordinary work.

### 8.1 The concrete example

When a neuron is removed from a network layer, its contribution can be
redistributed across the survivors:

```
W_S' = W_S + W_P · B          B = C_PS · C_SS⁻¹
```

`B` is `|P| × |S|`. At a 10% prune rate on a small model that is about
307 × 2765 ≈ **850,000 continuous parameters, per layer, 28 layers**. No
orthogonal array, Morris trajectory or Sobol sample reaches that, and no amount
of group screening gets from 10⁶ to 10² without destroying the thing being
measured.

There *is* a closed form — the least-squares solution above. The problem is that
it is optimal for a **surrogate objective** (error at that layer's output) which
is measurably misaligned with the objective anyone cares about (end-to-end
loss). Measured: a criterion that beat its alternative by 5.4× on the surrogate
lost by 10× end to end. So we have an analytic optimum for the wrong question
and no way to search the right one.

### 8.2 The reduction that worked, and why it generalises

The practical escape was not to design over `B` at all, but to use analysis to
produce a **one-parameter family** and design over that:

```
W_S' = W_S + γ · W_P · B          γ ∈ {0, 0.5, 1}, one γ per layer
```

γ = 0 is plain deletion, γ = 1 is the full closed-form solution. 850,000
parameters per layer collapse to **one factor per layer**, 28 factors, a
standard L243 — and it works: the fitted per-layer γ beat every uniform setting,
and full γ = 1 was the *worst* uniform choice, which is the misalignment showing
up directly.

**The generalisable pattern: use analysis to construct a low-dimensional family
of candidate solutions, then use the DOE on the family, not on the raw
parameters.** That feels like it deserves to be a named stage in the funnel
rather than something each user reinvents. It is close in spirit to RSM (E4),
but RSM assumes you already have a handful of continuous factors; here the work
is *manufacturing* those factors from a closed-form solution you do not fully
trust.

### 8.3 The harder version: A→B→C cascades

The example above got lucky — one scalar per layer, and layers are natural
groups. The general case is worse, because these systems are **cascades**:
stage A feeds B, B feeds C, and a factor set at stage A changes the *input
distribution* that stage B's factors act on.

That breaks an assumption every design here rests on. An orthogonal array gives
unbiased main effects when the response is additive in the factors. In a
cascade, factor *i*'s effect depends on the state produced by factors 1…*i*−1,
so "the main effect of stage B" is not well defined independent of A.

It is not merely a nuisance term either — measured in this domain, early stages
**amplify** perturbations while later stages **absorb** them, so the same
nominal change has systematically different consequences depending on where in
the chain it sits.

What a method for this would need:

1. **Conditional or sequential designs** — settle stage A, re-measure, then
   design stage B against the *realised* upstream state, rather than randomising
   all stages simultaneously and hoping the interactions wash out. Related to
   sequential bifurcation (§2) but for level-setting rather than screening.
2. **An explicit propagation model** — treat the response as a composition and
   estimate per-stage transfer (amplify/absorb) rather than a single additive
   effect. Closer to path analysis or SEM than to factorial design.
3. **Sketched or matrix-variate designs** — if the parameter block genuinely
   must be searched, design over a random projection of it, or over its rows and
   columns as separate factor sets, rather than its entries. Whether
   Johnson–Lindenstrauss-style guarantees survive contact with a DOE estimator
   is an open question and would need checking, not assuming.

   **Update 2026-08-05: this method exists and has been read.** Bonsai
   ([2402.05406](https://arxiv.org/abs/2402.05406), analysed in
   `../Activation-Geometry/sources/bonsai-method-notes.md`) samples `n ≪ N`
   random binary masks over the module set, evaluates each with one forward
   pass, and solves the **underdetermined ridge regression** of utility on the
   mask to recover per-module importance. That is precisely the design this
   section was asking for — a randomised design over a parameter block far
   larger than the evaluation budget, with an explicit estimator rather than a
   heuristic. It also uses complement masks for variance reduction, citing
   Covert & Lee (2020) on binary-input regression. **Start here rather than
   inventing one.**

### 8.4 What generalizes out of §8, and what stays in the pruning repo

*Added 2026-08-06.*

§8 is where this note is most useful and most at risk of pulling the suite
off-mission. The repo is a general-purpose DOE toolkit; the case that produced
§8 is LLM compression, which lives in `../Activation-Geometry/` and
`../Task-Specified-Quantization/`. The test for anything here is whether it
survives being stated **without mentioning neural networks**. Three do.

**A. The reduction pattern (from §8.2) — a named stage, not a binary.**
Stated generally: *when the natural parameter block is too large to design over,
use analysis to construct a low-dimensional family of candidate solutions, then
run the DOE on the family rather than on the raw parameters.* The γ ∈ {0, 0.5, 1}
example collapsed 850,000 parameters per layer to one factor per layer, and the
fitted per-layer γ beat every uniform setting — including γ = 1, the analytic
optimum of the surrogate objective, which was the *worst* uniform choice. That
last detail is the point: the pattern earns its place precisely when you have a
closed form you do not fully trust. This belongs in the funnel narrative in
DESIGN.md and `spec/screening-methods.md` §4, as the stage *before* screening.
It is documentation, and it is the highest-value item in §8.

**B. Supersaturated designs with regularized recovery — the honest name for
Bonsai.** §8.3's update found Bonsai and correctly said "start here." Translated
out of the domain: sample `n ≪ k` random binary masks over the factor set,
evaluate each once, and solve the underdetermined regression of response on mask
with regularization to recover per-factor effects. That is a **supersaturated
design** — the DOE-native answer to "more factors than runs," which §3 listed
and then did not pursue. Framed that way it is a general tool, it composes with
E1's least-squares core (ridge is a small step from OLS), and it is a peer of
`morris --groups` rather than a competitor: groups need a partition and give
group-level answers, supersaturated designs give per-factor answers and need
regularization. Complement, not replacement — per EXPANSION.md's additive rule.
**Not yet specced.** It needs its own §4-style validation before a roadmap row.

**C. The non-differentiable-metric argument — positioning, for the README.**
Gradient and Hessian methods must optimize a differentiable proxy; a DOE can
take the real metric, including exact-match, pass@1, yield, or anything else
that is measured rather than computed. The measured misalignment behind this
(a criterion 5.4× better on the surrogate and 10× worse end to end) is domain
evidence, but the claim is general and it is the clearest statement of what this
suite is *for*. One paragraph in README.md, not a feature.

**What stays out.** Per-neuron screening (§1 killed it: effects are exactly
additive at block output, and the sparsity premise is 2.5× top-heavy, not
sparse); cascade-specific conditional designs (§8.3 — real, but the general
version is a research project, not a roadmap item); and anything that requires
the suite to know what a layer is.

**One more item worth lifting, from `sources/README.md` §2.** *Random Pruning*
(Liu et al., ICLR 2022) found that random selection at matched allocation beat a
carefully derived criterion by 5.8×–15.6×. Stated generally: **a factor-importance
result should be reported against a random-selection baseline at matched
budget.** That is a methodological requirement with legs far beyond compression,
and it is cheap — a null-baseline column on `morris analyze`. Adjacent to E1's
CIs and arguably more valuable, since a CI tells you your estimate is stable
while a null baseline tells you it beats not trying.

### 8.5 What I am *not* claiming

I have not searched for prior art on any of this, and "design of experiments for
high-dimensional structured parameter blocks" is a plausible enough phrase that
some of it certainly exists — supersaturated designs, sketched regression and
Bayesian optimisation over latent spaces are all adjacent. **Check before
building.** The observation here is only that the gap was hit immediately in
routine work, that the escape (analysis → low-dimensional family → DOE) was ad
hoc, and that the cascade structure broke the additivity every tool in this
suite assumes.
