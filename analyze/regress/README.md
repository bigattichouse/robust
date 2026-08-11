# regress — direction, which variance shares cannot give

Sobol tells you a factor owns 30% of the variance. It does not tell you whether
raising that factor raises or lowers the response. `regress` does, with a sign.

```sh
morris sample model.space > design.csv        # factor columns
# ... join your measured responses onto it as a `response` column ...
regress model.space data.csv --metric response
```

```
SRC (standardized regression) (metric: response) — 120 runs, 3 factors

Factor                coefficient  direction
x0                         0.8659  raises the response
x1                        -0.4479  lowers the response
x2                      2.704e-14  no effect

R^2 = 1.0000 — the linear story explains the response; these coefficients
are the whole picture, and a variance decomposition would add little.
```

Coefficients are **standardized**, so they are comparable across factors
regardless of units. Ranked by magnitude; the sign is the direction.

**R² is a trust diagnostic, not decoration.** Near 1 means the linear story
suffices and this ranking is the whole answer. Low means it does not — and that
is exactly the case variance-based indices exist for. The tool says which
you're in.

`--ranks` gives **SRRC**: the same regression on ranks, which recovers a
relationship that is monotone but curved. If `R²` is poor here and good with
`--ranks`, the response is monotone in your factors but not linear.

Errors rather than guesses: a constant response, a constant factor column, two
factors that move together (rank-deficient — their effects cannot be
separated), or fewer runs than factors.

## Worked example

[**the walkthrough**](../../examples/cookies/) — part of [one experiment carried through every tool](../../examples/cookies/),
where this one shows which direction each factor pushes the score.
