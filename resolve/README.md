# resolve — confirm an effect, or pin down an interaction

The stage between *"the screen says this matters"* and *"act on it."* Both
tools here **spend new runs**, which is what separates them from
[`analyze/`](../analyze/): they generate a small design and execute it.

| Tool | Purpose | Status |
|---|---|---|
| [`ofat`](ofat/) | **Built.** One-factor-at-a-time confirmation around a base point. *"Any OA effect you act on costs exactly two more runs to verify"* — targets the aliasing / "16 dB artifact" failure mode. Reports curvature too, which a two-level design would miss. | M6 ✓ |
| [`grid`](grid/) | **Built.** Small full-factorial (2–3 factors) to **resolve** the interactions Sobol's `S_Tᵢ − Sᵢ` flags — every combination actually run, so the interaction is measured exactly rather than estimated. | M6 ✓ |

Why this stage exists, from `spec/screening-methods.md` §4: on a deterministic
model an orthogonal array can report whole decibels for a factor whose true
effect is negligible, because interaction leakage lands on its column. The
vinegar L27 scored `acetic_acid_molarity` at 3.53 dB where two OFAT runs showed
the true main effect was +0.03%. Hence the rule of thumb this stage encodes:
**any effect you intend to act on costs exactly two more runs to verify — always
spend them.**

Both build on `core/libdoe`. See [../DESIGN.md](../DESIGN.md) M6.

## Using them

```sh
# A screen says `temp` matters. Confirm it before acting — 3 runs.
ofat model.space ./run.sh --factor temp --levels 3

# Sobol flagged temp and ph as interacting but not with whom. Resolve it — 9 runs.
grid model.space ./run.sh --factors temp,ph --levels 3
```

`grid` judges the interaction **against the main effects**, not against total
variation. The question you are really asking is *"if I optimise these two
independently, how wrong will I be?"* — which is the departure from additivity
measured against the effects you would act on. A share-of-variance threshold
answers a different question and gets it wrong: for `y = a + b + 0.2ab` the
interaction is a quarter of each main effect, plainly worth knowing, yet only
7.7% of total variation.

## Worked example

[**3. Which two are in a relationship?**](../examples/cookies/#3-which-two-are-in-a-relationship-grid) — part of [one experiment carried through every tool](../examples/cookies/),
where this one resolves temperature x time -- browning is a product, not a sum.
