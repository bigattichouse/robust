# Security posture

*The hardening plan (HARDENING.md, 2026-06-29) completed all three phases on
2026-07-16; this is the surviving record. The rule, inherited from taguchi:
**adversarial input produces a clean error, never a crash, overflow, or
injection.***

## Threat model — trust boundaries

| Boundary | Untrusted thing |
|---|---|
| `.space` parser (`doe_space_parse`) | factor names, level values, ranges, `seed`/`samples`/`trajectories`/`grid_levels` — the file may come from a third party |
| results CSV (`doe_csv_read_metric`) | `run_id`s, metric values, headers, line lengths |
| model script (`doe_run` / `doe_run_capture`) | the env *values* reaching the script can originate from an adversarial `.space`; the script itself is the user's |
| report writers (`report.c`) | factor names/values rendered into HTML/JSON/`.tgu` |
| output paths (`--html`/`--json`/`--tgu`) | the user's own invocation — trusted as given, no symlink/privilege checks |

## Defenses (closed backlog, H1–H9)

- **H1** — `samples`/`trajectories`/`grid_levels` capped (`DOE_MAX_*`);
  `doe_size_mul_ok` overflow-checks every count-based allocation.
- **H2** — `doe_html_escape` on every name/value interpolated into report HTML
  (JSON uses `doe_json_escape`).
- **H3** — NULL guards on parser and CSV entry points.
- **H4** — CSV lines longer than the buffer are rejected, not silently mis-split.
- **H5** — non-finite (`inf`/`nan`) metric values and responses rejected in the
  CSV reader and the morris/sobol analyzers.
- **H6** — control characters rejected in factor names and level values
  (UTF-8 bytes ≥ 0x80 pass — non-English names stay legal); env values reach the
  model script as *data* via `setenv`, never spliced into a command; env var
  names guarded against `=` and length overflow.
- **H7** — non-finite `.space` bounds rejected (`isfinite` deliberately checked
  *before* `lo >= hi` — NaN compares false and would slip past ordering alone).
- **H8** — the survivors `.tgu` round-trips through `taguchi validate`
  (linear, log, and categorical writer branches).
- **H9** — output paths documented as caller-trusted (`robust/README.md`).

The `.tgu` parser's equivalent discipline lives in taguchi itself
(`taguchi/tests/test_security.c`).

## Invariant and verification

Every offending input returns `-1` with a bounded, NUL-terminated `err` and no
unaccounted allocation. **Parse errors also carry `line N:`**, added by
rewriting the finished message in place — a string-building site of exactly the
kind that produced this repo's three `off += snprintf(...)` overflows, so it is
bounded by the already-clamped formatted length and pinned by
`test_space_error_prefix_never_overflows`, which brackets `err` in sentinel
regions and therefore fails in *every* build mode rather than only under ASan.

Enforced by:

- `core/tests/test_security.c` + per-tool adversarial tests, wired into
  `make test` (valgrind) and `make test-asan` (ASan/UBSan);
- `make fuzz` — deterministic, seedable fuzz under ASan/UBSan of every
  hand-rolled parser that reads untrusted input: `doe_space_parse` and
  `doe_csv_read_metric` (`core/tests/fuzz_parsers.c`), plus
  `pareto_read_csv` and `pareto_front_load`
  (`analyze/pareto/tests/fuzz_pareto.c`). **Every new parser gets a target here.**
- `make coverage` — line/branch coverage over the suites (gcovr, or raw gcov
  as a fallback). Baseline at 2026-08-06: **84.2% lines, 95.5% functions** over a
  denominator that now includes every CLI binary (it previously counted only
  libraries, which flattered it). **88.3% / 98.7% at 2026-08-09.** Use it
  before claiming something is tested.
- CI (`.github/workflows/ci.yml`) runs build → test-all → test-asan → fuzz →
  validate on every push/PR, **and a second job that builds and tests with
  clang**. With `-Werror`, one compiler's silence is not evidence: GCC 16
  rejects an increment-only variable that GCC 13 accepts (PR #1), so CI was
  green while a future toolchain was already broken. clang diagnoses that
  class today.

### Rejection paths are now tested, not just written (2026-08-09)

`make coverage` showed the three parsers at 61% / 78% / 81%, and the missing
lines were overwhelmingly the *error* branches — the ones this document's
invariant is about. A rejection path with no test behind it is exactly where a
"clean error" quietly becomes a crash or an unterminated buffer, because
nothing ever looks at it.

Added, each asserting the full invariant (non-zero return, NUL-terminated error
inside `DOE_ERR_SIZE`, no crash): every malformed-factor branch, every
malformed-`groups:` branch, `doe_space_parse_file`'s path handling, and the
results CSV's rejections (absent metric column, no header, truncated row, bad
or out-of-range `run_id`, non-numeric and non-finite values, no data rows).
`sobol.c` 61% → 90%, `space.c` 78% → 91%, `csv.c` 81% → 98%.

**H1 extended while doing it.** `fopen` on a DIRECTORY succeeds on Linux and
`ftell` then reports `LONG_MAX`, so `doe_space_parse_file` was attempting a
9-exabyte `malloc` and reporting "out of memory" — true of the allocation,
useless about the cause. The size is now bounded *before* the allocation
(`DOE_MAX_SPACE_BYTES`, 4 MiB against a ~100 KB worst-case legitimate file) and
the error names the real problem.

### The .tgu parser no longer drops lines in silence (2026-08-09)

Inside `factors:`, only an indented line containing `:` was read as a factor;
every other line was skipped without comment. One mistyped factor line
therefore removed a factor from the design **while the tool still succeeded** —
the orthogonal array was built and executed over the wrong space, and nothing
in the output indicated it. A mistyped top-level key (`arry:` for `array:`) was
dropped the same way, leaving auto-selection to choose a different array than
the one requested.

Both are now errors naming the line and what was expected. This is the same
rule as the 2026-08-06 entry below: when the answer is undefined, say so — and
a design missing a factor the user asked for is undefined, not a design.

### Degenerate input now fails loudly (2026-08-06)

Two paths returned confident nonsense instead of an error:

- **`second_order:` was a silent no-op.** The parser accepted it and `robust`'s
  funnel propagated it, but no estimator implemented it, so anyone asking for
  interaction indices got first-order results with no indication. `sobol` now
  refuses, at the single choke point every entry path shares.
- **A constant response produced `-nan` and exit 0.** Sobol indices are shares
  of variance, so they are undefined when the output has none — the estimator
  was dividing by zero. It now errors with the likely causes named (a model
  that ignores its environment, a fixed echo, ranges too narrow to move it).
  The bootstrap estimator also guards internally: a degenerate resample would
  otherwise put NaN into the array that gets `qsort`ed for the CI bounds, and
  NaN makes that ordering arbitrary — a wrong interval rather than an obviously
  broken one.
- **`morris` was correct but quiet.** An all-zero μ\* table is legal, and
  usually means a broken harness rather than genuinely inert factors. It now
  says so on stderr, leaving stdout byte-identical so pipelines are unaffected.

The general rule these share: **when the answer is undefined, say so.** A tool
that prints `-nan` and exits 0 is worse than one that fails, because the number
gets copied into a decision.

### Security sweep, 2026-08-06

After the E1/M5/M6 build-out, a sweep over the shipped code (tests excluded):

- **No `strcpy`, `strcat`, `sprintf` or `gets` remain.** The last one was
  `doe_json_escape`, whose `sprintf` was provably in bounds — `len*6+1` is
  exactly the worst case of every character escaping to `\u00XX` — but proof
  by arithmetic stops holding the moment someone edits the format string. It
  is now a bounded `snprintf`, and a test drives a string made entirely of
  control characters, which sits exactly on that boundary.
- **No unclamped `off += snprintf(...)` accumulation.** That defect appeared
  three separate times this session (pareto's error builder, the serializer's
  growth path, a test buffer), so it is worth naming: `snprintf` returns the
  length it *would* have written, and adding that to an offset walks past the
  end of the buffer, after which the remaining-size argument goes negative and
  converts to an enormous `size_t`.
- **Every parser that reads untrusted input has a fuzz target** — `.space`,
  results CSV, and pareto's `.front`. The four tools added this session
  (`regress`, `uq`, `ofat`, `grid`) parse only their own CLI arguments and the
  shared CSV reader, which is already covered.
- **Every binary is exercised by a suite.** Five shell suites now drive the
  CLIs (`morris`, `pareto`, `taguchi` x2, and the analyze/resolve four), which
  is what turned up that the `morris` CLI had been at 20% with `groups:` and
  `bifurcate` never run end to end.

### The run loop is now tested (2026-08-06)

`make coverage`'s first run reported `core/src/runner.c` at **0.00% of 65
lines**. That is the fork/exec loop every tool uses to execute a user's model —
`fork`, `setenv` per factor, `execl /bin/sh -c`, `waitpid` — so the most
security-sensitive file in the repo was also the only one with no coverage at
all. Nothing anywhere called `doe_run` or `doe_run_capture`.

`core/tests/test_runner.c` now covers it in 14 tests, including the property
this document asserts and nothing verified: **factor values reach the script as
environment *values* and are never spliced into the command string.** Values of
`; touch FILE`, `$(touch FILE)` and `` `touch FILE` `` are passed through both
`doe_run` and `doe_run_capture`; the test asserts no marker file appears and
that each value arrives byte-for-byte intact.

Also covered: `RUN_ID` is 1-based and distinct per row, a NULL from the value
callback becomes an empty string, a factor name containing `=` is refused,
non-zero child exit and signal death are both rejected by `doe_run_capture`
even when a valid number was already printed, non-numeric and empty output are
rejected, and 100 KB of child output neither overflows the 256-byte read buffer
nor deadlocks on a full pipe.

**runner.c's coverage was a measurement artifact, and it is now fixed.** It
read ~52% because `child_set_env`, `execl` and the child's `dup2` run in the
forked child, which then execs or `_exit`s — gcov's counters are never flushed
either way, so those lines could not be attributed however well they ran.

`__gcov_dump()` writes the counters explicitly. Calling it in the child
immediately before `exec` and before each `_exit` attributes that work
correctly; gcov merges the child's `.gcda` into the parent's. It is compiled in
only under `make coverage` (`-DDOE_COVERAGE`), so ordinary and sanitizer builds
are byte-for-byte unaffected. **runner.c: 52% → 77%.**

What remains uncovered there is genuinely near-irreducible: `fork()`, `pipe()`
and `exec` failure paths, which need resource exhaustion to reach, and the
final `_exit` statements themselves, which by definition execute after the
counters have been written.

### Two assurance defects found and fixed, 2026-08-06

Both are worth recording because each made the pipeline *look* stronger than
it was.

1. **`make test` swallowed valgrind failures.** The stage was a sequence of
   `valgrind … && echo clean;` lines; because each ended in `;`, only the last
   suite's exit status reached make, and every line redirected output to
   `/dev/null`. A leak in any of the first five suites produced no failure and
   no diagnostic. CI ran this and reported success. Now a loop over
   `$(TEST_BINS)` that propagates failure, prints the report, and picks up new
   suites automatically.

2. **`pareto`'s error-message builder overflowed its buffer.** Building the
   "unknown objective column" list with
   `off += snprintf(err + off, DOE_ERR_SIZE - off, …)` is wrong: `snprintf`
   returns the length it *would* have written, so one long CSV header field
   pushes `off` past the buffer, after which `err + off` is out of bounds and
   the size argument is negative cast to `size_t`. Found by `fuzz_pareto.c` on
   its first run. Fixed with a clamping `err_cat()` helper — **use it for any
   multi-part error message.**

   **The regression test needed fixing too.** Its first version only failed
   under a sanitizer — verified against the reverted code, a plain build of the
   buggy version passed the whole suite, because the overflow lands in stack
   the assertions never read. A test that only works in one build mode is a
   false guarantee.

   The durable version (`test_error_paths_never_overflow_err`) places the error
   buffer between two 16 KB sentinel regions and checks them by hand, so an
   out-of-bounds write is detected by ordinary C in **every** build mode.
   Confirmed both directions against the reverted code:

   | build | old (buggy) | new (fixed) |
   |---|---|---|
   | plain `-O1`, no sanitizer | **18/19, GUARD VIOLATED**, exit 1 | 19/19, exit 0 |
   | ASan/UBSan | stack-buffer-overflow report | clean |

   The guard is sized past `PARETO_MAX_LINE` deliberately: the runaway offset
   is bounded by the input line length, so a small guard could be jumped over
   entirely. It sweeps six error paths, not just the one that broke, so the
   next message built from input-derived text is covered on arrival.

   **Pattern to reuse:** when a defect is only visible to a sanitizer, add the
   sentinel region rather than relying on `make test-asan` to catch a
   regression. Sanitizer builds are a backstop, not the primary guard.

New findings start a fresh backlog here.
