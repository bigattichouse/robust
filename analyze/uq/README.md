# uq — what the output distribution looks like

Morris and Sobol answer *which factors matter*. Neither says whether the
response is tight or wildly spread, symmetric or skewed, or how bad the bad
cases are. A design whose mean is fine and whose 5th percentile is a failure is
not a good design, and no sensitivity index reports that.

```sh
uq results.csv --metric yield --bins 8
```

```
Output distribution of 'response' — 120 runs

  mean           2.25      sd          4.29236
  min              -5      max              10
  p05              -5      p95              10
  p25        -1.66667      p75               5
  median      1.66667

  histogram (8 bins)
            -5 |##################################### 17  (cdf 0.142)
        -3.125 |######################################## 18  (cdf 0.292)
  ...
```

The histogram carries a running **cumulative fraction**, so it doubles as the
empirical CDF — read across for "what fraction of runs came in below this".

It reports **skew only when it is worth acting on**: if one tail is more than
twice the other, it says so and warns that the mean overstates a typical run.
Silence means the distribution is roughly symmetric.

A constant response is called out rather than summarised into a row of zeros.

Reads the same results-CSV dialect as everything else, including a `.front`
file — its preamble is comments.

**No row limit, and no design needed.** Unlike the other analyze stages, `uq`
has no array to check the file against — it summarises whatever responses it is
handed, so it sizes itself from the file in one pass before reading it. It needs
a real file rather than stdin for that reason. (It was capped at 1024 rows until
2026-08-18: it guessed a buffer size and grew on "buffer full", but the shared
reader reports a run id past the buffer as a *data* error — the right call for
the tools that pass a design's run count there — so the growth never happened
and larger files failed outright.)

## Worked example

[**the walkthrough**](../../examples/cookies/) — part of [one experiment carried through every tool](../../examples/cookies/),
where this one summarises how much the score varies across the design.
