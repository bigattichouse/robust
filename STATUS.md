# Status & Handoff

*Working document. Current as of 2026-08-09. Read this first; it is the state
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
`make validate` 8/8. Coverage **85.5% lines / 98.2% functions**.

**No known defects.**

### Milestones

| | state |
|---|---|
| M0–M5, MI | ✓ complete |
| M6 | ~ `ofat` + `grid` ✓; confirmation checker pending |
| M7 | pending — Python bindings |
| E0, E1, E3 | ✓ complete |
| E2, E4, E5, E6, E7 | pending |

---

## What to build next, in order

### 1. E2 — `--target-ci` sequential convergence

Now unblocked: Morris and Sobol both carry bootstrap CIs. Keep doubling
`trajectories:` / `samples:` until every CI is narrower than a target or a cap
is hit (caps per SECURITY.md H1). Must stay regenerable from the `.space` seed
alone.

### 2. M6's confirmation checker

`ofat` and `grid` exist. Missing: the piece that compares a *predicted*
optimum against a *measured* confirmation run and says whether the additive
prediction held. That is the step `spec/screening-methods.md` §1 calls the
hypothesis test for the whole Taguchi method.

### 3. E4 → E5 — RSM, then noise factors

`rsm` + `robust funnel --optimize` (the E1 least-squares core in
`core/src/stats.c` is already there — `doe_ols_src`). Then `noise:` factors,
crossed inner×outer designs and S/N ratios, which is the "robust" the project
is named for and the largest single piece of unbuilt *method*.

### 4. Smaller

M7 Python bindings; `pareto svg`; the Pareto chart in `report`; E6 (PCE,
Shapley); E7 (`desire`, `objectives:`).

### 5. Research leads — `EXPANSION_NOTE.md` §8.4

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

**When a reference implementation exists, pin to it, not to your own output.**
Every Sobol-sequence constant in `core/tests/test_doe.c` came from compiling
and running Joe & Kuo's own `sobol.cc`. A checksum computed from our own
generator would have passed identically on day one and pinned nothing. The
sequence matches theirs bit-for-bit across 4096×300 points, 65536×8 points, and
dimensions 513–1024; `sources/fetch.sh` re-downloads what is needed to redo it.

**Mutation-test a new suite before believing it.** All eight deliberate breaks
of the Sobol implementation were caught — but the first harness reported three
of them as *uncaught* because the test binary **segfaulted** and the harness
only grepped for the string `FAIL`. A mutation harness must treat a nonzero
exit as a catch, and must assert both its anchor and its build. Two of the
three "gaps" were harness bugs; the third was a no-op substitution.

**A test's expected value can be wrong in an interesting way.** The first
version of `test_qr_halves_coincide_only_at_rows_0_and_1` asserted one
coinciding row (the origin) and failed. There are always exactly **two**: row 0
is the origin, and row 1 is the centre of the cube in every dimension, because
`m_1` must be odd and `< 2` in every dimension so `m_1 = 1` is forced and
`v_1 = 1/2` identically. Those two rows contribute nothing to either estimator
— 0.2% of a default N=1024 design. That is a property of the unscrambled
sequence the paper prescribes, not a defect, and it is now pinned so any change
in the cost is visible.

**`strtok` cannot support line numbers, and quietly pretends it can.** It
collapses runs of its delimiter, so blank lines never become tokens and a
counter driven by it under-reports — the taguchi parser would have called a
factor on line 7 "line 4". That is why the `line_num` an external PR removed
had never been wired to anything: it *could not* have been right. Both parsers
now walk with `strchr(line, '\n')`, which preserves blank lines. If a third
parser appears, use that scan.

**Locate errors at one site, not at every `snprintf`.** Both parsers attach
"line N:" by rewriting the finished message once — in `space.c` at the single
post-loop exit, in the taguchi parser where `parse_factor_line` returns.
Threading a line number through every helper is how one of them ends up
reporting the wrong line. Increment the counter at the *top* of the loop body,
before anything can fail, or the `continue` paths each need their own and one
eventually gets missed.

**Whole-file errors must not get a line number.** "No factors defined", the
group-partition checks and the resource caps are properties of the file; a line
number would point at something that is not wrong. Both parsers raise these
after the loop, below the prefix site, and tests assert the *absence* of a line
number — that half is the easy thing to break by prefixing everything.

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
- **Quasi-random sampling beats LHS by a widening margin** — same estimator,
  same g-function, only the sampler changed: **3.0×** more accurate at N=256,
  11.3× at 4096, **65.8× at 65536**. The gap grows because it is a convergence
  *rate* difference, not an offset. This is why `sampling: sobol` is the
  default. (check G.) It also cut check E's worst error from 0.0078 to 0.0001.
- **A non-power-of-two `samples:` can cost more and deliver less** — N=20000
  gave **4.5× the error of N=16384** while running 22% more points, because the
  sequence's uniformity is a property of aligned 2^m blocks (Saltelli §5.1
  consideration 1). The `sobol` CLI notes this when it sees one. (check G.)
- **Joe & Kuo's Property A boundary is exactly where they say** — their page
  claims dimension 1111 for the D(6) set; measured as a GF(2) rank, it holds
  through 1111 and first fails at **1112**. This is what caps `sobol` at 512
  factors (2 dimensions each), not storage. (check H.)

---

## Housekeeping notes

- `core/src/runner.c` reads 77%, not higher, because the remaining lines are
  `fork`/`pipe`/`exec` failure paths and the final `_exit` statements. This is
  near-irreducible; the child-attribution problem was already fixed with
  `__gcov_dump()` under `-DDOE_COVERAGE`.
- `sources/pdf/` is gitignored; `sources/fetch.sh` re-fetches what is public.
  Two papers are paywalled and supplied manually — see `sources/README.md`,
  which summarises every source with citations and URLs precisely because the
  files themselves cannot be redistributed.
- **The Joe-Kuo direction numbers are the exception: BSD-licensed and
  redistributable.** `core/src/sobol_dirnum.h` is a generated 1024-dimension
  slice that ships in-repo with the copyright notice in full. Regenerate with
  `core/tools/gen_sobol_dirnum.sh` after `sources/fetch.sh`; the dimension
  count is static-asserted against `DOE_SOBOL_MAX_DIM` in `doe.h`, so a
  regeneration at a different cap is a build error rather than a silent
  disagreement. `make validate` check H's second half *skips*, visibly, when
  the full data file is absent.
- `sources/campolongo-2007-morris-screening_erratum/` is a self-contained,
  shareable reproduction of a published-table error, with a drafted summary. It
  has not been sent to the authors.
