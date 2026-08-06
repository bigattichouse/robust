# Chocolate Chip Cookie Optimization Example

This example demonstrates how to use the Taguchi Array Tool for optimizing a real-world experiment with manual execution - in this case, chocolate chip cookies.

## Problem Statement

You want to optimize your chocolate chip cookie recipe across 7 factors with 3 levels each. A full factorial would require 3^7 = 2,187 different batches, which is impractical. Using Taguchi's L27(3^13) orthogonal array, you only need 27 runs to get maximum information about factor effects.

### Factors and Levels
- **Butter Amount**: 1/2 cup, 3/4 cup, 1 cup
- **Sugar Ratio**: 1:1 brown:white, 2:1 brown:white, 3:1 brown:white  
- **Flour Type**: All-purpose, Bread flour, Cake flour
- **Eggs**: 1, 2, 3
- **Chocolate Chips**: 1/2 cup, 3/4 cup, 1 cup
- **Baking Temperature**: 325°F, 350°F, 375°F
- **Baking Time**: 8min, 10min, 12min

## Complete Workflow

### 1. Define the Experiment

Create `cookie_experiment.tgu`:

```yaml
factors:
  butter: 1/2 cup, 3/4 cup, 1 cup
  sugar_ratio: 1:1 brown:white, 2:1 brown:white, 3:1 brown:white
  flour_type: all_purpose, bread_flour, cake_flour
  eggs: 1, 2, 3
  chips: 1/2 cup, 3/4 cup, 1 cup
  baking_temp: 325F, 350F, 375F
  baking_time: 8min, 10min, 12min
array: L27
```

### 2. Generate Experimental Runs

```bash
taguchi generate cookie_experiment.tgu
```

### 3. Execute Experiments Manually

Work through the 27 runs systematically, one batch per day or as convenient. Rate each batch on:
- Taste (1-10 scale)
- Texture (1-10 scale)  
- Yield (actual cookie count)
- Appearance (1-10 scale)

Record results in `results.csv`:

```csv
run_id,batch_date,taste_score,texture_score,yield_count,appearance_score,notes
1,2026-01-01,8,7,22,8,"Great flavor, good texture"
2,2026-01-02,6,6,20,6,"Too dry"
...
```

### 4. Analyze Results

Once all 27 runs are complete:

```bash
# Calculate main effects for taste
taguchi effects cookie_experiment.tgu results.csv --metric taste_score

# Get optimization recommendations
taguchi recommend cookie_experiment.tgu results.csv --metric taste_score --higher-is-better
```

## Files in This Example

- `cookie_experiment.tgu` - The experiment definition
- `results_template.csv` - Template for recording your results  
- `sample_results.csv` - Simulated results showing expected data format
- `walkthrough.md` - Complete step-by-step guide with analysis example
- `generate_recipe_cards.sh` - Script to create printable recipe cards

## Benefits of This Approach

- **Efficiency**: 27 runs vs 2,187 full factorial (98.77% reduction!)
- **Systematic**: Proper statistical separation of factor effects
- **Actionable**: Clear identification of optimal settings
- **Practical**: Feasible for manual execution over time
- **Valid**: Statistically rigorous despite manual execution

## Real-World Application

This approach works for any optimization problem where:
- You have multiple factors to tune
- Experiments are time-consuming or costly
- You can measure quantitative outcomes
- The factors are independent or have minimal interactions

Common applications: baking recipes, brewing, manufacturing parameters, chemical formulations, performance tuning, etc.