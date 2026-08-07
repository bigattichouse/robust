# morris — elementary-effects screening

Ranks many factors by importance (**μ\***) and flags nonlinear / interacting ones
(**σ**) at `r·(k+1)` model runs — the first screening stage of the `robust` funnel.
Answers *"which factors matter at all?"*

- **Input:** the shared `.space` factor format (`trajectories: r`, `grid_levels: p`, `seed:`).
- **Commands (planned):** `sample | generate | run | analyze | validate`.
- **Output:** per-factor μ\*, σ; ranked keep/drop list; σ ≳ μ\*/2 flagged.

**Responses must be finite.** A run that yields `inf`/`nan` (e.g. a "never
converges" sentinel) fails analysis with a clean error rather than propagating
into μ\*/σ — have your model clamp to a large finite penalty instead.

Status: planned — see [../DESIGN.md](../DESIGN.md) §4 and roadmap **M2**.
Builds on `core/libdoe` (PRNG, `.space` scaling, run-loop).


## Group screening

A `groups:` section in the `.space` switches every subcommand to group
screening — `r(G+1)` runs instead of `r(k+1)`:

```
factors:
  w1: 0, 1
  w2: 0, 1
  w3: 0, 1
groups:
  encoder: w1, w2
  decoder: w3
```

```
$ morris analyze model.space results.csv
Morris GROUP effects (metric: response) — 10 trajectories, 2 groups over 3 factors
30 runs, against 40 for per-factor screening.

Group                         mu*       spread  members
encoder                        14        9.661        2
decoder                         1    7.746e-07        1
```

**The file decides, not a flag.** Group mode is selected by the presence of
`groups:`, because the design and the analysis must agree — a flag would let
you generate one and analyse the other, which is a wrong answer rather than an
error.

Groups must **partition** the factors: every factor in exactly one group.
Overlap would move a factor twice in a single step, making the group effect
ill-defined; an uncovered factor would silently never be screened. Both are
errors.

**Why the absolute value matters.** The measure is the mean *absolute* group
effect, `|d_u| = |y(X~) − y(X)| / Δ`, so a group holding two important factors
with **opposing signs still registers**. That is the silent false negative
sequential bifurcation suffers from, designed out rather than tested for.
Source: Campolongo, Cariboni & Saltelli (2007), *Environmental Modelling &
Software* **22**(10) 1509–1518, §3.3 p.1512 — reproduced against that paper's
Table 1 in `make validate` check B, which drives this exact code.

`spread` is the variability of the absolute group effect. It is **not** the
per-factor σ's interaction flag; do not read it that way. To localise within a
surviving group, split it and re-run.

Spec: [`../../spec/morris-groups.bp`](../../spec/morris-groups.bp).
