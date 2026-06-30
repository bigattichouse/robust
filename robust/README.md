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

Status: **built (M4)** — funnel + screen + report + `.tgu` hand-off, validated by
orchestrated-process tests. See [../DESIGN.md](../DESIGN.md) §6.
