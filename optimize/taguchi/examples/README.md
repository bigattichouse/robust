# Taguchi Array Tool - Examples

This directory contains practical examples demonstrating how to use the Taguchi Array Tool for various experiment types.

## Directory Structure

```
examples/
├── long-running-experiments.md     # Guide for long-running/manual experiments
├── hyperparameter-search.md        # ML hyperparameter optimization with Python bindings
├── chocolate-chips/               # Complete example: Chocolate chip cookie optimization
│   ├── cookie_experiment.tgu      # Experiment definition file
│   ├── results_template.csv       # Template for result collection
│   ├── sample_results.csv         # Sample data from completed experiment
│   ├── generate_recipe_cards.sh   # Script to create manual execution cards
│   ├── walkthrough.md             # Complete step-by-step guide
│   └── README.md
└── nodejs/                        # Node.js API binding examples
    └── taguchi_binding.js         # ffi-napi Node.js bindings
```

## Examples Overview

### 1. Long-Running Experiments
Documentation for using the tool with experiments that take time to complete, including:
- Manual experiments (cooking, baking, manual assembly)
- Automated long-running processes (performance tests, stress tests)
- Delayed execution workflows
- State management for interrupted workflows

### 2. ML Hyperparameter Search
Using the Python bindings to efficiently sweep ML hyperparameters with 67–94% fewer
training runs than full factorial search. Covers `Experiment`, `Analyzer`,
`recommend_optimal`, and `get_significant_factors`.

### 3. Chocolate Chip Cookie Optimization
A complete end-to-end example showing how to optimize a chocolate chip cookie recipe:
- 7 factors at 3 levels each (27-run L27 array)
- Manual execution workflow
- Data collection and analysis
- Result interpretation and optimization

### 4. Node.js API Client
Examples demonstrating how to interface with the C library from Node.js using FFI.

## Getting Started

### For Manual Experiments (Like Cookie Optimization)
1. Define your experiment in a `.tgu` file
2. Generate experimental runs: `taguchi generate experiment.tgu`
3. Execute runs manually (for long-running experiments)
4. Record results in CSV format
5. Analyze results when complete: `taguchi analyze experiment.tgu results.csv`

### For Automated Long-Running Experiments
1. Define your experiment in a `.tgu` file
2. Create an external script that performs a single experimental run
3. Run experiments: `taguchi run experiment.tgu './my_script.sh'`
4. The tool will execute your script once for each run with appropriate environment variables

## Key Concepts

### Orthogonal Arrays
- **L4**: 4 runs, 3 factors, 2 levels each
- **L8**: 8 runs, 7 factors, 2 levels each
- **L9**: 9 runs, 4 factors, 3 levels each
- **L16**: 16 runs, 15 factors, 2 levels each
- **L27**: 27 runs, 13 factors, 3 levels each

### Factor Levels
Factors are varied across predetermined discrete values:
- `factors: cache_size: 64M, 128M, 256M` (3 levels)  
- `factors: threads: 2, 4, 8, 16` (4 levels, would need L16 or L27 array)

### Environment Variables in Run Mode
When using `taguchi run`, each execution receives environment variables:
- `TAGUCHI_FACTOR_NAME=value` for each factor
- `TAGUCHI_RUN_ID=run_number` for the experiment ID

## File Format (.tgu)

The tool uses YAML-like syntax for experiment definition:

```yaml
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30s, 60s, 120s
array: L9
```

## Best Practices

1. **Randomize run order** to minimize time-based biases
2. **Use consistent measurement protocols** across all runs
3. **Record environmental conditions** that might affect results
4. **Include replication** of standard conditions for quality control
5. **Plan for missing data** - have procedures for failed runs

## Next Steps

Once you've completed your experiments:

```bash
# Analyze results to find optimal settings
taguchi analyze experiment.tgu results.csv --metric target_metric

# Calculate main effects of each factor
taguchi effects experiment.tgu results.csv

# Get optimization recommendations
taguchi recommend experiment.tgu results.csv --metric target --higher-is-better
```