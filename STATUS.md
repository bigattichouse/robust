# Status & Handoff

*Working document. Current as of 2026-08-07. Read this first; it is the state
of play plus the traps worth not rediscovering.*

Companions: [DESIGN.md](DESIGN.md) (build plan M0–M7),
[EXPANSION.md](EXPANSION.md) (methods roadmap E0–E7),
[SECURITY.md](SECURITY.md) (hardening discipline and defect log),
[EXPANSION_NOTE.md](EXPANSION_NOTE.md) (research leads, mostly resolved).

---

## Where things stand

**Eleven binaries ship**, all in `build/bin/`:

| stage | binaries |
|---|---|
| `screen/` | `morris` — μ\* / σ, group screening, `bifurcate` |
| `attribute/` | `sobol` — Sᵢ / S_Tᵢ with CIs, second-order pairs |
| `resolve/` | `ofat`, `grid` |
| `optimize/` | `taguchi` |
| `analyze/` | `pareto`, `regress`, `uq` |
| `orchestrate/` | `robust` |

**Green across every mode.** Zero build warnings under
`-Wall -Wextra -Werror -std=c99 -pedantic`; nine test binaries plus five shell
suites; valgrind clean on all nine; ASan/UBSan clean; both fuzzers clean;
`make validate` 6/6. Coverage **85.1% lines / 95.5% functions**.

**No known defects.**

### Milestones

| | state |
|---|---|
| M0–M4, MI | ✓ complete |
| M5 | ~ second-order ✓ built and validated exactly; **Joe-Kuo pending** |
| M6 | ~ `ofat` + `grid` ✓; confirmation checker pending |
| M7 | pending — Python bindings |
| E0, E1, E3 | ✓ complete |
| E2, E4, E5, E6, E7 | pending |

---

## What to build next, in order

### 1. Joe-Kuo low-discrepancy sequence (M5's other half)

The largest single item and the one with a real trap already documented.

`doe_sample_sobol()` in `core/src/sample.c` is a stub returning `-1`; `sobol`
currently uses LHS. Saltelli et al. (2010) §7 names quasi-random sampling as
one of its four best-practice choices, so this is the last gap between our
`sobol` and its source.

**Read the warning at the draw site in `attribute/sobol/src/lib/sobol.c`
before starting.** §5.1 p.263 of that paper requires `A` and `B` to be the
**left and right halves of a single 2k-dimensional sequence**, not two
k-dimensional draws taken in sequence. The latter is correct for LHS and wrong
for a QR sequence, which is deterministic — restarting reproduces points and
continuing correlates them. The contract is also spelled out in `doe.h`.

Needs an embedded direction-number table. `DOE_MAX_FACTORS` is 1024, so decide
deliberately how many dimensions to ship and **error clearly above that** — do
not silently fall back to LHS.

Validation is ready: `make validate` check E already drives `sobol` against the
g-function closed form, so a correct QR sequence should hold or improve those
errors (currently 0.0078 on S₁, 0.0055 on S_T at N=65536).

### 2. E2 — `--target-ci` sequential convergence

Now unblocked: Morris and Sobol both carry bootstrap CIs. Keep doubling
`trajectories:` / `samples:` until every CI is narrower than a target or a cap
is hit (caps per SECURITY.md H1). Must stay regenerable from the `.space` seed
alone.

### 3. M6's confirmation checker

`ofat` and `grid` exist. Missing: the piece that compares a *predicted*
optimum against a *measured* confirmation run and says whether the additive
prediction held. That is the step `spec/screening-methods.md` §1 calls the
hypothesis test for the whole Taguchi method.

### 4. E4 → E5 — RSM, then noise factors

`rsm` + `robust funnel --optimize` (the E1 least-squares core in
`core/src/stats.c` is already there — `doe_ols_src`). Then `noise:` factors,
crossed inner×outer designs and S/N ratios, which is the "robust" the project
is named for and the largest single piece of unbuilt *method*.

### 5. Smaller

M7 Python bindings; `pareto svg`; the Pareto chart in `report`; E6 (PCE,
Shapley); E7 (`desire`, `objectives:`).

### 6. Research leads — `EXPANSION_NOTE.md` §8.4

Unbuilt and still the most interesting direction: the reduction pattern
(analysis → low-dimensional family → DOE) as a *named funnel stage*;
supersaturated designs with regularized recovery, which is the honest DOE
framing of Bonsai; and the null-baseline column the Random Pruning paper argues
any importance result needs.

---

## Traps and lessons worth not rediscovering

**The build had no header dependencies until 2026-08-06.** Editing `doe.h`
rebuilt some objects and not others, and the link silently combined two struct
layouts — `sobol` read `samples` from the wrong offset and reported 0. Nothing
warned. `-MMD -MP` and the `.d` includes are in the Makefile now; **do not
remove them.**

**`off += snprintf(...)` is the recurring defect here.** It appeared three
separate times (pareto's error builder, the serializer's growth path, a test
buffer). `snprintf` returns the length it *would* have written; adding that to
an offset walks past the buffer, after which the remaining-size argument goes
negative and converts to an enormous `size_t`. Use the clamping pattern.

**Scripted doc edits silently no-op on stale anchors.** Three edits this
session matched nothing and reported success. Assert the anchor.

**A test that only fails under a sanitizer is a false guarantee.** The first
regression test for pareto's overflow passed against the buggy code in a plain
build. The durable version brackets the buffer in sentinel regions and catches
it in every build mode. Prefer that over relying on `make test-asan`.

**Validate claims, not just code.** `EXPANSION_NOTE.md` carried three
load-bearing claims from secondary sources; reading the primary paper overturned
two and turned up an erratum in its published table. `make validate` exists for
this and should grow whenever a decision rests on a source.

**A validation suite must drive the shipped code.** Check B originally tested
its own reimplementation of group μ\*, which proves nothing about what users
run. It now drives `morris_group_analyze`, with the prototype retained only as
an independent second opinion that must agree to 1e-9.

---

## Measurements worth keeping

- **μ\* ranks like S_T but gives no magnitude** — Spearman 0.923, ratio spread
  **118.6×**. So `sobol` is optional for a ranking and required for variance
  shares. (`make validate` check A.)
- **μ\*'s reliability is set by the index gap, not the budget** — top-5 keep is
  100% correct from r=20; top-3 never resolves at any r, because that cut falls
  inside a 1.0% tie. This is why `--keep-share` and the cut-gap warning exist.
  (check C.)
- **Group screening needs r ≥ 20** — at r=10, 2 of 8 important factors were
  missed when equal-and-opposite factors shared a group; at r≥20, none.
  `morris bifurcate` warns below 20.
- **Bifurcation's advantage grows with k** — 1.4× cheaper at k=64, 4.1× at 256,
  **12.8× at 1024**, zero false negatives throughout.
- **Second-order needs far more samples than first-order** — on a model with a
  real `a×b` interaction, N=512 gave S₂ = −0.009 (noise) and N=8192 gave 0.057.

---

## Housekeeping notes

- `core/src/runner.c` reads 77%, not higher, because the remaining lines are
  `fork`/`pipe`/`exec` failure paths and the final `_exit` statements. This is
  near-irreducible; the child-attribution problem was already fixed with
  `__gcov_dump()` under `-DDOE_COVERAGE`.
- `sources/pdf/` is gitignored; `sources/fetch.sh` re-fetches what is public.
  Two papers are paywalled and supplied manually — see `sources/README.md`.
- `sources/campolongo-2007-morris-screening_erratum/` is a self-contained,
  shareable reproduction of a published-table error, with a drafted summary. It
  has not been sent to the authors.
