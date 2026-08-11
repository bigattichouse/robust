# One experiment, every tool

You want to bake better cookies. You have seven things you could change and a
weekend. Testing every combination is 3⁷ = 2187 batches, which is not a
weekend, it is a career.

This walks the whole toolkit over that one problem, in the order you would
actually use it. Every command here runs in under a second — `model.sh` stands
in for the oven so you can follow along without baking anything.

**You don't need to know any statistics to read this.** Each step says what
question it answers and what to do with the answer.

```sh
cd examples/cookies          # every command below is run from here
```

**In a hurry, or just browsing?** Every output is committed in
[`output/`](output/) — start with [`output/study.html`](output/study.html), the
report this ends with. `./regenerate.sh` rebuilds them all.

---

## The problem

Seven knobs, in [`cookies.space`](cookies.space):

| | range | |
|---|---|---|
| `butter` | 0.5 – 1.0 cups | |
| `sugar_ratio` | 0.5 – 2.0 | brown : white |
| `flour` | 2.0 – 2.75 cups | |
| `egg` | 1 – 3 | whole eggs |
| `chips` | 0.5 – 1.5 cups | |
| `temp` | 325 – 425 °F | |
| `time` | 8 – 14 min | |

You score each batch out of 100. You also care about cost and how long it ties
up the oven, but start with taste.

---

## 1. Which knobs matter at all? — `morris`

Turning all seven at once is wasteful. Most of them probably don't matter, and
you want to find out cheaply which ones do.

```sh
morris sample cookies.space > design.csv
./bake.sh design.csv > results.csv
morris analyze cookies.space results.csv --metric taste
```

```
Factor                        mu*         [95% CI]        sigma   note
temp                        46.47      [26.6,70.8]         58.8   interacting/nonlinear
butter                      33.99      [32.6,34.7]        33.21   interacting/nonlinear
time                        27.27      [12.5,45.3]        38.26   interacting/nonlinear
chips                        8.25         [6.75,9]        2.598
sugar_ratio                 4.375         [1.75,7]        5.407   interacting/nonlinear
flour                           3            [3,3]            0
egg                           0.8        [0.8,0.8]    7.833e-05
```

**How to read it.** `mu*` is how much each knob moves the score — bigger means
it matters more. The ranking is the answer: temperature, butter and time do
almost all the work. Eggs do essentially nothing (0.8 out of a 46-point spread),
so stop varying them and use two.

**`sigma` is the second thing to look at.** When it's large next to `mu*`, that
knob doesn't act on its own — its effect depends on where the others are set.
Three factors are flagged, so hold that thought.

**`chips` has sigma ≈ 0**, which means the opposite: more chocolate is better by
a fixed amount, no matter what else you do. Nothing more to learn about chips.

> **Have I baked enough batches?** `trajectories:` in the `.space` is a guess.
> Don't guess:
> ```sh
> morris converge cookies.space ./model.sh --target-ci 8
> ```
> It doubles the batches until every confidence interval is narrower than 8
> points, then tells you the number to write in the file.

---

## 2. How much does each matter, and do they interact? — `sobol`

Screening ranked them. Now get shares of the variation, and settle what
`sigma` hinted at.

```sh
sobol sample cookies.space > sdesign.csv
./bake.sh sdesign.csv > sresults.csv
sobol analyze cookies.space sresults.csv --metric taste
```

`S1` is what a knob explains **on its own**. `ST` also counts what it explains
**through its partners**. When `ST` is much bigger than `S1`, that knob is in a
relationship with another one — and the sum of all the `S1`s falling well short
of 1 says the same thing about the recipe as a whole.

---

## 3. Which two are in a relationship? — `grid`

`sobol` says interactions exist. It cannot say *which pair*. For that, cross two
factors properly:

```sh
grid cookies.space ./model.sh --factors temp,time --levels 3
```

```
Interaction relative to the larger main effect: 68.0%

These two DO interact. Their main effects are not additive, so
optimising them independently will not find the joint optimum --
read the table, not the two ranges.
```

**This is the real lesson of the whole example.** Browning is temperature
*times* time, not temperature *plus* time. 400 °F for 9 minutes and 350 °F for
13 browns about the same. So "the best temperature" is not a question with an
answer until you fix the time — and tuning them one at a time will walk you
into a corner.

> `ofat` answers the narrower question — *is this one effect real?* — by
> sweeping a single factor with everything else held still:
> ```sh
> ofat cookies.space ./model.sh --factor butter --levels 5
> ```

---

## 4. What's the best setting? — `taguchi`

Now optimise, over the survivors only. An orthogonal array gets you main
effects for nine batches instead of eighty-one.

```sh
taguchi generate cookies.tgu --csv > tdesign.csv
./bake.sh tdesign.csv > tresults.csv
taguchi analyze cookies.tgu tresults.csv --metric taste
```

It prints each factor's level means and an **Optimal Configuration**.

### Then test that recommendation, because it is a guess

The array never actually baked the combination it just recommended. It
*predicted* it, by assuming the factors add up — and step 3 proved they don't.

```sh
taguchi confirm cookies.tgu tresults.csv --metric taste          # what to expect
taguchi confirm cookies.tgu tresults.csv --metric taste --measured 96.5
```

```
The additive prediction did NOT hold. The measurement is off by a
margin comparable to the effects themselves, which means something
the array could not see -- an interaction, or aliasing.
```

**Bake the recommended batch and type in what you got.** If the prediction
holds, the simple model was good enough. If it doesn't, you've learned
something real — and skipping this step means never finding out.

---

## 5. Where exactly is the peak? — `rsm`

Main effects pick the best *level you tried*. The best setting is usually
between two of them. A response surface fits a curved model and solves for the
top:

```sh
rsm sample rsm.space > rdesign.csv
./bake.sh rdesign.csv > rresults.csv
rsm analyze rsm.space rresults.csv --metric taste
```

```
Stationary point is a maximum
Predicted taste: 101.745

factor                      coded          value
butter                         -0           0.75
temp                      -0.2806        369.047
```

369 °F, and it never appeared in any design — that's the point of fitting a
curve rather than picking a winner.

**When there is no single answer, it says so.** Try it on `temp` and `time`
together and you get *saddle* — correct, because those two trade off against
each other along a ridge. Any temp/time pair that browns the same is equally
good, and the tool refuses to invent a winner.

---

## 6. Will it survive a bad oven? — `taguchi robust`

Everything so far assumed the oven is right. Ovens are not right. Yours might
run 30 °F hot and you cannot fix it — but you *can* deliberately test both ways.

```sh
taguchi generate cookies-robust.tgu > cdesign.csv
./bake.sh cdesign.csv > cresults.csv
taguchi robust cookies-robust.tgu cresults.csv --metric taste --sn larger
```

```
factor               robust (S/N)     mean-optimal
temp                 375              375
flour                2.75             2.375              <- differ

The robust and mean-optimal settings DIFFER.
```

**Read that carefully.** Two different answers for flour. On *average*, 2.375
cups scores better. But 2.75 makes a drier dough that rides over the oven being
wrong, and it is the one that still works on a bad day.

The best average and the most dependable are not the same recipe, and only
crossing your recipe against the thing you can't control will tell you which is
which. That's the "robust" this toolkit is named for.

---

## 7. Taste isn't the only thing — `desire` and `pareto`

Chocolate is expensive and oven time is real. Three objectives, one recipe.

```sh
./bake.sh design.csv --all > multi.csv         # taste, cost and minutes
desire --max taste --min cost --min minutes multi.csv > scored.csv
```

`desire` folds them into a single `desirability` column, so every tool above
works on trade-offs unchanged:

```sh
desire --max taste --min cost --min minutes multi.csv \
  | morris analyze cookies.space - --metric desirability
```

It multiplies rather than averages, deliberately: a recipe that fails one
requirement outright scores zero and cannot be rescued by being wonderful
elsewhere.

`pareto` answers the other half — not "which is best" but "what are my
options":

```sh
pareto filter multi.csv --max taste --min cost
```

Every recipe where you can't improve one thing without giving up another. Use
`desire` to pick; use `pareto` to see what you're giving up.

---

## 8. Write it down — `report`

```sh
morris analyze cookies.space results.csv --metric taste --json > morris.json
sobol  analyze cookies.space sresults.csv --metric taste --json > sobol.json
report morris.json sobol.json --html study.html
```

One self-contained HTML page — no internet, no dependencies, opens in ten
years — with a **Pareto chart of effects**: bars for each factor's
contribution, largest first, and a line for the running total. Where that line
flattens is where the remaining factors stopped paying for themselves.

---

## The short version

```
morris    which knobs matter?              -> temp, butter, time. not eggs.
sobol     how much, and do they interact?  -> yes, substantially
grid      which pair?                      -> temp x time. browning is a product.
taguchi   best setting from 9 batches      -> a recommendation
confirm   was that prediction any good?    -> test it, don't assume
rsm       where exactly is the peak?       -> 369F, between the levels tried
robust    will it survive a bad oven?      -> use more flour than tastes best
desire    taste vs cost vs time            -> one number to optimise
pareto    what are my options?             -> the trade-off set
report    write it down                    -> one HTML page
```

Each stage narrows the question. Screening spends few runs on many factors;
each later stage spends more runs on fewer. **That order is the whole method** —
running an RSM over seven factors is the 2187 batches you were avoiding.

---

## Everything this produced

[`output/`](output/) holds every design, result, table and the final page —
committed, so you can read the study without running it. See
[`output/README.md`](output/README.md) for a map.

They are not decoration: `examples/tests/test_examples.sh` regenerates them and
fails the build if the tools no longer produce what is committed here. A worked
example that has quietly drifted from the software is worse than none.

## Files

| | |
|---|---|
| `model.sh` | the "oven" — cookie physics as arithmetic |
| `bake.sh` | runs a design through it, writes a results CSV |
| `cookies.space` | seven factors, for the screening tools |
| `cookies.tgu` | the survivors, as an orthogonal array |
| `cookies-robust.tgu` | temp × flour crossed against a wrong oven |
| `rsm.space` | butter × temp, for the response surface |

`model.sh` is honest about what it is: a formula with a browning peak, a real
temperature×time product, an asymmetry (burnt is worse than pale), and a
sensitivity that flour damps. Every conclusion above is a property of that
formula — which is exactly what makes it a good teaching kitchen and a bad
recipe.
