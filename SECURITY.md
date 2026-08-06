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
unaccounted allocation. Enforced by:

- `core/tests/test_security.c` + per-tool adversarial tests, wired into
  `make test` (valgrind) and `make test-asan` (ASan/UBSan);
- `make fuzz` — deterministic, seedable fuzz under ASan/UBSan of every
  hand-rolled parser that reads untrusted input: `doe_space_parse` and
  `doe_csv_read_metric` (`core/tests/fuzz_parsers.c`), plus
  `pareto_read_csv` and `pareto_front_load`
  (`analyze/pareto/tests/fuzz_pareto.c`). **Every new parser gets a target here.**
- CI (`.github/workflows/ci.yml`) runs build → test-all → test-asan → fuzz →
  validate on every push/PR.

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
