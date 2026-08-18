# Status & Handoff

*Working document. Current as of 2026-08-10. Read this first; it is the state
of play plus the traps worth not rediscovering.*

Companions: [DESIGN.md](DESIGN.md) (build plan M0–M7),
[EXPANSION.md](EXPANSION.md) (methods roadmap E0–E7),
[SECURITY.md](SECURITY.md) (hardening discipline and defect log),
[EXPANSION_NOTE.md](EXPANSION_NOTE.md) (research leads, mostly resolved).

---

## Where things stand

**Twelve binaries ship**, all in `build/bin/`:

| stage | binaries |
|---|---|
| `screen/` | `morris` — μ\* / σ, group screening, `bifurcate` |
| `attribute/` | `sobol` — Sᵢ / S_Tᵢ with CIs, second-order pairs |
| `resolve/` | `ofat`, `grid` |
| `optimize/` | `taguchi`, `rsm` |
| `analyze/` | `pareto`, `regress`, `uq`, `report`, `desire` |
| `orchestrate/` | `robust` |

**Green across every mode.** Zero build warnings under
`-Wall -Wextra -Werror -std=c99 -pedantic`; nine test binaries plus six shell
suites; valgrind clean on all nine; ASan/UBSan clean; both fuzzers clean;
`make validate` 8/8. Coverage **90.0% lines / 100% functions**.

**No known defects.**

### Milestones

| | state |
|---|---|
| M0–M5, MI | ✓ complete |
| M6 | ✓ complete — `ofat`, `grid`, `taguchi confirm`, `report` |
| M7 | ~ Python bindings ship and are CI-gated (299 checks), but they drive the CLI as a **subprocess**, not the shared library via **ctypes**, which is what M7 asks for |
| E0 | ✓ complete |
| E1 | ✓ complete — the Pareto chart of effects ships in `report` |
| E3 | ~ `morris --groups` + `bifurcate` ✓; `pawn` and `morris analyze --dgsm` pending |
| E2 | ✓ complete — `morris converge` + `sobol converge` |
| E5 | ✓ complete — `noise:`, crossed designs, S/N ratios, `taguchi robust` |
| E4 | ✓ complete — `rsm`: central composite design, quadratic fit, stationary point |
| E7 | ~ `pareto` ✓ and `desire` ✓; the front-vs-scalar tooling is complete |
| E6 | ~ PCE pending; Shapley is a stated non-goal until `.space` supports correlated inputs |

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

### 1. M7 — ctypes bindings

The Python bindings ship and are CI-gated, but they drive the CLI as a
subprocess. M7 asks for ctypes against `libtaguchi.so`, which the build already
produces. The `--json` contracts make the subprocess route perfectly workable,
so this is about the *cost* of a fork per call, not correctness.

### 2. ~~M6's confirmation checker~~ — built 2026-08-10

`taguchi confirm <file.tgu> <results.csv> [--measured V]` predicts the response
at the recommended settings and, given a measured confirmation run, says
whether the additive prediction held.

That was the largest conceptual gap in M0–M7: an orthogonal array never runs
the combination it recommends, so the additive prediction is a hypothesis and
nothing in the analysis tested it. `spec/screening-methods.md` — "the additive
prediction is the hypothesis, not the result; skipping the confirmation run
means never testing it."

The verdict is judged against the largest main effect, which is the size of the
thing the design claims to have measured. It is a sanity check, not a
significance test: that needs an error variance, and a saturated array has no
degrees of freedom to estimate one. The output says so.

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

The same report came back for `taguchi`, and there the parse was already
losing data. Its Python binding scraped `generate` and `effects`, matching the
effects table with `\s*(\w+)\s+([\d.]+)\s+(.+)` and skipping non-matching
lines — and `\w+` does not match a factor named `kv-type`, so that factor was
dropped from the analysis with no error and no gap in the output. `generate`,
`analyze` and `effects` take `--json` now, and the recommendation carries each
level's VALUE rather than only the index `taguchi_recommend_optimal` names.

Two things that made those tests weak, both fixed: the CLI fixture's responses
were perfectly balanced (every level mean exactly 20.000, every range 0), so
maximize and minimize picked the same level and the recommendation logic was
untested — flipping max to min left the suite green. And `escape_json_string`
allocated `len*2+1` while a control character needs six bytes, passing those
bytes through raw.

Rather than wait for a third report, the remaining commands that emit a
decision or a measurement were given the same treatment: `ofat` and `grid` (the
CONFIRMATION stage -- a silent partial parse there means believing you
confirmed an effect you did not), `robust screen` (whose whole output is a
keep/drop decision, and whose `--json` was documented "funnel only" although it
builds the same result), and `morris bifurcate`. Every command that emits a
design, ranking, measurement or recommendation now has a machine path; `sample`
and `pareto` were already CSV, and `validate` answers with its exit status.

Related, found while fixing it: `regress --json` and `uq --json` interpolated
factor and metric names raw. The `.space` parser rejects only control
characters, and `--metric` is argv, so one quote produced a document no parser
would accept — from the mode whose only purpose is being parsed. Both escape
now, via `doe_json_string` / `doe_json_number` in `core/src/json.c`
(the latter emits `null` for a non-finite value, since JSON has no `NaN`).


**`-include` sets the default goal, and that broke a bare `make`.** From
2026-08-06 to 2026-08-10, `-include $(DEPFILES)` sat above `all:` in the
Makefile. make takes its default goal from the first target it sees in *any*
makefile, `-include` counts, so once a build had emitted `.d` files a bare
`make` built one already-current object and stopped:

```
$ make
make: 'build/pareto/lib/pareto.o' is up to date.
```

Success, and nothing built. Every incremental rebuild after the first silently
produced stale binaries. Reported from outside by someone who spent a while
concluding that a shipped feature "wasn't implemented", against a binary a day
older than the source. **A fresh checkout is immune** (no `.d` files yet) and so
was CI (every step named a goal) — which is why it survived, and why the CI step
that guards it now deliberately runs a bare `make`. `.DEFAULT_GOAL := all` is
pinned above the include; `make check-default-goal` fails if that moves.

Same shape as the header-dependency bug the include was added to fix, and the
glued-CI bug above: **wrong but silent, reported as success.** That is the
failure mode this project keeps producing, so it is the one to look for.

**The last function with no coverage was returning a wild pointer.**
`taguchi_run_get_factor_names` cast a `char[MAX_FACTORS][MAX_FACTOR_NAME]` to
`const char **` and returned it. That is not a reinterpretation, it is a
different data structure: the 2D array is contiguous character storage with no
pointers in it, so `names[0]` read the first eight bytes of the first factor's
NAME and used them as an address. With a factor called "alpha" a caller follows
`0x00006168706c61`. The header has always documented a NULL-terminated array of
names. Chasing function coverage from 98% to 100% is what found it — it was the
only function in the tree nothing had ever called.

The first fix was wrong too, instructively: it cached the pointer table behind a
"built yet?" flag living in memory the run's allocator does not zero, so it
worked in a standalone program and failed inside the test suite. Rebuilt every
call now. Filling k pointers is cheaper than the mistake.

**`robust funnel` had zero CLI coverage** — the orchestrator's headline command,
the one the README leads with. It runs in 0.15s at test sizes, so there was
never a cost reason. Adding it immediately caught a regression from this
session: `funnel --json -` printed the progress banner, both tables and
"Wrote JSON: -" onto stdout AROUND the document, so piping it to a parser
failed. Introduced while adding `-` support to the JSON writer.

**A test suite outside the build is a test suite nobody runs.** The Python
binding at `optimize/taguchi/bindings/python` has 11 test files and 260-odd
checks, and none of it was wired into `make` or CI. Two consequences, both
found on 2026-08-10:

- Its `core.py` searched `optimize/taguchi/build/taguchi` — where taguchi put
  its binary back when it built itself with a sub-make — **before** the
  umbrella `build/bin/taguchi`. That stale file survives in older trees, so the
  suite was running against a binary **four days old** and reporting passes.
  Fixed: umbrella path first, `$TAGUCHI_CLI` overrides.
- **22 of its checks fail on a clean machine**, and did so before any of this
  session's work (verified by running the same suite against the pre-change
  binary: 22 failures either way, none introduced, none fixed). They are not
  code bugs — they assume `/usr/bin/taguchi` exists and assume the legacy build
  path. Until they are hermetic they cannot gate the build.

So `make test-bindings` runs **only** `tests/test_cli_contract.py`, which is
hermetic and does gate the build. Making the other 22 hermetic is the follow-up.

**The binding reads `--json` now.** It scraped the human tables in four
places, and `\w+` in the effects regex dropped a factor named `kv-type` from
the analysis silently. All four now go through one reader,
`taguchi/_cli_json.py`, which raises where the scrapers skipped. The two
`xfail(strict=True)` cases in `test_cli_contract.py` are plain passing tests
again, which is the switch's acceptance criterion met.

Coverage of the binding is **84% by line but only partly enforced** — the whole
suite covers 84%, and `test_cli_contract.py` is what gates the build. That gap,
not the 84%, is what let the `kv-type` drop ship.

Working through the non-hermetic checks took the suite from **37 broken
(33 failures + 4 errors) to 13**, and turned up four real defects rather than
just environment coupling:

- **`from taguchi import TaguchiError` caught nothing the library raised.** The
  package exported `class BackwardCompatibleTaguchiError(TaguchiError,
  _OriginalTaguchiError)` — a SUBCLASS of both layers' error types. `except`
  matches a class or its ancestors, so a subclass of both catches neither. Both
  now derive from one base (`_base_error.py`) and the export is that base.
- **`core_enhanced.py` had its own stale binary discovery**, missing the
  umbrella `build/bin/` path and reading a different env var
  (`TAGUCHI_CLI_PATH` vs `TAGUCHI_CLI`). Same defect as `core.py`, missed
  because the logic exists twice.
- **`TAGUCHI_DEBUG=1` and `=yes` silently did nothing** — the parser tested
  `.lower() == "true"` only.
- Fixtures scoped inside one test class, so a second class's four tests errored
  on a missing fixture instead of running.

It is now at **zero**: all 306 checks pass, and `make test-bindings` runs the
WHOLE directory rather than the one hermetic file. Getting there turned up five
more real defects — every batch did, which is the point:

- **`Experiment.from_tgu(path)` deleted the caller's file.** `cleanup()`
  unlinked whatever `_tgu_path` named, and `from_tgu` pointed that at the file
  it was handed, so `with Experiment.from_tgu("my_experiment.tgu"): ...`
  destroyed the input on the way out of the block. Silent, immediate,
  unrecoverable, and present in BOTH layers. Ownership is tracked now rather
  than inferred from "the path is set".
- **The binding could not see L18.** `_get_arrays_info` scraped with a regex
  requiring a NUMBER before "levels"; a mixed-level array prints "mixed", so
  L18 never matched. `list_arrays()` returned 19 of 20 and
  `get_array_info("L18")` raised "not found" — the one array designed for
  mixed-level factors was unreachable. Both layers read `list-arrays --json`
  now, and `levels` is null for mixed rather than 0.

- **`CommandExecutionError` never escaped `_run_command`.** `raise error` sat
  inside the `try`, so the following `except Exception` caught and rewrapped it
  as a plain `TaguchiError` — `exit_code` and `stderr` discarded, and
  `except CommandExecutionError` never matching.
- **`Taguchi(cli_path=...)` could be silently overridden** by whatever
  `TAGUCHI_CLI_PATH` was set to in the shell: the enhanced layer searched the
  environment variable BEFORE the explicit argument. An argument is a decision;
  an environment variable is a default.
- **`summary()` could never report incomplete data.** It builds a "Data
  completeness: X%" line and a missing-runs warning, but called `main_effects()`
  first — which raises on missing runs. Both lines were unreachable in the only
  situation they exist for. It reports now; `main_effects()` stays strict,
  because an unbalanced design gives biased level means.

The `from_tgu` data loss needed fixing **twice**: `cleanup()` was not the only
place that unlinked. `__del__` did its own, checking only that `_tgu_path` was
set, so a `from_tgu` that RAISED left a half-built object whose destructor
deleted the file the user was asking about. The first fix passed its test
because that test only covered the with-block. Both paths are covered now.

**The binding is one implementation now, not two.** It used to carry
`core.py`/`core_enhanced.py`, `experiment.py`/`experiment_enhanced.py`,
`analyzer.py`/`analyzer_enhanced.py` and two unrelated `TaguchiError` classes.
Comparing the public surfaces settled it: the enhanced classes were a strict
superset — no method existed only on the originals, and the only signature
difference was `summary()` gaining an optional argument with a default. So the
merge was mostly deletion.

One class per concept, one error hierarchy, canonical module names, and the
`Original*` aliases are gone. Shared internals live in `_discovery.py` (finding
the CLI binary), `_tgu.py` (parsing `.tgu` and owning the temp file) and
`_cli_json.py` (reading `--json`). Net ~750 lines removed.

This mattered because every defect in the duplicated code had to be found and
fixed twice, and usually was not: the stale-binary search, the file-deleting
destructor, the `.tgu` parser, and an exception hierarchy where the exported
`TaguchiError` caught nothing.

**A test suite outside the build is a test suite nobody runs.** The Python
binding at `optimize/taguchi/bindings/python` has 11 test files and 260-odd
checks, and none of it was wired into `make` or CI. Two consequences, both
found on 2026-08-10:

- Its `core.py` searched `optimize/taguchi/build/taguchi` — where taguchi put
  its binary back when it built itself with a sub-make — **before** the
  umbrella `build/bin/taguchi`. That stale file survives in older trees, so the
  suite was running against a binary **four days old** and reporting passes.
  Fixed: umbrella path first, `$TAGUCHI_CLI` overrides.
- **22 of its checks fail on a clean machine**, and did so before any of this
  session's work (verified by running the same suite against the pre-change
  binary: 22 failures either way, none introduced, none fixed). They are not
  code bugs — they assume `/usr/bin/taguchi` exists and assume the legacy build
  path. Until they are hermetic they cannot gate the build.

So `make test-bindings` runs **only** `tests/test_cli_contract.py`, which is
hermetic and does gate the build. Making the other 22 hermetic is the follow-up.

**The binding reads `--json` now.** It scraped the human tables in four
places, and `\w+` in the effects regex dropped a factor named `kv-type` from
the analysis silently. All four now go through one reader,
`taguchi/_cli_json.py`, which raises where the scrapers skipped. The two
`xfail(strict=True)` cases in `test_cli_contract.py` are plain passing tests
again, which is the switch's acceptance criterion met.

Coverage of the binding is **84% by line but only partly enforced** — the whole
suite covers 84%, and `test_cli_contract.py` is what gates the build. That gap,
not the 84%, is what let the `kv-type` drop ship.

Working through the non-hermetic checks took the suite from **37 broken
(33 failures + 4 errors) to 13**, and turned up four real defects rather than
just environment coupling:

- **`from taguchi import TaguchiError` caught nothing the library raised.** The
  package exported `class BackwardCompatibleTaguchiError(TaguchiError,
  _OriginalTaguchiError)` — a SUBCLASS of both layers' error types. `except`
  matches a class or its ancestors, so a subclass of both catches neither. Both
  now derive from one base (`_base_error.py`) and the export is that base.
- **`core_enhanced.py` had its own stale binary discovery**, missing the
  umbrella `build/bin/` path and reading a different env var
  (`TAGUCHI_CLI_PATH` vs `TAGUCHI_CLI`). Same defect as `core.py`, missed
  because the logic exists twice.
- **`TAGUCHI_DEBUG=1` and `=yes` silently did nothing** — the parser tested
  `.lower() == "true"` only.
- Fixtures scoped inside one test class, so a second class's four tests errored
  on a missing fixture instead of running.

It is now at **zero**: all 306 checks pass, and `make test-bindings` runs the
WHOLE directory rather than the one hermetic file. Getting there turned up five
more real defects — every batch did, which is the point:

- **`Experiment.from_tgu(path)` deleted the caller's file.** `cleanup()`
  unlinked whatever `_tgu_path` named, and `from_tgu` pointed that at the file
  it was handed, so `with Experiment.from_tgu("my_experiment.tgu"): ...`
  destroyed the input on the way out of the block. Silent, immediate,
  unrecoverable, and present in BOTH layers. Ownership is tracked now rather
  than inferred from "the path is set".
- **The binding could not see L18.** `_get_arrays_info` scraped with a regex
  requiring a NUMBER before "levels"; a mixed-level array prints "mixed", so
  L18 never matched. `list_arrays()` returned 19 of 20 and
  `get_array_info("L18")` raised "not found" — the one array designed for
  mixed-level factors was unreachable. Both layers read `list-arrays --json`
  now, and `levels` is null for mixed rather than 0.

- **`CommandExecutionError` never escaped `_run_command`.** `raise error` sat
  inside the `try`, so the following `except Exception` caught and rewrapped it
  as a plain `TaguchiError` — `exit_code` and `stderr` discarded, and
  `except CommandExecutionError` never matching.
- **`Taguchi(cli_path=...)` could be silently overridden** by whatever
  `TAGUCHI_CLI_PATH` was set to in the shell: the enhanced layer searched the
  environment variable BEFORE the explicit argument. An argument is a decision;
  an environment variable is a default.
- **`summary()` could never report incomplete data.** It builds a "Data
  completeness: X%" line and a missing-runs warning, but called `main_effects()`
  first — which raises on missing runs. Both lines were unreachable in the only
  situation they exist for. It reports now; `main_effects()` stays strict,
  because an unbalanced design gives biased level means.

The `from_tgu` data loss needed fixing **twice**: `cleanup()` was not the only
place that unlinked. `__del__` did its own, checking only that `_tgu_path` was
set, so a `from_tgu` that RAISED left a half-built object whose destructor
deleted the file the user was asking about. The first fix passed its test
because that test only covered the with-block. Both paths are covered now.

**Deduplicating has started, at the seams where the bugs actually were.**
Two shared modules, 254 lines of duplicated logic removed:

- `_discovery.py` — one search order for the CLI binary. The two copies
  disagreed three ways, and each disagreement was a separate bug found
  separately: a stale path preferred over the umbrella build, a different
  environment variable per layer, and an explicit `cli_path` argument
  outranked by the environment.
- `_tgu.py` — the `.tgu` parser (byte-identical in both) and the file
  lifecycle. The lifecycle is the one that matters: both layers deleted the
  file named by `_tgu_path` without asking whether they had created it, which
  destroyed callers' files, and the first fix missed `__del__` in both.

What is deliberately NOT shared is error reporting: the layers raise different
exception types with different diagnostics, and that is an interface
difference rather than an accident.

**The rest of the duplicated "enhanced" layer is the standing liability.**
`core.py`/`core_enhanced.py`, `analyzer.py`/`analyzer_enhanced.py`,
`experiment.py`/`experiment_enhanced.py`, and until now two unrelated
`TaguchiError` classes. The CLI-scraping bug lived in FOUR places because of
it, the discovery bug in two, and the error hierarchy was unusable. Every
defect here is found and fixed two or four times. Collapsing the layers is the
highest-value structural change left in the binding.

**`pos += snprintf(...)` reached a fourth and fifth instance.** STATUS.md has
warned about this since 2026-08-06 and it kept appearing: `taguchi.c`'s
`taguchi_effects_to_json` wrote against a guessed buffer with it, and
`analyzer.c`'s `recommend_optimal_levels` used it behind `pos < buf_size`
guards that stopped an overwrite but left `pos` meaningless. Both now use the
clamping form, and the effects serializer sizes its buffer from the content
rather than guessing. It was also interpolating the factor name RAW, so a quote
in a name produced a document no parser would accept — from the public function
named for producing JSON. A test pins the escaping; reverting it fails the
build.

**A function named for producing JSON returned something that was not JSON.**
`serialize_effects_to_json` returned a bracketed C-comment placeholder, and
`test_serializer.c` asserted that output — enshrining it. Nothing called it
(`taguchi_effects_to_json` is the real one), so it was a trap for whoever
reached for the obvious name. Deleted, along with the test.

**The .tgu parser accepted control characters in factor names** while the
.space parser has always rejected them. Those names reach the JSON emitters and
`setenv("TAGUCHI_<name>")`, neither of which can represent them. Rejected now;
UTF-8 names still work, since only bytes below 0x20 are refused.

**...and `make coverage` still had none until 2026-08-10.** The fix below
applied to `CFLAGS`, and the coverage target REPLACES `CFLAGS` wholesale, so
`build/cov` was built with no `.d` files at all. Adding a field to
`ExperimentDef` rebuilt some objects there and not others; the link combined
two struct layouts and `taguchi_generate_runs` returned a 16-run design where
the `.tgu` asked for 81. Only `make coverage` failed, which reads as a coverage
problem rather than the stale-object bug it is. When a target overrides
`CFLAGS`, it inherits none of the discipline in them.

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

**The tool that reads a file must check the file is the one it thinks.**
`taguchi analyze`, `effects` and `confirm` regenerated the array to learn which
level each run used, bucketed the responses by level, and never once asked
whether the results file had the runs the array called for. Both directions
were silent. Too many rows: the extras were skipped, so an L9 against a 20-row
file returned a complete-looking ranking from rows 1-9. Too few: a level that
no response landed in was reported as a mean of **0.000**, printed in the same
column as the real means — a fabricated number in a table of measurements, at
exit 0.

Worst was a **crossed** design. With a `noise:` section the run ids number
inner × outer *pairs*, so the first nine rows of a 144-row L9 file are nine
noise points of control setting 1 — and the output was a main-effects ranking
of pure noise, wearing the control factors' names. Reported 2026-08-18 from the
inkwell iron-tannate project, where `robust` and `analyze` returned different
optimal settings from the same two files and neither said anything.

The lessons, in order of how much they cost:

- **The validation already existed.** `taguchi robust` requires every inner ×
  outer pair and refuses rather than averaging over a hole. `analyze` simply
  did not call it. Look for the check before writing it — and when one command
  in a suite validates something, ask why its siblings do not.
- **The repo's own worked example was demonstrating the bug.**
  `examples/cookies/cookies.tgu` carried a `noise:` section *and* the
  walkthrough ran `analyze` on it, so the committed
  `4-optimize-analysis.txt` was a wrong table shipped as a teaching artifact —
  and its README prose said "nine batches" while the file generated eighteen.
  The example suite ran the command and checked it *exited 0*, which it did.
  An example that runs is not an example that is right.
- **A zero is the most dangerous default there is.** `0.000` is a plausible
  measurement. Nothing about it looks like absence, which is precisely why a
  missing run must be refused rather than defaulted. The fabrication was fixed
  at *both* layers: the CLI refuses a results file that does not cover the
  design, and `calculate_main_effects` refuses an empty level whoever calls it,
  because a C consumer of `libtaguchi` has no CLI in front of it.
- **Ambiguity is a reason to refuse, not to guess.** A nine-row file for a
  crossed design could be nine control runs or the first nine of 144; nothing
  in its shape says which. `analyze` now refuses every crossed design outright
  and names `robust` instead.

**One parameter cannot mean two things.** `doe_csv_read_metric`'s `max_rows` is
a *design's run count* for every caller that has a design — morris, sobol and
rsm all pass the size of the array they generated — so a run id past it means
the file does not belong to that design, and refusing is correct. `uq` has no
design, and used the same parameter as a *capacity*: allocate 1024, read, and
grow if the buffer came back exactly full. Those two readings cannot both hold.
The reader saw run id 1025 against max_rows 1024 and reported a data error, so
the growth loop could never run and `uq` — whose entire job is summarising large
response sets — was capped at 1024 rows, failing outright on anything bigger.
Reported and fixed 2026-08-18 by sizing up front with `doe_csv_max_run_id`.

It was reported as a regression from the `taguchi analyze` row-count fix. It is
not: the `run_id > max_rows` check dates to `0f69945` (the funnel
reorganization), and a `uq` built entirely from pre-fix sources fails on a
2000-row file the same way. The cap had been shipping. Nothing had exercised
`uq` past 1024 rows, which is its own lesson — the tool with no design to check
against is also the tool no fixture happens to stress.

Two things worth carrying forward:

- **The bug was in the caller that was different, not the check.** The check
  earned its place; six callers depend on it. Adding a way to *ask the file its
  size* was the fix, not weakening what the other six rely on.
- **It reported the greatest run id, not the row count.** The buffer is indexed
  by run id, so a file holding runs 1, 5 and 900 needs 900 slots for three
  values. Sizing by row count would have reintroduced the same overflow from
  the other direction. (Fixing this also surfaced that `uq` was reading those
  untouched slots *uninitialised* to decide `isfinite()`; they are NaN-filled
  now.)

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
