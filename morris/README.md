# morris — elementary-effects screening

Ranks many factors by importance (**μ\***) and flags nonlinear / interacting ones
(**σ**) at `r·(k+1)` model runs — the first screening stage of the `robust` funnel.
Answers *"which factors matter at all?"*

- **Input:** the shared `.space` factor format (`trajectories: r`, `grid_levels: p`, `seed:`).
- **Commands (planned):** `sample | generate | run | analyze | validate`.
- **Output:** per-factor μ\*, σ; ranked keep/drop list; σ ≳ μ\*/2 flagged.

Status: planned — see [../DESIGN.md](../DESIGN.md) §4 and roadmap **M2**.
Builds on `common/libdoe` (PRNG, `.space` scaling, run-loop).
