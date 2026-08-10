# Status & Handoff

*Working document. Current as of 2026-08-10. Read this first; it is the state
of play plus the traps worth not rediscovering.*

Companions: [DESIGN.md](DESIGN.md) (build plan M0–M7),
[EXPANSION.md](EXPANSION.md) (methods roadmap E0–E7),
[SECURITY.md](SECURITY.md) (hardening discipline and defect log),
[EXPANSION_NOTE.md](EXPANSION_NOTE.md) (research leads, mostly resolved).

---

## Where things stand

**Nine binaries ship**, all in `build/bin/`:

| stage | binaries |
|---|---|
| `screen/` | `morris` — μ\* / σ, group screening, `bifurcate` |
| `attribute/` | `sobol` — Sᵢ / S_Tᵢ with CIs, second-order pairs |
| `resolve/` | `ofat`, `grid` |
| `optimize/` | `taguchi` |
| `analyze/` | `pareto`, `regress`, `uq` |
| `orchestrate/` | `robust` |

**Green across every mode.** Zero build warnings under
`-Wall -Wextra -Werror -std=c99 -pedantic`; nine test binaries plus six shell
suites; valgrind clean on all nine; ASan/UBSan clean; both fuzzers clean;
`make validate` 8/8. Coverage **88.3% lines / 98.7% functions**.

**No known defects.**

### Milestones

| | state |
|---|---|
| M0–M5, MI | ✓ complete |
| M6 | ~ `ofat` + `grid` ✓; confirmation checker pending |
| M7 | pending — Python bindings |
| E0 | ✓ complete |
| E1 | ~ `pareto`, `regress`, `uq`, μ\* CIs, `--keep-share`, cut-gap ✓; the **Pareto chart of effects** is pending, because it needs `report` |
| E3 | ~ `morris --groups` + `bifurcate` ✓; `pawn` and `morris analyze --dgsm` pending |
| E2, E4, E5, E6, E7 | pending |

*E1 and E3 were both recorded here as complete until 2026-08-09. They are not:
each has one deliverable left, and in E1's case it was blocked on a binary the
README claimed already shipped. Counting `build/bin/` found it; re-reading the
sentence would not have.*

---

## What to build next

*Ordered by value per effort, but that is not the only axis. Read the two notes
under the heading before picking.*

**Cheapest real win: E2.** Small, fully unblocked, and it removes a guess users
currently have to make.

**The prize: E5.** Noise factors are the "robust" this project is named for,
and nothing in the toolkit does robustness-to-noise today. Everything it needs
is now in place. It is the largest single piece of unbuilt *method* and the
biggest gap between what the project is called and what it does.

### 1. E2 — `--target-ci` sequential convergence

Morris and Sobol both carry bootstrap CIs, so this is unblocked. Keep doubling
`trajectories:` / `samples:` until every CI is narrower than a target or a cap
is hit (caps per SECURITY.md H1). Must stay regenerable from the `.space` seed
alone — which is free for `sampling: sobol`, since the sequence is
deterministic, and needs care for `lhs`.

### 2. M6's confirmation checker

`ofat` and `grid` exist. Missing: the piece that compares a *predicted* optimum
against a *measured* confirmation run and says whether the additive prediction
held.

Worth more than its position suggests. `spec/screening-methods.md` §1 calls
this the hypothesis test for the whole Taguchi method — without it the toolkit
predicts an optimum and never checks whether the prediction was sound, which is
the one step that distinguishes a design of experiments from a guess with
arithmetic.

### 3. E4 → E5 — RSM, then noise factors

`rsm` + `robust funnel --optimize`; the least-squares core is already there
(`doe_ols_src` in `core/src/stats.c`). Then E5: `noise:` factors, crossed
inner×outer designs and S/N ratios.

E5 is the namesake. It is also the item most likely to change the shape of the
`.space` format, so doing it before the smaller cosmetic work avoids
re-doing that work.

### 4. `report`, and the Pareto chart that closes E1

The standalone HTML/SVG dashboard. `robust` writes its own HTML/JSON report
today, so the missing thing is the *standalone* tool — plus the Pareto chart of
effects, which is E1's one remaining deliverable and has nowhere to live until
`report` exists. **The README listed `report` as shipping until 2026-08-09.**

### 5. Smaller

M7 Python bindings; `pareto svg`; E3's remaining `pawn` tool and
`morris analyze --dgsm`; E6 (PCE, Shapley); E7 (`desire`, `objectives:`).

### 6. Engineering, not method

- **A real GCC 16 job.** The clang job catches the increment-only class that
  PR #1 hit, but it is a proxy, not an equivalent. Add gcc-16 when the runners
  carry it.
- `core/src/runner.c` stays at 77% on purpose — see Housekeeping.

### 7. Research leads — `EXPANSION_NOTE.md` §8.4

Unbuilt and still the most interesting direction: the reduction pattern
(analysis → low-dimensional family → DOE) as a *named funnel stage*;
supersaturated designs with regularized recovery, which is the honest DOE
framing of Bonsai; and the null-baseline column the Random Pruning paper argues
any importance result needs.

---

## Traps and lessons worth not rediscovering

**A display change to `analyze` is an API change.** The μ\* confidence interval
(E1, commit `8a2c342`) shipped rendered *glued* to the value —
`215.6[210,221]` — under a header advertising `mu* [95% CI]` as one column. It
read as a formatting improvement from inside the repo. Downstream,
`llama-optimize` split each row on whitespace, got `215.6[210,221]` for field 1,
and failed to parse it as a number. **Every** row failed the same way, so the
result was an *empty* ranking rather than a partial one; the caller took that
for "no factors ranked", kept all of them, and `--screen` degraded into a no-op
— after paying for `r·(k+1)` real GPU benchmark runs. Nothing errored and
nothing warned. Reported 2026-08-09, fixed 2026-08-10.

Three things came out of it, and they are the general lesson:

- **The only reliable fix is a separate contract.** `morris analyze --json` and
  `sobol analyze --json` now exist, versioned with a `schema` key, so the table
  is free to change again. Anything consuming these tools should use it.
- **A table nobody parses positionally is still parsed positionally.** Columns
  are separated, and every interval is a single space-free token (`[210,221]`)
  — `[210, 221]` would split in two and shift every column after it. The morris
  and sobol CLI suites assert this the way a consumer reads it: split on
  whitespace, demand a bare number. Reintroducing the glued format fails them.
- **Unknown options were being ignored.** `analyze ... --format json` used to
  print the human table and exit 0 — the same silent-wrong-answer shape. Both
  tools now reject an option they do not know.

Related, found while fixing it: `regress --json` and `uq --json` interpolated
factor and metric names raw. The `.space` parser rejects only control
characters, and `--metric` is argv, so one quote produced a document no parser
would accept — from the mode whose only purpose is being parsed. Both escape
now, via `doe_json_string` / `doe_json_number` in `core/src/json.c`
(the latter emits `null` for a non-finite value, since JSON has no `NaN`).


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

**One compiler is one opinion.** `-Werror` plus a single compiler means a
future toolchain can already be broken while CI is green — which is how PR #1
arrived, from GCC 16 rejecting an increment-only variable that GCC 13 accepts.
CI now builds and tests with clang as well, which flags that same pattern
today. Adding it immediately turned up ten files with no trailing newline and a
`main()` with no prototype, all latent `-Werror` failures. Run
`make all CC=clang-18` before believing a clean build.

**Silently ignoring input is worse than rejecting it.** The `.tgu` parser
treated only an indented line containing `:` as a factor and dropped every
other line in `factors:` without comment, so one mistyped line removed a factor
while the tool still *succeeded* — the array was built and run over the wrong
space with nothing in the output to say so. A mistyped top-level key
(`arry: L9`) did the same to `array:`, leaving auto-selection to pick something
else. Both are errors now. When adding a parser branch, ask what happens to
input that matches none of them.

**`make coverage` used to mix runs.** `.gcda` counters *accumulate* by design,
so before 2026-08-09 a report blended the current run with executions of code
that no longer existed — and after a header edit that moves a `static inline`,
gcovr failed outright ("function on multiple lines"). The target now deletes
`.gcda` first, which resets counters without forcing a recompile. If a coverage
number ever looks implausibly good, suspect this before believing it.

**Coverage pointed at the right file, not the easy one.** The three worst files
were `sobol.c` (61%), `space.c` (78%) and `csv.c` (81%) — and the uncovered
lines were almost entirely *rejection paths* plus, in sobol's case, the whole
success path of `sobol_analyze_pairs`. The second-order estimator was pinned
only by `make validate` check F, which `make coverage` does not run, so the
numbers users see for `second_order: true` had no unit test behind them at all.
Now 90% / 91% / 98%. Chasing the percentage would have found none of that;
reading *which lines* were missing found all of it.

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
