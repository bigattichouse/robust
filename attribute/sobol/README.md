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


## `converge` — stop guessing `samples:`

```sh
sobol converge model.space ./run.sh --target-ci 0.1 [--max-samples N]
```

Doubles `samples:` and re-analyses until every Sᵢ **and** S_Tᵢ interval is
narrower than the target, then names the N to write into the `.space`. Both
indices are checked: S_T can stay wide while Sᵢ has settled, and a total index
you cannot bound is exactly the one you must not act on.

Doubling suits this design. A quasi-random sequence is uniform over aligned
blocks of 2ᵐ points, so starting from a power of two and doubling keeps the
alignment that `make validate` check G measures the value of — the property the
non-power-of-two note warns about losing.

Hitting the cap **exits non-zero**. Variance shares that poorly determined
should not be ranked, let alone acted on.

Under `sampling: lhs` the draw depends on the RNG stream rather than N alone,
so the reported N reproduces the run only with the seed that is already in the
`.space`. The output says so.

## `--json` — the machine-readable contract

```sh
sobol analyze model.space results.csv --json
```

The table is a display; these keys are the interface.

```json
{
  "tool": "sobol", "command": "analyze", "schema": 1,
  "metric": "response", "sampler": "sobol", "samples": 1024, "runs": 5120,
  "factor_count": 3, "sum_first_order": 0.865, "additive": false,
  "indices": [
    {"factor": "b", "s1": 0.353, "s1_lo": 0.29, "s1_hi": 0.41,
     "st": 0.505, "st_lo": 0.44, "st_hi": 0.57, "interaction": 0.152}
  ],
  "second_order": [
    {"a": "b", "b": "c", "s2": 0.1314, "closed": 0.8654}
  ]
}
```

- **`schema`** is bumped when a key is renamed or removed, never for an
  addition. Refuse a schema you do not know rather than parsing it wrongly.
- **`sampler`** is `"sobol"` or `"lhs"`. It changes the design and therefore
  the indices, so a result that does not record it cannot be reproduced.
- **`second_order`** is `null` unless `second_order: true` is set, and when
  present it carries **every** pair — the table stops at ten, but truncating a
  machine format leaves the consumer unable to tell a short list from a
  complete one.

Both index columns and their intervals print as separate whitespace-delimited
fields in the table too, and each interval is a single space-free token
(`[0.29,0.41]`). They used to be glued together — `0.353[0.29,0.41]` — which
silently broke every consumer that split the row positionally. See the same
section in [../../screen/morris/README.md](../../screen/morris/README.md).
