# ML Hyperparameter Search with Taguchi Arrays

## Overview

The Taguchi library provides **orthogonal array experimental design** that can
significantly reduce the number of experiments needed to find optimal ML
hyperparameters — typically 67–94% fewer runs than a full factorial sweep,
while still identifying which parameters matter most and what their optimal
values are.

## Installation

```bash
# Build the taguchi CLI
cd ~/workspace/taguchi
make cli

# Install the Python package
pip install bindings/python/
```

## Usage Example

```python
from taguchi import Experiment, Analyzer

# Define hyperparameters to explore
with Experiment() as exp:
    exp.add_factor("DEPTH",        ["6",   "8",   "10"])
    exp.add_factor("MATRIX_LR",    ["0.02", "0.04", "0.08"])
    exp.add_factor("WEIGHT_DECAY", ["0.1",  "0.2",  "0.3"])

    # Auto-selects L9: 9 runs instead of 27 full factorial
    runs = exp.generate()
    print(f"Using {exp.array_type} array: {exp.num_runs} runs")

    results = {}
    for run in runs:
        factors = run["factors"]
        # Set hyperparameters, run training, collect your metric
        # val_loss = train(
        #     depth=int(factors["DEPTH"]),
        #     lr=float(factors["MATRIX_LR"]),
        #     weight_decay=float(factors["WEIGHT_DECAY"]),
        # )
        # results[run["run_id"]] = val_loss

    with Analyzer(exp, metric_name="val_loss") as analyzer:
        analyzer.add_results_from_dict(results)

        # Recommend optimal settings (lower loss is better)
        optimal = analyzer.recommend_optimal(higher_is_better=False)
        print(f"Optimal config: {optimal}")

        # See which factors matter most
        significant = analyzer.get_significant_factors(threshold=0.1)
        print(f"Most influential: {significant}")

        print(analyzer.summary())
```

## Efficiency Gains

| Factors | Levels | Full Factorial | Taguchi  | Savings |
|---------|--------|---------------|----------|---------|
| 3       | 3      | 27 runs        | 9 (L9)   | 67%     |
| 4       | 3      | 81 runs        | 9 (L9)   | 89%     |
| 7       | 2      | 128 runs       | 8 (L8)   | 94%     |
| 13      | 3      | 1,594,323 runs | 27 (L27) | >99%    |

## Notes

- **Factor names** must not contain `=`, `#`, `:`, spaces, or commas.
- **Use `with` blocks** for both `Experiment` and `Analyzer` to ensure
  temporary files are cleaned up automatically.
- **Run IDs are 1-indexed** — the first run is `run_id=1`, not `0`.
- Results can be added incrementally as experiments complete; call
  `analyzer.main_effects()` only after all results are in.

## Reference

See `bindings/python/demo.py` for a self-contained working example, and
`bindings/python/README.md` for the full API reference.
