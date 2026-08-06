# resolve — confirm an effect, or pin down an interaction

The stage between *"the screen says this matters"* and *"act on it."* Both
tools here **spend new runs**, which is what separates them from
[`analyze/`](../analyze/): they generate a small design and execute it.

| Tool | Purpose | Status |
|---|---|---|
| `ofat` | One-factor-at-a-time confirmation around a base point. *"Any OA effect you act on costs exactly two more runs to verify"* — targets the aliasing / "16 dB artifact" failure mode. | M6 |
| `grid` | Small full-factorial (2–3 factors, 3×3) to **resolve** the interactions Sobol's `S_Tᵢ − Sᵢ` flags — exact, no aliasing. | M6 |

Why this stage exists, from `spec/screening-methods.md` §4: on a deterministic
model an orthogonal array can report whole decibels for a factor whose true
effect is negligible, because interaction leakage lands on its column. The
vinegar L27 scored `acetic_acid_molarity` at 3.53 dB where two OFAT runs showed
the true main effect was +0.03%. Hence the rule of thumb this stage encodes:
**any effect you intend to act on costs exactly two more runs to verify — always
spend them.**

Both build on `core/libdoe`. See [../DESIGN.md](../DESIGN.md) M6.
