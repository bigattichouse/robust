# Robust — A Design-of-Experiments / Sensitivity-Analysis Toolkit

*Build plan. Companion to `spec/idea.txt`, `spec/screening-methods.md`, and the
two blueprint prompts. Written 2026-06-28.*

`robust` is a set of small, composable C binaries — modelled directly on the
existing `taguchi` tool — that together cover the full sensitivity-analysis
funnel for design-of-experiments work:

```
many factors ──► MORRIS ──► survivors ──► SOBOL ──► key factors ──► TAGUCHI / grids
              "what matters?"           "how much, and which   "what is the best,
               μ*  (importance)          interactions?"          robust setting?"
               σ   (interaction flag)    Sᵢ, S_Tᵢ (variance)     level means, S/N
```

These are **stages of maturity, not competitors** (idea.txt). Morris and Sobol
belong on a cheap deterministic simulator; Taguchi belongs on the bench. `robust`
is the orchestrator that runs the funnel and chains the stages together.

---

## 1. Decisions (settled)

| Decision | Choice |
|---|---|
| Repo structure | **Monorepo umbrella.** `morris/`, `sobol/`, `common/`, `robust/` live here, and `taguchi` is folded in as a peer subdir `taguchi/` (full history preserved via `git mv`). The GitHub repo is renamed taguchi→robust to keep its stars + redirects. See §12. |
| Language | **C99**, mirroring taguchi: library-first, shared + static lib + thin CLI, `-Wall -Wextra -Werror -std=c99 -pedantic`, valgrind-clean, CC0. |
| Code sharing | **Shared `common/` core** (`libdoe`) linked by every tool. |
| Delivery | A **set of standalone binaries** (Unix style), unified by one shared `.space` file format and the `robust` orchestrator. |

Open items deferred to build time: Sobol low-discrepancy sequence vs. LHS-first
(§5.2), second-order Sobol indices, and when to add language bindings (§9).

---

## 2. The taguchi template → what transfers, what is new

taguchi's spine is **generate → run → analyze**. Morris and Sobol are *new math
in the same skeleton*; that is exactly why taguchi is a template and not just
inspiration.

| taguchi piece | morris / sobol equivalent | New work |
|---|---|---|
| `.tgu` (discrete levels) | `.space` (continuous **ranges**: linear / log / categorical) | new parser + factor scaling |
| `arrays.c` (GF orthogonal arrays, deterministic) | Morris trajectories / Sobol Saltelli matrices | **PRNG + sampling** (taguchi has none) |
| `generate` → runs | same; design must be **reconstructable** for analysis | seed in `.space`, re-derive (§3.2) |
| `run` (fork, `TAGUCHI_*` env, CSV back) | identical (`MORRIS_*`, `SOBOL_*`) | lift into `common/runner` |
| `analyze` (main effects, S/N) | μ*/σ (Morris); Sᵢ/S_Tᵢ + bootstrap (Sobol) | new stats per tool |
| `serializer.c` (JSON) | same | move to `common/json` |

Two genuinely new capabilities, both in `common/`:
1. **A deterministic, seedable PRNG** — never `rand()`; reproducible across
   platforms (PCG32 / splitmix64 seeding).
2. **Sampling** — Latin Hypercube + (later) a Sobol low-discrepancy sequence,
   plus the Morris trajectory builder.

---

## 3. Shared infrastructure

### 3.1 The `.space` file format

One format defines a factor space once; every tool reads the keys it needs.
A cousin of `.tgu`, but factors are **ranges**, not enumerated levels.

```
# distillation.space — factor space for screening
factors:
  reflux_ratio:     1.0, 8.0                 # linear continuous range [min, max]
  catalyst_load:    1e-4, 1e-1  log          # log-scaled (min > 0)
  feed_temp:        320, 410                  # linear
  packing:          random, structured, gauze # categorical (ordinal grid)
  recycle:          true, false              # boolean → 2-level categorical

seed:         20260628    # makes every design reproducible
# Morris keys
trajectories: 15          # r  (10–20)
grid_levels:  4           # p  (even; Δ = p / (2(p-1)))
# Sobol keys
samples:      1024        # N  (power of 2, 256–2048)
second_order: false
```

- **Scaling** (`common/space`): map u∈[0,1] → real value.
  - linear: `lo + u·(hi−lo)`
  - log:    `exp(ln lo + u·(ln hi − ln lo))`  (requires `lo > 0`)
  - categorical: `levels[ clamp(floor(u·m), 0, m−1) ]` (screening caveat: no
    natural order → Morris σ will flag it; Sobol indices still valid).
- `robust to-tgu` converts a `.space` (survivors) into a taguchi `.tgu` for the
  bench stage, so the funnel's hand-off is one command.

### 3.2 Stateless design reconstruction (key design point)

taguchi's `analyze` re-reads the `.tgu` and regenerates the array to map
`run_id → levels`. Morris/Sobol do the **same trick**: because sampling is
seeded from the `.space` file, `analyze` regenerates the identical design and
knows, for each `run_id`, which trajectory/factor moved (Morris) or which block
A / B / A_B⁽ⁱ⁾ it belongs to (Sobol). No hidden state, no sidecar required.

`--design out.json` optionally dumps the design for auditing; analysis never
*depends* on it.

### 3.3 `common/` core — `libdoe` (static lib)

| Module | Responsibility |
|---|---|
| `prng`   | Seedable PCG32 / splitmix64; uniform doubles; reproducible. |
| `sample` | LHS, Sobol sequence (later), Morris trajectory builder. |
| `space`  | `.space` parser; factor scaling [0,1]↔real (linear/log/categorical). |
| `runner` | fork + `setenv` + per-point script execution (lifted from taguchi `run`). |
| `csv`    | Multi-metric results CSV parsing (`--metric`), reused verbatim from taguchi. |
| `json`   | JSON serialization for bindings / chaining. |
| `stats`  | mean, variance, std, percentiles, bootstrap CIs. |
| `viz`    | Self-contained SVG/HTML primitives (dark theme, no external deps). |

Provisional names (`libdoe`, `doe.h`) — flagged for review. The shared lib is
distinct from the `robust` orchestrator *binary*.

---

## 4. `morris/` — the screening workhorse

Randomized one-factor-at-a-time. Answers **"which factors matter at all, and
which act nonlinearly / through interactions?"** Cost: `r·(k+1)` runs.

**Generate.** Build `r` trajectories of `k+1` points. Per trajectory: random
start on the grid; visit factors in random order; at each step move *only* that
factor by ±Δ, staying in [0,1]. Each adjacent pair differs in exactly one
factor → one elementary effect for that factor. (v1 = field-guide construction;
v2 = Morris (1991) B* matrix + Campolongo optimized trajectories for spread.)

**Analyze.** For each factor, over its `r` elementary effects
`EEᵢ = [ y(x + Δeᵢ) − y(x) ] / Δ` (Δ signed):

- **μ\*ᵢ = mean(|EEᵢ|)** — overall importance.
- **σᵢ = std(EEᵢ)** — inconsistency ⇒ nonlinear / interacting.
- Rank by μ*; flag `σ ≳ μ*/2` as "interacting — handle with care"; emit a
  keep/drop list. (Refit ranges if a known-important factor lands in DROP — a
  bad range is the usual cause.)

**CLI:** `morris sample|generate|run|analyze|validate <file.space>`
(`run` sets `MORRIS_<factor>` env vars; `analyze` takes `--metric`).

---

## 5. `sobol/` — variance attribution

Treats factors as random over their ranges and splits Var(Y) into shares.
Answers **"what fraction of output variance does each factor own, including
hidden interactions?"** Cost: `N·(k+2)` runs (`N·(2k+2)` with second order).

### 5.1 Saltelli sampling + estimators

Draw two independent N×k matrices **A**, **B**; build **A_B⁽ⁱ⁾** (A with column
i taken from B). Evaluate `yA=f(A)`, `yB=f(B)`, `yABᵢ=f(A_B⁽ⁱ⁾)`. With
`V = Var(yA ∪ yB)`:

- First order:  **Sᵢ ≈ (1/N) Σⱼ yBⱼ · (yABᵢⱼ − yAⱼ) / V**   (Saltelli 2010)
- Total order:  **S_Tᵢ ≈ (1/2N) Σⱼ (yAⱼ − yABᵢⱼ)² / V**       (Jansen 1999)
- Bootstrap the N rows → confidence intervals; if CIs are wide, double N.

Diagnostics emitted: `S_Tᵢ ≈ 0` → freeze the factor; `S_Tᵢ − Sᵢ` large → works
through interactions (find the partner with a 2-factor grid); `Σ Sᵢ ≈ 1` →
additive (the OA / Taguchi ranking was trustworthy); `Σ Sᵢ ≪ 1` → it never was.

### 5.2 Sampler scope

True Sobol indices want a **low-discrepancy sequence** (Joe & Kuo 2008 direction
numbers — public-domain data, vendor a subset). That is the harder piece. Plan:
ship **LHS first** (needs only the PRNG; correct; ~10× slower convergence per the
field guide), then drop in the Sobol sequence behind the same interface.

**CLI:** `sobol sample|generate|run|analyze|validate <file.space>`.

---

## 6. `robust/` — the orchestrator ("ideal as one tool")

Reads one `.space`, runs the funnel, tracks survivors between stages, and
produces a combined report. The "stages of maturity" made executable.

| Command | Does |
|---|---|
| `robust funnel <file.space> <script>` | Morris → auto-drop low-μ* factors → Sobol on survivors → unified report; emits a `.tgu` for the bench. |
| `robust screen <file.space> <script>` | Morris stage only → reduced `.space`. |
| `robust attribute <file.space> <script>` | Sobol stage only. |
| `robust report <morris.json> <sobol.json> [taguchi.csv]` | Unified HTML/SVG dashboard. |
| `robust to-tgu <file.space>` | Emit taguchi `.tgu` for the survivors. |

**Implemented (M4):** `robust funnel` and `robust screen`, with `--keep-fraction`
and `--html`/`--json`/`--tgu` outputs. The keep rule drops factors with
μ* < `keep_fraction`·max(μ*) (default 0.1), always retaining the top factor.
Rather than shell out to the `morris`/`sobol` *binaries*, `robust` **links their
libraries** and drives the funnel in-process (`morris_design_build`/`_analyze`,
`sobol_*`) — no PATH dependency, and the orchestration is unit-testable end to
end against in-process evaluators. It shells out only to the user's model script,
via `doe_run_capture` (the script prints one number to stdout).

---

## 7. Cross-cutting analysis binaries

Small, independently useful tools that the funnel leans on (your field guide's
hard-won rules):

| Binary | Purpose |
|---|---|
| `ofat`   | One-factor-at-a-time confirmation around a base point. *"Any OA effect you act on costs exactly two more runs to verify"* — directly targets the aliasing / "16 dB artifact" failure mode. |
| `grid`   | Small full-factorial (2–3 factors, 3×3) to **resolve** interactions Sobol's `S_Tᵢ−Sᵢ` flags — exact, no aliasing. |
| `report` | Standalone unified HTML/SVG: Morris μ*–σ scatter, Sobol Sᵢ/S_Tᵢ tornado bars, Taguchi main-effects + S/N. (Also callable as `robust report`.) |

Later / optional: a **confirmation-run checker** (Taguchi additive prediction vs.
measured optimum → "interactions dominate?"), Sobol **convergence diagnostics**
(CI width vs. N), run-**failure-fraction** reporting (anti-pattern #4), and a
Python **surrogate fitter** (RF/GP on LHS → Sobol on the surrogate) for slow models.

---

## 8. The set of binaries

| Binary | Status | Role |
|---|---|---|
| `taguchi` | folded in (`taguchi/`) | optimization / bench screening |
| `morris`  | **new** | factor screening (μ*, σ) |
| `sobol`   | **new** | variance attribution (Sᵢ, S_Tᵢ) |
| `robust`  | **new** | funnel orchestrator + report |
| `ofat`    | **new** | OFAT confirmation runs |
| `grid`    | **new** | 2–3 factor interaction grids |
| `report`  | **new** | unified HTML/SVG dashboard |

MVP = `morris`, `sobol`, `robust`. Extended = `ofat`, `grid`, `report`.

---

## 9. Repository layout

```
robust/
├── README.md                 # umbrella overview + quick start
├── DESIGN.md                 # this document
├── Makefile                  # builds common → each tool; install; test-all
├── spec/                     # existing design docs
│
├── common/                   # shared C core → libdoe.a
│   ├── include/doe.h
│   └── src/  prng.* sample.* space.* runner.* csv.* json.* stats.* viz.*
│
├── morris/  include/ src/{lib,cli}/ tests/
├── sobol/   include/ src/{lib,cli}/ tests/
├── robust/  src/cli/ tests/         # orchestrator
├── tools/   ofat/ grid/ report/     # cross-cutting binaries
└── taguchi/                         # the taguchi tool — own Makefile, bindings, history
```

Each tool mirrors taguchi's `lib` (opaque handles, `error_buf` pattern) + thin
`cli`. Top-level `Makefile` builds `common` first, then each tool links
`libdoe.a` statically (CLI has no runtime `.so` dependency, like taguchi).

---

## 10. Testing & numerics

Mirror taguchi: `test_framework.h`, `-Werror`, valgrind-clean, integration +
shell CSV tests. Validate the math against **closed-form benchmarks**:

- **Sobol** — the **Ishigami function** and **Sobol g-function** have analytic
  Sᵢ/S_Tᵢ; assert estimates fall within bootstrap CIs of the known values.
- **Morris** — Morris (1991) test function with a known μ*/σ ordering; assert the
  keep/drop ranking and the interaction flags.
- **Determinism** — same seed ⇒ byte-identical design (cross-platform).
- **Scaling** — linear/log/categorical round-trips; log requires `lo > 0`.

Reproducibility is a first-class requirement: the PRNG is seedable and platform-
independent, and any design can be regenerated from the `.space` file alone.

---

## 11. Roadmap

**Status: M0–M4 complete** — common core + `morris` + `sobol` + the `robust`
funnel orchestrator, all suites green under `-Werror` and valgrind.

| Milestone | Deliverable | |
|---|---|---|
| **M0** | Repo skeleton, top-level Makefile, `common/` stubs, taguchi submodule (interim). | ✓ |
| **M1** | `common` core: prng, space parser+scaling, runner, csv, json, stats — unit + determinism tests. | ✓ |
| **M2** | `morris` (sample/generate/run/analyze); validated on linear + interaction functions. | ✓ |
| **M3** | `sobol` Saltelli + Sᵢ/S_Tᵢ with bootstrap CIs; validated against Ishigami. | ✓ |
| **M4** | `robust funnel`/`screen` (Morris→Sobol, in-process) + self-contained HTML/JSON report + `.tgu` hand-off; orchestrated-process tests. | ✓ |
| **M5** | Sobol low-discrepancy sequence (Joe-Kuo); optional second-order indices. | |
| **M6** | `ofat` + `grid` + confirmation checker. | |
| **MI** | **Taguchi integration** — folded in + GitHub repo renamed to robust (§12). | ✓ |
| **M7** | Python (ctypes) bindings mirroring taguchi; CI running `make test`. | |

Beyond M7 — new methods (PAWN, DGSM, RSM, noise factors, PCE), post-run
analysis (SRC/SRRC, UQ summaries, Pareto charts, convergence targets), and
multi-response Pareto fronts: see **EXPANSION.md**.

## 12. Taguchi integration (done 2026-06-29)

`robust` will absorb `taguchi` into one repo so users get every tool from a
single clone. taguchi has ~7 stars, so migration cost is low.

**Mechanism — rename, don't re-publish.** Rename the GitHub `taguchi` repo to
`robust`: GitHub keeps its stars/watchers/forks/issues (same repo object) and
301-redirects every old `…/taguchi` URL and git remote, so existing links keep
working. (Caveat: don't later create a *new* repo named `taguchi`, which would
shadow the redirect.) The renamed repo becomes the canonical umbrella; the
scaffolding built in the interim `workspace/robust` folder migrates into it and
the interim submodule is removed (a repo can't submodule itself).

**Timing.** Do it once `morris` and `sobol` are ready (now) so the repo isn't
fronted by stubs. The GitHub rename is the user's action; the local reorg is ours.

**Layout: (B) — done.** taguchi's files were `git mv`'d into `taguchi/` (full
history preserved); the umbrella root now holds `common/ morris/ sobol/ robust/
tools/`. taguchi keeps its own `Makefile`/bindings; the top-level `Makefile`
builds the new tools and delegates `make -C taguchi`.

**Done.** The GitHub repo was renamed **taguchi → robust** (stars + redirects
intact) and the consolidation is pushed — live at `github.com/bigattichouse/robust`
(`origin` = `git@github.com:bigattichouse/robust.git`).
```
