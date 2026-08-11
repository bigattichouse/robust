# rsm — response surface methodology

Screening says *which* factors matter. Attribution says *how much*. This says
**what setting is best**, by fitting a quadratic over the two or three
survivors and solving for its stationary point.

```sh
rsm sample  model.space > design.csv     # run these
rsm analyze model.space results.csv
```

```
Stationary point is a maximum
Predicted response: 100

factor                      coded          value
x                          0.4243              3
y                         -0.5657             -4
```

A quadratic is the smallest model that can have an **interior** optimum, which
is the entire reason for the stage: a linear fit always points at a corner, so
it can rank factors but never locate a peak.

## The design

A **central composite**: the 2ᵏ factorial corners (which estimate the
interactions), 2k axial points at ±α, and centre replicates.

The axial points are not decoration. Corners alone cannot estimate a pure
quadratic term — every coordinate is ±1 there, so x² is 1 everywhere and the
curvature is invisible. α = (2ᵏ)^¼ makes the design *rotatable*: prediction
variance depends on distance from the centre and not on direction, so the fit
is equally trustworthy whichever way the optimum turns out to lie.

## What it tells you when there is no answer

- **saddle** — the surface curves up one way and down another. There is no
  interior optimum to report, and the tool says so rather than naming a point.
- **no stationary point** — the fit is a plane, or close enough that the
  quadratic terms cannot be told from zero. The optimum is on a boundary.
- **outside the design region** — a stationary point beyond the runs is
  **extrapolation**. Re-centre the ranges on it and run again before believing
  it.
- **wrong kind** — you asked to minimise and found a maximum. The surface turns
  the wrong way; move the ranges toward the direction that improves.

Each of those is a real outcome of a real experiment, and each is more useful
than a number that looks like an answer.

## Limits

Two or three factors. A response surface over everything is the full factorial
this toolkit exists to avoid — screen first, then bring the survivors here.
