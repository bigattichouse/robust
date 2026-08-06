# Robust

A small, fast, POSIX-compliant suite of command-line tools for designing and
analyzing robust experiments — **Morris** screening, **Sobol** variance
attribution, and **Taguchi** optimization — built in C with a shared-library
core for easy language bindings.

```
many factors ──► MORRIS ──► survivors ──► SOBOL ──► key factors ──► TAGUCHI / grids
              "what matters?"           "how much, and which   "what is the best,
               μ*  (importance)          interactions?"          robust setting?"
               σ   (interaction flag)    Sᵢ, S_Tᵢ (variance)     level means, S/N
```

These are **stages of maturity, not competitors**: Morris and Sobol run on a cheap
deterministic simulator to find what matters; Taguchi runs on the bench to optimize it.
`robust` orchestrates the funnel. Every tool is a small POSIX binary over a shared
`libdoe` core; [`taguchi`](optimize/taguchi/) — the original tool this grew from — is the `optimize/` stage.

## The binaries

| Binary | Role |
|---|---|
| `taguchi` | optimization / bench screening (in `taguchi/`) |
| `morris`  | factor screening — μ\* (importance), σ (interaction flag) |
| `sobol`   | variance attribution — Sᵢ (first-order), S_Tᵢ (total) |
| `robust`  | funnel orchestrator + unified report |
| `pareto`  | multi-objective frontier — filter + accumulating `.front` store |
| `ofat`    | one-factor-at-a-time confirmation runs |
| `grid`    | small full-factorial interaction grids |
| `report`  | unified HTML/SVG dashboard |

All of them share one `.space` factor-definition format and a common C core
(`core/libdoe`) holding the PRNG, sampling, factor scaling, the fork/env run-loop,
CSV/JSON, and stats. See **[DESIGN.md](DESIGN.md)** for the full plan and
**[EXPANSION.md](EXPANSION.md)** for the methods roadmap beyond it.

## Building

```bash
make            # build libdoe + morris, sobol, robust, pareto, and taguchi
make test       # run every suite, then valgrind (fails the build on a leak)
make test-all   # also run the taguchi suite
make test-asan  # the same suites under ASan/UBSan
make fuzz       # seedable fuzz of every parser that reads untrusted input
make validate   # reproduce published results against closed-form ground truth
make clean
```

`make test` runs valgrind when it is present and says plainly when it is not —
a skipped memory check is never reported as a pass. `make validate` is separate
from `make test` because it pins the *claims* the roadmap rests on rather than
our code; see [validation/](validation/README.md).

`make` builds `libdoe` and every tool binary into `build/bin/`. **`taguchi` is a
peer like any other** — this Makefile compiles it, there is no sub-make, and its
two suites are part of `make test`, so they get the same valgrind and ASan/UBSan
discipline as everything else. `make install` covers the whole suite. Further
tools land per the DESIGN.md roadmap.

## Layout — one directory per stage of use

Directories are named for **what you do** at that point in the funnel, so a new
tool has an obvious home the moment you know which question it answers.

```
core/         libdoe — PRNG, sampling, .space parsing + scaling, fork/env
              runner, results CSV, JSON, stats. Every tool builds on it.

screen/       morris/            "which factors matter at all?"
attribute/    sobol/             "how much variance, and which interactions?"
resolve/      ofat/  grid/       "is that effect real, and who does it pair with?"
optimize/     taguchi/           "what is the best, most robust setting?"

analyze/      pareto/  regress/  uq/  report/
              consume results — no new sampling, no model runs

orchestrate/  robust/            drives the whole funnel, emits the report

validation/   reproduces published results against closed-form ground truth
sources/      reference papers, with an errata directory for one of them
spec/         .bp specifications, blueprints, the screening-methods field guide
```

The split that matters: **`analyze/` consumes results; every other stage
generates a design and spends runs.** `ofat` and `grid` live under `resolve/`
rather than `analyze/` for exactly that reason — they exist to buy new runs.

Binaries all land in `build/bin/` regardless of source location, so this layout
is free to evolve without breaking anything downstream.

## Status

**`morris`, `sobol`, and the `robust` funnel are built and tested** (M0–M4), and
**`taguchi` is folded in** as a peer tool: the `common/` core (seedable PRNG, `.space`
parsing + scaling, fork/env runner, results CSV, stats), `morris` (μ\*/σ screening),
`sobol` (Saltelli Sᵢ/S_Tᵢ with bootstrap CIs), and `robust` (Morris → Sobol funnel with
HTML/JSON reports and `.tgu` hand-off). **`pareto`** (E1) is built: the non-dominated
frontier over multi-metric results, as both a stateless filter and a `.front` store
that accumulates across experiment batches.

All suites pass under `-Werror`, valgrind, and ASan/UBSan, with adversarial-input
coverage and parser fuzzing per [SECURITY.md](SECURITY.md).

**Claims are validated too, not just code.** [`validation/`](validation/README.md)
reproduces the published screening results this project relies on against
closed-form ground truth — which confirmed `sobol` implements the estimators its
source recommends, established where μ\* stops being a usable proxy for the total
Sobol index, and turned up an erratum in a 2007 paper's published table
([standalone reproduction](sources/campolongo-2007-morris-screening_erratum/)).

See [DESIGN.md](DESIGN.md) and [EXPANSION.md](EXPANSION.md).

## License

Public Domain (CC0), matching `taguchi`.
