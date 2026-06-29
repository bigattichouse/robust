# sobol — variance attribution

Splits output variance into per-factor shares: **Sᵢ** (first-order) and **S_Tᵢ**
(total, including interactions), via Saltelli sampling at `N·(k+2)` runs. Answers
*"how much does each factor matter, including hidden interactions?"*

- **Input:** the shared `.space` factor format (`samples: N`, `seed:`, `second_order:`).
- **Commands (planned):** `sample | generate | run | analyze | validate`.
- **Output:** Sᵢ, S_Tᵢ, and (S_Tᵢ − Sᵢ) = interaction share, with bootstrap CIs;
  diagnostics for "freeze if S_Tᵢ ≈ 0" and "additive if Σ Sᵢ ≈ 1".

Sampler: LHS first, then a Sobol low-discrepancy sequence (Joe-Kuo). Validated
against the Ishigami function's closed-form indices.

Status: planned — see [../DESIGN.md](../DESIGN.md) §5 and roadmap **M3 / M5**.
