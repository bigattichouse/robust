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

## Recursive splitting — `morris bifurcate`

Screen a partition, drop what the keep rule rejects, split each survivor in
two, repeat:

```
$ morris bifurcate model.space ./run.sh --keep-share 0.95
Bifurcating 16 factors, keep-share 0.95
  worst-case budget : 186 evaluations
  per-factor would be: 102

Round 1:
  g2                             10        4  keep
  g1                              0        4  drop
...
2 survivor(s) of 16 factors, 3 round(s), 90 evaluations (worst case was 186)
  x3
  x9
```

**The cost is printed before anything runs.** A screening method whose price
you learn afterwards is not usable for planning an expensive experiment, which
is the whole reason to screen.

**Two limits worth knowing before you reach for it:**

- **The worst-case budget can exceed per-factor screening** (186 vs 102
  above), because the bound assumes every group survives and splits every
  round. Bifurcation is a bet that importance is *concentrated*. It paid there
  — 90 actual — but when importance is diffuse, screening every factor
  directly is cheaper and simpler.

Measured on 64 factors with 8 important ones at alternating signs: 64 → 8
survivors, 4 rounds, 460 evaluations against 650, **zero false negatives**.
The alternating signs matter — that is the case that defeats sequential
bifurcation, and it is why this uses the absolute group effect.

**Use at least 20 trajectories.** Taking the absolute value of the group effect
stops cancellation from biasing the *mean* to zero, but not from happening in
individual trajectories. With too few, a group holding equal-and-opposite
factors can fall below the keep cut — and a dropped factor is silent. Measured
on 1024 factors with 8 important ones at alternating signs, 20 seeds per row:

| trajectories | opposing pairs sharing a group | spread apart |
|---|---|---|
| 10 | **2 of 8 missed** | 0 |
| 20 / 40 / 80 | 0 | 0 |

`morris bifurcate` warns on stderr below 20.

Stopping: all groups singletons, nothing survives, `max_rounds`, or the
keep/drop cut falls inside a near-tie. That last one matters — `make validate`
check C measured that a boundary inside a tie never resolves at any trajectory
count, so the tied groups are all kept rather than chosen between arbitrarily.
