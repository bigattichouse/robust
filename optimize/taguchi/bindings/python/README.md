# Taguchi Python Bindings

Python bindings for the Taguchi orthogonal array tool. Wraps the `taguchi`
CLI binary via subprocess — no shared library or ctypes required.

## Installation

Build the CLI first, then install the package:

```bash
make cli                        # from the repo root
pip install bindings/python/    # or: pip install -e bindings/python/
```

The package will locate the `taguchi` binary automatically (searches
`build/taguchi`, `/usr/local/bin/taguchi`, and `PATH`).

## Quick Start

```python
from taguchi import Experiment, Analyzer

# Define factors and generate runs
with Experiment() as exp:
    exp.add_factor("learning_rate", ["0.001", "0.01", "0.1"])
    exp.add_factor("batch_size",    ["32",    "64",   "128"])
    exp.add_factor("weight_decay",  ["0.0",   "0.1",  "0.2"])

    runs = exp.generate()
    print(f"Array: {exp.array_type}, Runs: {exp.num_runs}")

    # Execute experiments, then collect results
    results = {}
    for run in runs:
        print(f"Run {run['run_id']}: {run['factors']}")
        # results[run['run_id']] = your_metric_here

    # Analyze
    with Analyzer(exp, metric_name="val_loss") as analyzer:
        analyzer.add_results_from_dict(results)
        print(analyzer.summary())
        optimal = analyzer.recommend_optimal(higher_is_better=False)
        print(f"Best config: {optimal}")
```

> **Always use `with` blocks** — they ensure temporary files created during
> the session are cleaned up. `__del__` finalizers provide a safety net but
> context managers are the preferred pattern.

## API

### `Experiment`

```python
exp = Experiment(array_type=None)
```

| Method / Property | Description |
|---|---|
| `add_factor(name, levels)` | Add a factor; returns `self` for chaining |
| `generate()` → `list[dict]` | Generate and cache runs; each run is `{'run_id': int, 'factors': {str: str}}` |
| `array_type` | Auto-selected or explicit array name (e.g. `"L9"`) |
| `num_runs` | Number of runs (triggers generation) |
| `factors` | Copy of defined factors `{name: [levels]}` |
| `get_array_info()` | `{'rows', 'cols', 'levels'}` for the selected array |
| `to_tgu()` → `str` | Render definition as `.tgu` content |
| `save(path)` | Write `.tgu` file |
| `from_tgu(path)` *(classmethod)* | Load from an existing `.tgu` file |
| `get_tgu_path()` | Path to the temp `.tgu` file (created on demand) |
| `cleanup()` | Delete the temp `.tgu` file |

**Factor name rules**: names must not be empty or contain `=`, `#`, `:`,
spaces, or commas. These characters are reserved by the `.tgu` format or the
environment variable mechanism used by `taguchi run`.

### `Analyzer`

```python
analyzer = Analyzer(experiment, metric_name="response")
```

| Method / Property | Description |
|---|---|
| `add_result(run_id, value)` | Record a result; returns `self` for chaining |
| `add_results_from_dict(dict)` | Record multiple results; returns `self` |
| `main_effects()` → `list[dict]` | `[{'factor', 'range', 'level_means'}, ...]` |
| `recommend_optimal(higher_is_better=True)` → `dict` | `{factor: best_level}` |
| `get_significant_factors(threshold=0.1)` → `list[str]` | Factors with range ≥ threshold × max_range |
| `summary()` → `str` | Formatted text report |
| `cleanup()` | Delete the temp results CSV |

> `main_effects()` raises `TaguchiError` if no results have been added.

### `TaguchiError`

All errors from the library raise `TaguchiError(Exception)`.

## Saving and Loading Experiments

```python
# Save for later
exp.save("my_experiment.tgu")

# Reload — factors, array type, and definition order are preserved
exp2 = Experiment.from_tgu("my_experiment.tgu")
runs = exp2.generate()
```

`from_tgu` handles `#` comments (full-line and inline) and blank lines.

## Running Tests

```bash
cd bindings/python
python3 -m venv .venv
.venv/bin/pip install -e . pytest
.venv/bin/pytest tests/ -v
```

109 tests, all passing.

## How the binding talks to the CLI

It shells out and reads **`--json`** (`taguchi/_cli_json.py`), not the human
tables. Scraping them silently dropped data — a factor named `kv-type` never
matched the effects regex and vanished from the analysis.

The readers are strict where the scrapers were forgiving. A short run list, a
missing `settings` object, or a `schema` newer than this binding understands
raises instead of returning less. A partial design is not a smaller answer.

`Taguchi.effects()` and `.analyze()` still return the human text, because a
caller printing a report wants that. Nothing should *parse* it —
`effects_json()` is the one to build on.

## Tests

```sh
make test-bindings          # from the repo root
```

That runs `tests/test_cli_contract.py`, which pins the binding's **public API**
against the CLI: run counts and orthogonal balance out of `generate_runs`,
`range` against the level means it reports, and `recommend_optimal` naming a
declared level *value* under both objectives.

It is written against the public API on purpose. The binding currently parses
the CLI's human output (`core.py:168`, `analyzer.py:86`) and should move onto
`--json`; these tests stay meaningful across that change, so passing before and
after is evidence the switch preserved behaviour.

Two checks are `xfail(strict=True)`: a factor named `kv-type` is dropped from
the analysis today, because the effects regex matches the name with `\w+`.
Strict means the suite fails if they start passing — they have to be flipped to
plain tests as part of the switch, not left behind.

The binary comes from `$TAGUCHI_CLI`, else `<repo>/build/bin/taguchi`. It used
to prefer `optimize/taguchi/build/taguchi`, a path left over from when taguchi
built itself, which meant testing a stale binary and not knowing.

The other files in `tests/` are **not** run by `make`: 22 of their checks
assume a system install at `/usr/bin/taguchi` and fail on a clean machine.
