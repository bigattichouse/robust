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


## Confidence intervals and the keep rule

`morris analyze` reports a 95% bootstrap interval on each μ\*, resampling
**trajectories** — the independent unit — and seeded from the `.space` so the
interval is reproducible from the file alone.

```sh
morris analyze model.space results.csv --keep-share 0.9
```

`--keep-share S` keeps the top factors until their cumulative μ\*-share reaches
`S` — an 80/20 cut rather than a fixed count. That is deliberate:
`make validate` check C measured that a **fixed** cut is only as trustworthy as
the gap it lands in. On the g-function the top-5 cut was 100% correct from
r=20, while the top-3 cut never resolved at *any* budget because it fell inside
a 1% tie. A share-based cut at least lands where the mass runs out.

Two warnings fire on stderr when the cut is not trustworthy:

- the gap at the boundary is under 5% — *"not resolvable at any trajectory
  count; keep both, or separate them with `sobol`"*
- the confidence intervals of the last kept and first dropped factor **overlap**,
  so their order is not established at this trajectory count

stdout is unchanged either way, so pipelines are unaffected. Both warnings also
fire in `--json` mode, for the same reason they exist at all.


## `converge` — stop guessing `trajectories:`

```sh
morris converge model.space ./run.sh --target-ci 5 [--max-trajectories N]
```

Doubles `trajectories:` and re-screens until every 95% CI on μ\* is narrower
than the target, then tells you the number to write into the `.space`. That
number reproduces the run exactly — the design is a pure function of
`(factors, r, grid_levels, seed)` — so converging once and recording the answer
keeps the file self-contained.

Hitting the cap **exits non-zero** and says so. A budget that cannot resolve the
ranking is a finding, and a script must not read a wide interval as a narrow
one. Raise `--max-trajectories`, widen the target, or accept that this response
will not give a resolvable ranking at a price you want to pay.

`--target-ci` is an absolute width in response units, because that is what the
interval is measured in. Read one `analyze` run first if you need a sense of
scale.

## `--json` — the machine-readable contract

```sh
morris analyze model.space results.csv --keep-share 0.9 --json
```

The table is a **display**; `--json` is the **interface**. Use it from anything
that consumes `analyze` programmatically.

```json
{
  "tool": "morris", "command": "analyze", "schema": 1,
  "mode": "per-factor", "metric": "response",
  "trajectories": 20, "runs": 84, "factor_count": 3,
  "total_mu_star": 324.4, "all_zero": false,
  "factors": [
    {"factor": "kv_type", "rank": 1, "mu": 215.6, "mu_star": 215.6,
     "mu_star_lo": 210.1, "mu_star_hi": 221.3, "sigma": 8.067,
     "share": 0.6645, "interacting": false}
  ],
  "keep": {"share_requested": 0.9, "share_achieved": 0.973, "count": 2,
           "factors": ["kv_type", "n_depth"], "ci_overlap_at_cut": false},
  "cut_at": "keep-share", "gap_at_cut": 24.38, "cut_is_tie": false
}
```

A `groups:` file produces `"mode": "group"` with a `groups` array
(`group`, `mu_star`, `sigma`, `members`) in place of `factors`, plus
`per_factor_runs` — what the same screening would have cost per factor.

Notes on the fields:

- **`schema`** is bumped when a key is renamed or removed, never for an
  addition. Refuse a document whose schema you do not know rather than parsing
  it wrongly.
- **`gap_at_cut`** is `mu_star[last kept] / mu_star[first dropped]`, and
  **`cut_is_tie`** is true when that ratio is under 1.05 — the ranking is then
  not resolvable at any trajectory count. Both keys are always present: `null`
  and `false` when no `--keep-share` was given (no cut was requested), and
  `gap_at_cut` is also `null` when the dropped factor's μ\* is zero, i.e. the
  separation is infinite.
- **`cut_at`** says where that gap was measured: `"keep-share"` for the
  `--keep-share` boundary, `null` when no cut was requested, and `"bottom"` in
  group mode, where `analyze` is given no cut and reports the gap at the bottom
  of the ranking — the same pair the stderr warning describes.
- **`all_zero`** repeats the stderr note that no factor moved the response.
  A consumer will never read that text, and it is the difference between
  "nothing matters" and "the harness is broken".

### Why this exists

`analyze` once printed μ\* glued to its interval — `215.6[210,221]` — so a
consumer splitting the row on whitespace got a field 1 that would not parse as
a number. Every row failed identically, which yielded an *empty* ranking rather
than a partial one; the caller read that as "nothing to rank" and skipped
screening entirely, after paying for `r·(k+1)` real benchmark runs. Nothing
errored and nothing warned.

The columns are separated now (and every interval stays one space-free token,
so it cannot split either), but the durable fix is this mode: it lets the table
keep evolving without breaking anyone. `sobol analyze` carries the same flag
and the same guarantee.
