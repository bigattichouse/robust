# pareto — multi-objective frontier filter and store

Extracts and maintains the **non-dominated set** over a results CSV carrying
several metric columns. Two modes over one dominance core:

- **filter** — stateless. Results CSV in, non-dominated rows out, same dialect.
- **store** — a `.front` file that accumulates a frontier across experiment
  batches, so a study's trade-off set is a durable artifact rather than
  something recomputed and lost each run.

Spec: [`../../spec/pareto.bp`](../../spec/pareto.bp). Roadmap: EXPANSION.md E1.

## Dominance

`a` dominates `b` iff `a` is no worse than `b` on **every** objective and
strictly better on **at least one**. Irreflexive, asymmetric, transitive
(asserted over 200 random 3-objective points in the suite).

Points equal on every objective dominate each other in *neither* direction, so
**both survive**. That is deliberate: two settings with identical measured
performance are distinct operating points the objectives do not separate, and
the user may prefer one for reasons the objectives do not encode. Use
`list --duplicates` to see them.

Source: Deb, K. (2001), *Multi-Objective Optimization using Evolutionary
Algorithms*, Wiley, Sec. 2.4.

## Usage

```sh
# stateless filter — composes in pipes, reads '-' for stdin
pareto filter results.csv --max yield --min cost > front.csv

# persistent store
pareto init --max yield --min cost --columns-from batch1.csv > study.front
pareto merge study.front batch1.csv
pareto merge study.front batch2.csv
pareto list  study.front                      # rows, no preamble
pareto why   study.front batch1.csv --run 2   # explain a verdict
```

At least two objectives are required. With one, the answer is just the best
row — the tool says so and points at `sort` rather than silently obliging.

## The `.front` format

Deliberately **a valid results CSV**. Metadata lives in `#` comment lines,
which `core/src/csv.c` already skips, so every downstream tool reads a
frontier unchanged and `pareto list` is only a convenience:

```
# tgu-front 1
# objectives: yield max, cost min
# merge: b1.csv 2026-08-06T16:43:43Z in=4 admitted=3 evicted=0 rejected=1 dup=0
# merge: b2.csv 2026-08-06T16:43:43Z in=2 admitted=1 evicted=1 rejected=1 dup=0
run_id,yield,cost,temp
5,0.90,20,360
1,0.50,10,300
3,0.30,5,280
```

```sh
sobol analyze model.space study.front --metric yield   # works, unchanged
```

Rows are sorted by the first objective in its improving direction, then by
`run_id`, so a no-op merge is byte-identical and `git diff` on a `.front`
shows exactly what moved.

The merge preamble is the audit trail. A frontier is a claim about which
configurations are worth running; the claim is only trustworthy if you can see
what it was built from.

## Merge semantics

- **Order-independent.** Merging A then B gives the same point set as B then A,
  because each merge recomputes the non-dominated set over the whole union
  rather than filtering arrivals against incumbents.
- **Equal to the one-shot filter.** Merging batches separately gives the same
  set as filtering their concatenation. The store cannot drift from the
  stateless answer, and the suite asserts it.
- **Idempotent.** Re-merging a batch admits 0 and evicts 0. An arrival already
  on the front — *same `run_id` and same value on every objective* — is counted
  as `duplicate`, not admitted. This is distinct from the tie rule above: a tie
  is a **different** run whose objectives happen to be equal, and both of those
  are kept.
- **Atomic.** Writes go to a temp file in the same directory, then `rename(2)`.
  A crash mid-merge never truncates a frontier that may represent weeks of runs;
  a failed write leaves the original byte-identical.

## Caps

| | |
|---|---|
| `PARETO_MAX_ROWS` | 1048576 across front + incoming |
| `PARETO_MAX_OBJECTIVES` | 16 |
| line length | 8192, matching `csv.c` |

Dominance is O(n²). Above `PARETO_WARN_ROWS` (65536) the tool prints the
expected comparison count to stderr before starting, so a user who pipes in a
million rows learns why it is slow instead of wondering whether it hung.

## Tests

`make run-tests` (18 tests). The dominance core is validated against an
analytic front **before** any store behaviour is exercised:

- `y1 = x`, `y2 = 1 − x²` on [0,1], both maximized — every sampled point is
  non-dominated, so all 101 must survive.
- The same curve **plus 500 planted interior points** at `(0.9·y1, 0.9·y2)`,
  every one strictly worse on both axes. All 500 must be dropped. The all-pass
  case above cannot detect an over-permissive filter; this one can.
- Exhaustive: over 200 random 2–5 objective problems, every excluded point must
  have a dominator among the survivors and no survivor may dominate another.

## Worked example

[**7. Taste isn't the only thing**](../../examples/cookies/#7-taste-isnt-the-only-thing-desire-and-pareto) — part of [one experiment carried through every tool](../../examples/cookies/),
where this one shows the recipes where you cannot improve one thing without giving up another.
