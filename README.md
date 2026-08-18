# Robust

A small, fast, POSIX-compliant suite of command-line tools for designing and
analyzing robust experiments — screening, variance attribution, optimization,
response surfaces and robust parameter design — built in C over a shared
`libdoe` core.

```
  many factors                                                    one setting
       │                                                                ▲
       ▼                                                                │
    MORRIS  ──►  SOBOL  ──►  GRID / OFAT  ──►  TAGUCHI  ──►  CONFIRM  ──►  RSM
  what matters?  how much,   which pair,      best of the   did that      where
                 and do they  and is that     levels tried  prediction    exactly
                 interact?    effect real?                  hold?         is the peak?

                              ROBUST — and does it survive what you can't control?
```

Each stage narrows the question and spends more runs on fewer factors. **That
order is the method**: an exhaustive search over everything is exactly what
this suite exists to avoid.

`robust` runs the first two stages as one command. `report` turns any of it into
a page you can hand someone.

> ### 👉 New here? [**One experiment, every tool**](examples/cookies/)
>
> Seven things you could change about a batch of cookies, and a weekend. It
> walks the whole suite over that one problem in the order you would actually
> use it — which factors matter, how much, which ones interact, what the best
> setting is, whether that prediction survives contact with reality, and
> whether the recipe still works when your oven runs hot.
>
> No statistics needed, and every command runs in under a second.

## The binaries

| Binary | Role |
|---|---|
| `morris`  | screening — μ\* (importance), σ (interaction flag), group screening |
| `sobol`   | variance attribution — Sᵢ, S_Tᵢ, second-order pairs |
| `ofat`    | one-factor-at-a-time confirmation — is that effect real? |
| `grid`    | small full-factorial — resolve an interaction exactly |
| `taguchi` | orthogonal arrays: optimize, `confirm` a prediction, `robust` design |
| `rsm`     | response surface — quadratic fit, stationary point |
| `pareto`  | multi-objective frontier — filter + accumulating `.front` store |
| `desire`  | Derringer–Suich desirability — several metrics into one |
| `regress` | SRC/SRRC + R² — the *direction* of each effect |
| `uq`      | output distribution — percentiles, histogram, skew |
| `report`  | self-contained HTML — Pareto chart of effects |
| `robust`  | funnel orchestrator — screen → attribute in one command |

Every tool but one takes the shared `.space` factor format. `taguchi` uses
`.tgu`, which adds the orthogonal array and the `noise:` section a crossed
design needs. All of them share a C core
(`core/libdoe`) holding the PRNG, sampling, factor scaling, the fork/env run-loop,
CSV, JSON reading and writing, and stats. See **[STATUS.md](STATUS.md)** for where things stand and what is next,
**[DESIGN.md](DESIGN.md)** for the full plan, and
**[EXPANSION.md](EXPANSION.md)** for the methods roadmap beyond it.

## Building

```bash
make                # build libdoe and all twelve binaries into build/bin/
make test           # every suite, then valgrind (fails the build on a leak)
make test-asan      # the same suites under ASan/UBSan
make test-bindings  # the Python binding's suite
make fuzz           # seedable fuzz of every parser that reads untrusted input
make validate       # reproduce published results against closed-form ground truth
make coverage       # line/function/branch coverage (needs gcovr for totals)
make clean
```

`make test` runs valgrind when it is present and says plainly when it is not —
a skipped memory check is never reported as a pass. `make validate` is separate
from `make test` because it pins the *claims* the roadmap rests on rather than
our code; see [validation/](validation/README.md).

`make` builds `libdoe` and every tool binary into `build/bin/`. **`taguchi` is a
peer like any other** — this Makefile compiles it, there is no sub-make, and its
two suites are part of `make test`, so they get the same valgrind and ASan/UBSan
discipline as everything else. `make install` covers the whole suite.

## Layout — one directory per stage of use

Directories are named for **what you do** at that point in the funnel, so a new
tool has an obvious home the moment you know which question it answers.

```
core/         libdoe — PRNG, sampling, .space parsing + scaling, fork/env
              runner, results CSV, JSON, stats. Every tool builds on it.

screen/       morris/            "which factors matter at all?"
attribute/    sobol/             "how much variance, and which interactions?"
resolve/      ofat/  grid/       "is that effect real, and who does it pair with?"
optimize/     taguchi/  rsm/     "what is the best, most robust setting?"

analyze/      pareto/  regress/  uq/  report/  desire/
              consume results — no new sampling, no model runs

orchestrate/  robust/            drives the whole funnel, emits the report

examples/     cookies/           the worked example, and the tests that keep
                                 every command in it honest

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

**All twelve binaries above ship**, covering the funnel end to end: screening
through attribution, resolution, optimization, confirmation, response surfaces
and robust design, with the analyze stage alongside.

All suites pass under `-Werror`, valgrind and ASan/UBSan, with adversarial-input
coverage and parser fuzzing per [SECURITY.md](SECURITY.md). Coverage is 90.0%
lines / 100% functions.

**Claims are validated too, not just code.** [`validation/`](validation/README.md)
reproduces the published results this project relies on against closed-form
ground truth — which confirmed `sobol` implements the estimators its source
recommends, established where μ\* stops being a usable proxy for the total Sobol
index, and turned up an erratum in a 2007 paper's published table
([standalone reproduction](sources/campolongo-2007-morris-screening_erratum/)).

`sobol` samples with a **Joe-Kuo low-discrepancy sequence** by default
(`sampling: sobol`), built as its source specifies — matrices A and B are the
left and right halves of one 2k-dimensional sequence, not two draws. It is
bit-for-bit identical to the authors' own reference generator, and measured
against the same closed form it is 3× more accurate than Latin Hypercube at
N=256 and **66× at N=65536**.

Still to build: PCE surrogates and the two remaining sensitivity methods
(`pawn`, `--dgsm`); see [STATUS.md](STATUS.md).

## Driving these tools from another program

**Every command that emits a design, a ranking, a measurement or a
recommendation takes `--json`**, and that is the interface:

| stage | commands |
|---|---|
| screen | `morris analyze`, `morris bifurcate` |
| attribute | `sobol analyze` |
| resolve | `ofat`, `grid` |
| optimize | `taguchi generate`, `taguchi analyze`, `taguchi effects`, `taguchi confirm`, `taguchi robust`, `taguchi list-arrays`, `rsm analyze` |
| analyze | `regress`, `uq` |
| orchestrate | `robust screen`, `robust funnel` (`--json PATH`, or `-` for stdout) |
| present | `report` reads those documents |

`morris sample`, `sobol sample`, `taguchi generate --csv` and the `pareto`
commands emit CSV by design, and `validate` answers with its exit status.

The text tables are a display for people. They are laid out to stay positionally
parseable, but they will keep changing, and a program that parses them will keep
breaking — so parse the JSON, and check its `schema`, which is bumped only when
a key is renamed or removed.

Every document leads with `tool`, `command` and `schema`, so a consumer can
identify what it is holding before it reads a field. Documents also carry the
run count they were computed from — `runs` — because a ranking is only as good
as the rows behind it, and knowing there were nine of them is how you tell a
complete analysis from a partial one.

Diagnostics — near-tie cuts, overlapping intervals, an all-inert result — go to
**stderr in every mode**, so `--json` never buys a clean-looking document at the
price of the warning that made it worth reading.

<details>
<summary>Why this is a contract and not a convenience</summary>

`morris analyze` once printed μ\* glued to its new confidence interval
(`215.6[210,221]`). A downstream tool split each row on whitespace, failed to
parse *every* row identically, and read the resulting empty ranking as "no
factors matter" — so it skipped its screening stage after paying for hours of
real benchmark runs, with nothing erroring and nothing warning.

That is why the tables are no longer the interface. See
[screen/morris/README.md](screen/morris/README.md#--json--the-machine-readable-contract).
</details>

## License

Public Domain (CC0), matching `taguchi`.
