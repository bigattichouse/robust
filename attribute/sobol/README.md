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

**Responses must be finite.** A run that yields `inf`/`nan` (e.g. a "never
converges" sentinel) fails analysis with a clean error — variance decomposition
on infinity is meaningless. Have your model clamp to a large finite penalty.

Status: planned — see [../DESIGN.md](../DESIGN.md) §5 and roadmap **M3 / M5**.


## Second-order indices

Set `second_order: true` in the `.space` and `sobol analyze` also reports which
**pairs** interact:

```
Second-order interactions (3 pairs)

pair                                   S2     closed
a x b                              0.0567     1.0110
a x c                              0.0000     0.5913
```

- **`closed`** = `Var(E[Y | Xi, Xj]) / V` — everything the pair explains together.
- **`S2`** = `closed − S_i − S_j` — the interaction *alone*.

`S2` answers the question `S_T − S_i` can only raise: **which two**. The output
names the top pair and the exact `grid` command to resolve it.

**Cost is `N(k + 2 + k(k−1)/2)`** — one extra block per pair, which is why it is
opt-in. At k=10 that is 47 blocks against 12, so raise `samples:` deliberately
rather than by habit.

**Second-order needs more samples than first-order.** At N=512 on a model with a
real `a×b` interaction, the estimate came back at −0.009 (noise swamped it); at
N=8192 it read 0.057 and ranked correctly. The bootstrap CIs on `S1`/`ST` are
the guide — if those are wide, the pair estimates are worse.

Validated in `make validate` check F against the g-function's exact
decomposition (`V_ij = V_i·V_j` for a product function, so every pair has a
closed form): worst error 0.005 at N=16384.
