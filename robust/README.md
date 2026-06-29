# robust — the funnel orchestrator

The "ideal as one tool": reads one `.space`, runs the funnel end to end, tracks
which factors survive each stage, and produces a combined report. The "stages of
maturity" (idea.txt) made executable.

- **Commands (planned):**
  - `robust funnel <file.space> <script>` — Morris → drop low-μ\* factors → Sobol on survivors → report; emits a `.tgu` for the bench.
  - `robust screen` / `robust attribute` — run a single stage.
  - `robust report` — unified HTML/SVG dashboard.
  - `robust to-tgu` — emit a taguchi `.tgu` for the survivors.

Shells out to `morris`, `sobol`, and `taguchi` (Unix style) so any stage is
inspectable and swappable.

Status: planned — see [../DESIGN.md](../DESIGN.md) §6 and roadmap **M4**.
