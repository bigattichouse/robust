# Errata — Campolongo, Cariboni & Saltelli (2007)

Discrepancies found while implementing methods from:

> Campolongo, F., Cariboni, J., Saltelli, A. (2007). **An effective screening
> design for sensitivity analysis of large models.** *Environmental Modelling &
> Software* **22**(10), 1509–1518.
> [doi:10.1016/j.envsoft.2006.10.004](https://doi.org/10.1016/j.envsoft.2006.10.004)
>
> Local copy: [`../pdf/campolongo-2007-morris-screening.pdf`](../pdf/campolongo-2007-morris-screening.pdf)

Each finding gets a subdirectory named for the table or figure it concerns,
holding a self-contained program that reproduces it. Those programs depend on
nothing outside their own directory — no build system, no libraries beyond
libm, no input files, no network — so any one of them can be handed to someone
who has never seen this project.

| Finding | Where | Status | Reproduce with |
|---|---|---|---|
| Table 1, test case 3, group v: S_T printed as 0.393, should be 0.417 | p.1512 | Confirmed by two independent routes | [`table1/`](table1/) — `make run` |

**This is not a criticism of the paper.** The method is correct and is the
basis of what this project builds; §3.3 in particular solved the problem that
sent us looking. One number in one table is printed wrong. It is recorded here
only because that table is the natural validation target for anyone
implementing group screening, and a wrong target costs real time.

---

## Finding 1 — Table 1, test case 3, group v

**Printed (p.1512):** the "S_T group analytical" column gives **0.393** for
test case 3, group v = {X3, X5, X9}.

**Should be:** **0.417**.

### Evidence

Two independent routes agree with each other and disagree with the printed
value. Reproduce both with `cd table1 && make run` (about one second).

| Group | Closed form | Monte Carlo | Printed |
|---|---|---|---|
| u {X1,X4,X8} | 0.43645 | 0.43688 | 0.436 ✓ |
| **v {X3,X5,X9}** | **0.41707** | **0.41709** | **0.393 ✗** |
| w {X2,X6,X7} | 0.42929 | 0.42869 | 0.429 ✓ |

The two routes are:

1. **Closed form.** From the g-function's partial variances
   `V_i = (1/3)/(1 + a_i)²` and `V = ∏ᵢ (1 + V_i) − 1`, a group's total index
   is one minus the share of variance involving only factors outside the group:

   ```
   S_T(u) = 1 − [ ∏_{j ∉ u} (1 + V_j) − 1 ] / V
   ```

   Source: Saltelli & Sobol' (1995), *Reliability Engineering and System
   Safety* **50**, 225–239 — the same closed form the paper's own analytical
   column derives from.

2. **Monte Carlo**, using no closed form and no value from the paper:

   ```
   S_T(u) = ( 1/(2N) · Σₙ [ f(Aₙ) − f(A_Bᵘₙ) ]² ) / V(Y)
   ```

   with A, B independent uniform samples and `A_Bᵘ` equal to A with group u's
   columns taken from B. N = 2,000,000. Source: Jansen (1999), *Computer
   Physics Communications* **117**, 35–43.

### Why this is a typo and not a definitional difference

Three things point the same way:

- **The other eight values reproduce exactly.** All six in test cases 1 and 2,
  and the other two groups in test case 3's own row. A different definition of
  the group total index would not agree eight times and fail once.
- **It is not the first-order index.** `S(v)` for that group is 0.239, not
  0.393 — so it is not a first-order/total-order mix-up.
- **It is not a mislabelled group.** All 84 three-factor subsets of that
  `a`-vector were checked; none has a total index of 0.393.

The program checks all nine printed values, not just the disputed one, and
exits nonzero unless it finds exactly the single expected discrepancy. If a
future reader gets a different result, it says so rather than quietly agreeing.

### Has this been reported before?

**No. Checked 2026-08-06, including the registry where corrections are
formally recorded.**

- **Crossref has no correction registered for this DOI.** Querying
  `api.crossref.org/works/10.1016/j.envsoft.2006.10.004` returns
  `update-to: null`, `relation: {}`, `updates: null`. Publishers register
  corrigenda, errata and retractions through Crossref/CrossMark, so an empty
  record here is close to authoritative for "no formal correction exists".
- **No corrigendum points at it from the other direction either.** A Crossref
  search for corrigenda in *Environmental Modelling & Software* matching this
  paper's title returns **0 results**.

  Reproduce both:

  ```sh
  curl -s "https://api.crossref.org/works/10.1016/j.envsoft.2006.10.004" \
    | python3 -c "import json,sys; m=json.load(sys.stdin)['message']; \
      print(m.get('update-to'), m.get('relation'), m.get('updates'))"
  ```

- **No corrigendum or erratum** turned up in general web search either.
  Elsevier's article landing page returns HTTP 403 to automated fetches, so its
  "related articles" panel was not read directly — but the Crossref checks above
  cover the same ground more reliably, since that panel is generated from the
  same registration data.
- **The authors' own later work does not reuse these test cases.** Saltelli
  et al. (2011), *From screening to quantitative sensitivity analysis*, CPC,
  and the JRC EUR handbook *Sensitivity analysis: how to detect important
  factors in large models* were both retrieved and searched for the Table 1
  values (0.393, 0.417, 0.436, 0.429, and the μ\* column 8.108, 7.083, 6.364).
  **Zero hits in either.** So the numbers were neither republished nor quietly
  corrected in the follow-on literature.
- **SALib** implements Morris with groups and cites this paper for it, but
  nothing indicates it validates against Table 1 specifically.
- Later work revisiting the μ\* ↔ S_T proxy question — Feng, Lu & Yang,
  *Enhanced Morris method for global sensitivity analysis: good proxy of
  Sobol' index*, Struct. Multidisc. Optim. **59**, 373–387 (2019) — addresses
  the proxy relationship for non-uniform inputs, not this table. It carries a
  2020 correction ([10.1007/s00158-020-02712-2](https://doi.org/10.1007/s00158-020-02712-2),
  local copy in `../pdf/`), which was read: **45 citation corrections, no
  change to any equation, result or figure.** So it neither reports this
  erratum nor bears on it.

**A plausible reason it has gone unnoticed:** §3.3, the group extension, is the
least-used part of a heavily-cited paper. Most citations are for μ\* and the
improved sampling strategy. If even the authors' own later papers do not reuse
the group test cases, few implementations will have used Table 1 as a
regression target — which is exactly what we were doing when we hit it.

The Crossref result is strong evidence that **no formal correction was ever
published**. It cannot rule out an informal report — a mailing-list post, a
GitHub issue, a remark in someone's thesis — so if this is ever raised with the
authors, "we could not find a prior report" is the honest phrasing, not "this
is unreported". A single wrong cell in one table of a 2007 paper is precisely
the kind of thing that survives two decades unremarked.

### Suggestions

**For anyone implementing group screening.** Validate against **0.417** for
that cell. This is the concrete cost of the typo: Table 1 is the obvious
regression target for a group-μ\* implementation, and a correct implementation
will appear to fail on it. We assumed the bug was ours for a while.

**For reporting it.** The Errata Summary below is written to be used as-is.
Realistic expectations:

- **A corrigendum is very unlikely.** The paper is from 2007. Formal
  corrections to papers that old are essentially never issued. The value of
  reporting is that the authors know, and that a search turns this up.
- **Contact details need looking up.** The corresponding-author address printed
  in the paper is a European Commission JRC address from 2007 and is almost
  certainly dead. Find current affiliations rather than using it.
- **Consider telling implementers, not only authors.** The people who will
  actually lose time are those validating code against this table. If a widely
  used sensitivity-analysis library implements Morris with groups and
  references Table 1 in its tests or documentation, an issue filed there
  reaches them directly. Worth checking whether any does before assuming so.
- **Scope it accurately.** It is one number, in a paper whose method is sound
  and is the basis of what this project builds.

---

## Errata Summary

*Self-contained; carries its own citation so it can be lifted out and used
as-is.*

Campolongo, F., Cariboni, J., Saltelli, A. (2007). "An effective screening
design for sensitivity analysis of large models." *Environmental Modelling &
Software* **22**(10), 1509–1518. doi:10.1016/j.envsoft.2006.10.004

Table 1 (p.1512), test case 3, group v = {X3, X5, X9}: the "S_T group
analytical" column gives **0.393**. The correct value is **0.417**.

Recomputing that index from the g-function's closed-form partial variances
gives 0.41707. An independent Monte Carlo estimate — Jansen's estimator,
2×10⁶ samples, using no closed form — gives 0.41709. The other two groups in
the same row reproduce the printed values exactly (0.436 and 0.429), as do all
six values in test cases 1 and 2, so this is an isolated transcription error
rather than a difference in definition. It is also not the group's first-order
index (0.239), and no other three-factor subset of that `a`-vector has a total
index of 0.393, so it is not a mislabelled group.

The consequence is narrow but real: Table 1 is the natural validation target
for an implementation of the group elementary-effects method of §3.3, and a
correct implementation will appear to fail against that one cell.

A self-contained C program reproducing both calculations is in
[`table1/`](table1/) — no dependencies beyond a C compiler, about a second to
run. It checks all nine printed values and exits nonzero unless it finds
exactly this one discrepancy.

---

## Adding a finding

Create a subdirectory named for the table or figure, keep it dependency-free,
and add a row to the table at the top. The rules that make these shareable:

- **One directory, one finding**, named for what it concerns (`table1/`,
  `fig7/`).
- **No dependencies.** C99 and libm. Inline anything else — the PRNG in
  `table1/` is inlined for exactly this reason, which also makes its Monte
  Carlo figures reproducible bit-for-bit.
- **Check everything, not just the disputed value.** The argument rests on what
  *does* reproduce as much as on what does not.
- **Two independent routes** before calling anything an error.
- **Rule out the innocent explanations** in code, so the report says what the
  printed number is *not*, rather than only that it looks wrong.
- **Exit nonzero unless the expected discrepancy is found**, so the program
  detects its own obsolescence if a value is ever corrected.
