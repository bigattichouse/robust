# desire — Derringer–Suich desirability

Every other tool here analyses **one** metric. Real experiments trade objectives
off. `desire` maps each metric to [0,1], combines them, and **appends a
`desirability` column**:

```sh
desire --max yield --min cost results.csv \
  | sobol analyze model.space - --metric desirability
```

The output is the same results-CSV dialect as the input, which is the whole
design: the existing single-response pipeline — screen, attribute, RSM — runs
on it unchanged. This is a filter, not a stage.

## Geometric, not arithmetic

One objective at zero takes the whole row to zero. A candidate that fails a
requirement outright is **not** rescued by excelling elsewhere — which is
exactly what an arithmetic mean would let it do, and why the method specifies
the geometric one.

| | yield | cost | desirability |
|---|---|---|---|
| best yield in the file, worst cost | 95 | 50 | **0** |
| decent at both | 90 | 10 | 0.89 |

## Objectives

```
--max COL           higher is better
--min COL           lower is better
--target COL:VALUE  best at VALUE, falling away on both sides
```

Bounds come from each column's **observed range**, so desirability is relative
to the experiment you actually ran. A constant column scores 1 throughout: it
carries no information and should not penalise anything.

## Pairs with `pareto`

The scalar path and the front view answer different questions. `desire`
**recommends** — one number to optimise, so the whole funnel applies.
`pareto` **shows the trade-off space** — every non-dominated option, so you can
see what you would be giving up. Use both.
