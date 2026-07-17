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

- `common/tests/test_security.c` + per-tool adversarial tests, wired into
  `make test` (valgrind) and `make test-asan` (ASan/UBSan);
- `make fuzz` — deterministic, seedable fuzz of `doe_space_parse` and
  `doe_csv_read_metric` under ASan/UBSan;
- CI (`.github/workflows/ci.yml`) runs build → test-all → test-asan → fuzz on
  every push/PR.

New findings start a fresh backlog here.
