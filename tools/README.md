# tools — cross-cutting analysis binaries

Small, independently useful tools the funnel leans on.

| Tool | Purpose | Roadmap |
|---|---|---|
| `ofat`   | One-factor-at-a-time confirmation around a base point. *"Any OA effect you act on costs exactly two more runs to verify"* — targets the aliasing / "16 dB artifact" failure mode. | M6 |
| `grid`   | Small full-factorial (2–3 factors, 3×3) to **resolve** interactions Sobol's S_Tᵢ−Sᵢ flags — exact, no aliasing. | M6 |
| `report` | Standalone unified HTML/SVG: Morris μ\*–σ scatter, Sobol Sᵢ/S_Tᵢ tornado bars, Taguchi main-effects + S/N. Also callable as `robust report`. | M4 |

All read the shared `.space` format and build on `common/libdoe`.
See [../DESIGN.md](../DESIGN.md) §7.
