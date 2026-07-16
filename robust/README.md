# robust — the funnel orchestrator

The "ideal as one tool": reads one `.space`, runs the funnel end to end, tracks
which factors survive each stage, and produces a combined report. The "stages of
maturity" (idea.txt) made executable.

- **Commands:**
  - `robust funnel <file.space> <script>` — Morris → drop low-μ\* factors → Sobol on survivors; writes a self-contained HTML/JSON report and a `.tgu` for the bench (`--html` / `--json` / `--tgu`).
  - `robust screen <file.space> <script>` — Morris stage only (keep/drop list).
  - `--keep-fraction F` — keep factors with μ\* ≥ F·max(μ\*) (default 0.1).

`robust` **links** the `morris` and `sobol` libraries and drives the funnel
in-process; it shells out only to your model `<script>`, which reads
`ROBUST_<factor>` env vars and prints one numeric response to stdout.

**Security:** `ROBUST_<factor>` values are passed to the script as environment
*data* (via `setenv`, never spliced into a command). Your script must read them as
data — don't interpolate them into a shell / `eval` / `awk` program — so a hostile
`.space` can't inject through it. See [../HARDENING.md](../HARDENING.md).

**Responses must be finite.** A script that prints `inf`/`nan` (e.g. a "never
converges" sentinel) hard-fails the run with a clean error rather than
propagating NaN into the indices — clamp to a large finite penalty instead.

Status: **built (M4)** — funnel + screen + report + `.tgu` hand-off, validated by
orchestrated-process tests. See [../DESIGN.md](../DESIGN.md) §6.
