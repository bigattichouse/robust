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
`libdoe` core; [`taguchi`](taguchi/) — the original tool this grew from — lives here as a peer.

## The binaries

| Binary | Role |
|---|---|
| `taguchi` | optimization / bench screening (in `taguchi/`) |
| `morris`  | factor screening — μ\* (importance), σ (interaction flag) |
| `sobol`   | variance attribution — Sᵢ (first-order), S_Tᵢ (total) |
| `robust`  | funnel orchestrator + unified report |
| `ofat`    | one-factor-at-a-time confirmation runs |
| `grid`    | small full-factorial interaction grids |
| `report`  | unified HTML/SVG dashboard |

All of them share one `.space` factor-definition format and a common C core
(`common/libdoe`) holding the PRNG, sampling, factor scaling, the fork/env run-loop,
CSV/JSON, and stats. See **[DESIGN.md](DESIGN.md)** for the full plan.

## Building

```bash
make            # build libdoe + morris, sobol, and taguchi
make test       # run the core + morris + sobol suites (valgrind if present)
make test-all   # also run the taguchi suite
make clean
```

`make` builds `libdoe`, the `morris`/`sobol` binaries (into `build/bin/`), and
`taguchi` (via `make -C taguchi`); further tools land per the DESIGN.md roadmap.

## Layout

```
common/   shared C core → libdoe.a   (PRNG, sample, space, runner, csv, json, stats)
morris/   sobol/   robust/           method tools (lib + cli), per DESIGN.md
tools/    ofat/ grid/ report         cross-cutting binaries
taguchi/  the taguchi tool           own Makefile, bindings, full history
spec/     design notes               idea, screening-methods, blueprints
```

## Status

**`morris` and `sobol` are built and tested** (M0–M3), and **`taguchi` is folded in**
as a peer tool: the `common/` core (seedable PRNG, `.space` parsing + scaling, fork/env
runner, results CSV, stats), plus `morris` (μ\*/σ screening) and `sobol` (Saltelli
Sᵢ/S_Tᵢ with bootstrap CIs). All suites pass under `-Werror` and valgrind. Next: the
`robust` funnel (M4). See [DESIGN.md](DESIGN.md).

## License

Public Domain (CC0), matching `taguchi`.
