# Hardening Plan — Adversarial Inputs & Users

*How `robust` should behave when fed hostile `.space` / `.tgu` / results files, or
run by someone trying to make it misbehave. Companion to DESIGN.md. 2026-06-29.*

The rule, inherited from taguchi: **adversarial input produces a clean error, never
a crash, overflow, or injection.** taguchi's `.tgu` parser already follows this
(`taguchi/tests/test_security.c`). This plan extends the same discipline to the new
`common` core and the `morris`/`sobol`/`robust` tools, closes the gaps they
introduce, and bakes the checks into each test suite.

---

## 1. Threat model — trust boundaries

| Boundary | Untrusted thing | Who supplies it |
|---|---|---|
| `.space` parser (`doe_space_parse`) | factor names, level values, ranges, `seed`/`samples`/`trajectories`/`grid_levels` | a file that may come from a third party |
| results CSV (`doe_csv_read_metric`) | `run_id`s, metric values, headers, line lengths | the model's output / a shared results file |
| model script (`doe_run` / `doe_run_capture`) | the `<script>` and the values that reach it via `ROBUST_*` env vars | script is the **user's**; env *values* can originate from an adversarial `.space` |
| report writers (`report.c`) | factor names/values rendered into HTML/JSON/`.tgu` | flow through from the `.space` |
| output paths (`--html`/`--json`/`--tgu`) | filesystem paths | the user's own invocation |

Two adversary models: **(A)** a malicious *file* handed to a trusting user
(`robust funnel evil.space ./mymodel.sh`), and **(B)** resource exhaustion / memory
corruption from absurd-but-parseable parameters.

---

## 2. Already hardened — do not redo

- **`.tgu` parser** (taguchi): name/value length rejection, factor/level count caps,
  NULL/empty/no-section handling, `=`-in-name documented, shell metacharacters stored
  verbatim (not interpreted), error-buffer overflow test. `robust_write_tgu` output is
  re-consumed by this parser, so the bench leg stays covered.
- **`common` defenses in place:** env-var **name** guarded against `=` and length
  overflow (`runner.c`); `run_id` bounds-checked (`>= 1`, `<= max_rows`) in `csv.c`;
  missing responses rejected via `isnan` in `morris_analyze`/`sobol_analyze`; `.space`
  rejects oversized names/values, too many factors, too many levels, `lo >= hi`, and
  `log` with `lo <= 0`; PRNG is seeded/deterministic; the runner uses
  `execl("/bin/sh","sh","-c",script)` with the script as a single arg — **no shell
  command is ever built from untrusted data**.

---

## 3. Hardening backlog (prioritized)

| ID | Sev | Component | Risk | Fix | Test to add |
|---|---|---|---|---|---|
| **H1** | High | `space.c`, `morris.c`, `sobol.c` | `samples`/`trajectories` are unbounded → `npoints = N·(k+2)` and `malloc(npoints·k·8)` **overflow `size_t`** → undersized buffer / heap overflow, or OOM. | Cap `samples` (≤ 2²⁰), `trajectories` (≤ 10⁴), `grid_levels` (≤ 64); add `doe_size_mul_ok()` overflow-checked multiply and use it before **every** design/analysis `malloc`. | `.space` with `samples: 999999999999` → clean error; `morris_design_build`/`sobol_design_build` with hostile params → `-1`, no alloc. |
| **H2** | High | `report.c` | Factor names/values written **unescaped** into `report.html` → stored **XSS** (e.g. a factor named `<script>…`) from an adversarial `.space`. | Add `doe_html_escape()` (`< > & " '`) and use it for every interpolated name/value in HTML. (JSON already uses `doe_json_escape`.) | funnel result with factor `<script>` → HTML contains `&lt;script&gt;`, never the raw tag. |
| **H3** | Med | `space.c` | `doe_space_parse(NULL,…)` calls `strlen(NULL)` → crash; `doe_space_parse_file(NULL,…)` likewise. | NULL-guard `content`/`path`/`space` → clean error. | `doe_space_parse(NULL,&sp,err)` → `-1`, no crash. |
| **H4** | Med | `csv.c` | Lines longer than the 8192 buffer are silently split mid-row → misparse (taguchi's CSV reader detects this; ours does not). NULL `path` unguarded. | Detect buffer-full-without-newline → error (port taguchi's check); NULL-guard. | a 100 KB single line → clean "line too long" error. |
| **H5** | Med | `csv.c`, analyzers | `strtod` accepts `inf`/`nan` → non-finite responses propagate into μ\*/Sᵢ as `NaN`/`Inf`. | Reject non-finite metric values on read (`isfinite`); `isfinite`-guard responses in `morris_analyze`/`sobol_analyze`. | CSV value `inf` / `nan` → error. |
| **H6** | Med | `space.c`, runner contract | Adversarial **categorical level values** become `ROBUST_<factor>` env values; a naive model script that interpolates them into a shell/awk command is injectable (adversary model A). | Reject NUL/control chars in level values at parse time; **document the data-not-code contract** loudly (scripts must read env as data); optional `--strict` to reject shell metacharacters in values. | level value containing a newline / control char → parse error; metachar value stored verbatim + documented. |
| **H7** | Med | `space.c` | Non-finite or absurd bounds (`lo: -inf`, `hi: inf`) parse and yield `inf` after scaling. | Reject non-finite `lo`/`hi`. | `x: -inf, inf` → error. |
| **H8** | Low | `report.c` / `.tgu` | Survivor `.tgu` carries odd level strings downstream. Names can't hold `:`/newline and levels can't hold `,`/newline (parser-constrained), but round-tripping should be proven. | Assert on write; rely on taguchi's parser downstream. | `robust to-tgu` output passes `taguchi validate`. |
| **H9** | Low | CLI / paths | `--html`/`--json`/`--tgu` overwrite caller-chosen paths. | Not a third-party vector (user's own paths); document "no privileged/symlink assumptions"; no fix beyond docs. | n/a |

---

## 4. Where the adversarial tests live

- **`common/tests/test_security.c`** (new) — the core boundary suite, mirroring
  taguchi's: oversized name/value/factor-count/level-count (assert **reject**, not
  silent truncate), NULL/empty/no-`factors`, malformed numbers, `lo >= hi`, `log ≤ 0`,
  non-finite bounds (H7), huge `samples`/`trajectories` (H1), CSV line truncation (H4),
  `run_id` bounds + overflow, non-finite responses (H5), control-char level values (H6).
- **`morris`/`sobol` tests** — `*_design_build` with hostile params → clean error;
  `*_analyze` with `NaN`/`Inf` responses → error.
- **`robust` tests** — funnel with an adversarial factor name → report HTML is escaped
  (H2); `to-tgu` round-trips through `taguchi validate` (H8).
- Wire all of these into `make test` (and `make test-all`).

---

## 5. Phases

1. **Memory safety + injection (H1–H4) — done (2026-06-30).** Param caps +
   the overflow-checked `doe_size_mul_ok` in `space`/`morris`/`sobol`,
   `doe_html_escape` wired into `report.c`, NULL guards in the parser and CSV
   reader, and CSV line-truncation detection. Shipped `common/tests/test_security.c`
   (7 cases) plus a report-escaping test in `robust` — 25 suite tests pass, valgrind clean.
2. **Robustness (H5–H7).** Non-finite rejection (responses and bounds), the env-value
   data-not-code contract + control-char rejection, plus the per-tool adversarial tests.
3. **Assurance (H8–H9 + tooling).** `.tgu` round-trip, doc notes, and a **fuzz target**:
   feed random bytes to `doe_space_parse` and `doe_csv_read_metric` under
   ASan/UBSan (`make fuzz`) — cheapest way to find the cases this table missed. Fold a
   sanitizer build and `make test` into CI.

**Invariant for every fix:** the offending input returns `-1` with a bounded,
NUL-terminated `err` message and performs **no** allocation it can't account for —
verified under `-Werror`, valgrind, and (Phase 3) ASan/UBSan.
