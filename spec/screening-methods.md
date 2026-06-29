# Screening Methods Field Guide — Taguchi Arrays, Morris, Sobol

*For the experimenter who needs to rank 8–35 factors without running the full
factorial. Companion to design/Jun2026-audit.md §M1. Written 2026-06-12.*

---

## The one-table summary

| Method | Question it answers | Cost (k factors) | Use when | Output |
|---|---|---|---|---|
| **Taguchi OA** | "Which level of each factor is best, on average?" | ~k to 3k runs | Experiments are **expensive/noisy** (hardware, hours per trial) | Level means, S/N ranking, predicted optimum |
| **Morris** | "Which factors matter *at all*, and which act nonlinearly/interactively?" | r·(k+1) runs, r=10–20 | Model is **cheap and deterministic**; you want a global keep/drop list | μ\* (importance), σ (interaction flag) per factor |
| **Sobol** | "What *fraction of output variance* does each factor (and its interactions) own?" | N·(k+2) runs, N=256–2048 | Final attribution on the surviving factors; robustness budgeting | First-order Sᵢ and total S_Tᵢ indices |

The honest hierarchy for this project: **Morris and Sobol belong on the
simulator** (it's deterministic and costs milliseconds per run — the economy
that justifies orthogonal arrays doesn't exist there). **Taguchi belongs on
the bench**, where a trial costs you an afternoon and real noise exists.

One shared prerequisite for all three: define each factor's range so that
both ends are *physically buildable*. A screening result is only as good as
the ranges you gave it.

---

## 1. Taguchi orthogonal arrays

### What it is

An orthogonal array is a carefully chosen fraction of the full factorial with
two properties: **balance** (each level of each factor appears equally often)
and **pairwise orthogonality** (every *pair* of factors sees every level
combination equally often). Balance is what lets you estimate a factor's
main effect by simply averaging all trials at each of its levels — the other
factors' contributions cancel out *on average*.

The catch, and it is the catch that bit this project repeatedly: balance
cancels other factors' **main effects** only. If factors A and B *interact*
(their combined effect isn't the sum of their solo effects), that interaction
signal doesn't vanish — it lands on whichever column is the A×B combination.
In the GF(3) construction we use, **column 3 literally is A+B**: assign a
factor there and its apparent effect is contaminated by — or entirely made
of — the A×B interaction. That is where the "16 dB artifact" factors came
from, and why the vinegar L27 showed 3.53 dB for a factor whose true effect
is +0.03%.

### Algorithm

```
1. List factors and pick 2, 3, or 5 levels each (3 levels lets you see
   curvature; 5 if the optimum may sit mid-range — GF(5) arrays available).
2. Pick the smallest array with enough columns:
     L9 (4 cols, 3-level) … L27 (13) … L81 (40) … L243/L729,
     GF(5): L25 (6) … L625 (156) … L3125.
   Prefer 50–200% spare capacity over a saturated fit.
3. ASSIGN COLUMNS DELIBERATELY (the step everyone skips):
   - Put the factors you already know are dominant on the BASIS columns
     (the k independent generator columns: 1, p, p², … in mask order).
   - Leave the basis columns' pairwise-interaction columns EMPTY. An empty
     column's apparent "effect" estimates your aliasing + noise floor —
     if a real factor's effect isn't well above it, don't believe it.
   - Spread remaining factors over high-order-interaction columns.
4. Run all rows. For hardware: randomize run order; replicate if you can.
5. Analyze, per response variable:
     mean_level(f, L)  = average of y over the trials where f was at L
     effect(f)         = max_L mean_level − min_L mean_level
     S/N (larger-is-better) = −10·log10( (1/n) Σ 1/yᵢ² )   per level,
     Δ(f) in dB        = max_L S/N − min_L S/N
     curvature         = mid-level mean vs average of extremes (3-level only)
6. Predict the optimum additively: ŷ = grand_mean + Σ_f (best_level_mean − grand_mean).
7. CONFIRMATION RUN at the predicted optimum. If the measured value misses
   the additive prediction badly, interactions dominate → the ranking is
   not trustworthy → escalate to a focused 2-factor grid or Morris/Sobol.
```

### When it's the right tool

- Bench experiments: 9–27 trials is a weekend; 6561 is not.
- Noisy responses: the level-averaging gives you n/3 replicates per level
  estimate for free.
- **Robust design** (the under-used superpower): put control factors in an
  *inner* array and noise factors (±tannin dose, ±gap, ±anode mass, ±T) in an
  *outer* array; each inner row is scored by mean and σ across the outer
  array. Optimizing mean − k·σ finds recipes that are *insensitive to sloppy
  manufacturing* — which is precisely the series-stack reversal problem.

### Failure modes

- Main↔interaction aliasing (above). Array size does **not** fix this;
  L3125 with first-N-column assignment has the same disease.
- The additive prediction is the hypothesis, not the result — skipping the
  confirmation run means never testing it.
- On a deterministic simulator, S/N "noise" is really other-factor leakage;
  a null factor can score whole dB. Check any surprising OA result with two
  OFAT runs (it costs seconds).

---

## 2. Morris elementary effects (the screening workhorse for models)

### What it is

Randomized one-factor-at-a-time. An **elementary effect** of factor i is just
a normalized finite difference, measured somewhere in the design space:

```
EE_i = [ y(x + Δ·e_i) − y(x) ] / Δ
```

OFAT from one base point gives you that derivative *at one place*. Morris
repeats it from r random places and looks at the distribution of each
factor's EEs:

- **μ\*ᵢ = mean(|EEᵢ|)** — overall importance. Large μ\* = the factor moves
  the output somewhere in the space.
- **σᵢ = std(EEᵢ)** — inconsistency. If the effect of factor i depends on
  where you are (i.e., on the other factors), its EEs vary → large σ means
  **nonlinear or interacting**.

Reading the μ\*–σ plane:

```
σ
│            ▲ interacting / nonlinear —
│            │ matters, but don't trust its main effect alone
│   (noise)  │
│  ┌─────────┼──────────
│  │ DROP    │ KEEP, additive — safe to optimize independently
└──┴─────────┴──────────────────── μ*
```

### Algorithm

```
1. Map every factor to [0,1]. Continuous: linear (or log for i₀-like spans).
   Categorical: integer grid levels (acceptable for screening; see note).
2. Choose grid resolution p (even, typically 4) and step Δ = p / (2(p−1))
   (= 2/3 for p=4 — deliberately large: you want global, not local, slopes).
3. Build ONE trajectory (k+1 points):
     x⁰ = random point on the grid
     visit the k factors in random order; at each step move ONLY that factor
     by ±Δ (staying inside [0,1]) → x¹ … x^k
   Each adjacent pair differs in exactly one factor → one EE per factor.
4. Repeat for r trajectories (r = 10–20), fresh random starts/orders.
   Total runs: r·(k+1).            [35 factors, r=15  →  540 runs]
5. For each factor: μ* = mean |EE|,  σ = std(EE).
6. Decide: rank by μ*; flag σ ≳ μ*/2 as "interacting — handle with care";
   drop factors with μ* below the bottom cluster (refit ranges if a factor
   you *know* matters lands there — wrong range is the usual cause).
```

That's the whole method. It is robust, assumption-free, and the standard
first pass on any model with more than ~10 factors. SALib (`pip install
SALib`) implements it, or it's ~50 lines by hand.

### Failure modes

- Categoricals with no natural order make EEs jumpy — either screen within
  each category combo, or accept that σ will flag them.
- μ\* says nothing about *which level is best* — it's a keep/drop tool.
  Follow with level sweeps on the keepers.

---

## 3. Sobol indices (variance attribution)

### What it is

Treat the factors as random inputs over their ranges; the output Y then has a
variance. Sobol decomposition splits that variance into shares:

- **Sᵢ (first-order):** fraction of Var(Y) explained by factor i acting
  alone. `Sᵢ = Var(E[Y|Xᵢ]) / Var(Y)`.
- **S_Tᵢ (total-order):** fraction involving factor i in *any* way, including
  all its interactions.

The two numbers per factor tell you everything the OA couldn't:

- `S_Tᵢ ≈ 0` → factor is genuinely inert over its range — **freeze it** and
  shrink the design space honestly.
- `S_Tᵢ − Sᵢ` large → that factor works through interactions — identify the
  partner with a 2-factor grid.
- `Σ Sᵢ ≈ 1` → the system is additive and your OA results were trustworthy;
  `Σ Sᵢ ≪ 1` → it never was.

### Algorithm (Saltelli sampling)

```
1. Same [0,1] mapping as Morris. Use a quasi-random (Sobol/LHS) generator —
   NOT plain random — for ~10× faster convergence.
2. Draw two independent N×k sample matrices A and B  (N = 256–2048, power of 2).
3. For each factor i build A_B(i): matrix A with column i swapped for B's
   column i.  → k extra matrices.
4. Evaluate the model on every row of A, B, and all A_B(i):
   total runs N·(k+2).        [35 factors, N=1024  →  37,888 runs]
5. With yA = f(A), yB = f(B), yABi = f(A_B(i)) and V = Var(yA ∪ yB):
     Sᵢ   ≈  mean( yB · (yABi − yA) ) / V          (Saltelli 2010)
     S_Tᵢ ≈  mean( (yA − yABi)² ) / (2V)           (Jansen)
6. Bootstrap the rows for confidence intervals; if CIs are wide, double N.
```

### Cost control

37k runs at ~0.1–1 s each is hours, not days — feasible directly on this
simulator overnight. If a future model gets slower: fit a **surrogate**
(random forest or Gaussian process) on 2–5k LHS samples, verify its R² on a
holdout, and compute Sobol indices on the surrogate for free. SALib does the
index math either way.

### Failure modes

- Indices are defined *relative to the ranges you chose* — widen a range and
  its factor's share grows. This is a feature (it answers "what matters over
  the space I'd actually build") but report ranges with results.
- Needs many runs: never the first tool, always the last word.

---

## 4. The funnel — how they compose for this project

```
 35 factors, simulator              bench, hardware
┌──────────────────────┐
│ MORRIS  (~550 runs)  │  global keep/drop + interaction flags
└─────────┬────────────┘
          │ ~10–15 survivors
┌─────────▼────────────┐
│ SOBOL  (~15k runs)   │  variance shares, S_T−S → interaction pairs
└─────────┬────────────┘
          │ 3–6 factors that own the variance
┌─────────▼────────────┐
│ 2-FACTOR GRIDS       │  resolve the flagged interactions (e.g. Cl⁻ ×
│ (3×3 each, ~9 runs)  │  coating, pH × tannin) — exact, no aliasing
└─────────┬────────────┘
          │ candidate recipe
┌─────────▼────────────┐     ┌─────────────────────────────────┐
│ OFAT sanity (2 runs  │ ──▶ │ TAGUCHI ON HARDWARE: L9/L18/L27 │
│ per claimed effect)  │     │ worksheets + confirmation run;  │
└──────────────────────┘     │ inner×outer for stack robustness│
                             └─────────────────────────────────┘
```

**Case study from this repo (2026-06-12):** the vinegar L27 reported
acetic_acid_molarity at 3.53 dB. Two OFAT runs (seconds) showed the true
main effect is +0.03%. The 3.5 dB was interaction leakage from the
feso4/fe3_fraction columns. Rule of thumb that falls out: *on a
deterministic model, any OA effect you intend to act on costs exactly two
more runs to verify — always spend them.*

Tooling notes: `~/workspace/taguchi` generates the OAs (GF(2/3/5) through
L3125) and does the S/N analysis — right tool for the bench leg. For Morris
and Sobol either use SALib or ask for `experiments/morris_screen.py` /
`experiments/sobol_screen.py` runners wired to `CellAssembler` (audit
worklist P10).
