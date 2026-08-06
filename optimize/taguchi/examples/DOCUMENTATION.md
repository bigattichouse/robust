# Taguchi Array Tool - Integration Examples Documentation

## Overview

This document describes the language integration examples for the Taguchi Array Tool.

## Directory Structure

```
bindings/python/          # Python package (primary)
│   taguchi/
│   ├── __init__.py       # Package entry: Experiment, Analyzer, Taguchi
│   ├── core.py           # Taguchi class — CLI subprocess wrapper
│   ├── experiment.py     # Experiment class — define factors, generate runs
│   └── analyzer.py       # Analyzer class — collect results, main effects
│   demo.py               # Quick-start demo
│   pyproject.toml        # pip-installable package metadata
│   README.md             # Python bindings documentation
examples/
├── nodejs/               # Node.js examples (FFI-based)
│   ├── basic_examples.js
│   ├── advanced_examples.js
│   └── package.json
└── long-running-experiments.md
└── chocolate-chips/      # End-to-end manual experiment walkthrough
```

## Python Bindings (`bindings/python/`)

The Python bindings wrap the `taguchi` CLI binary via subprocess. No shared
library or ctypes required — just a built CLI binary on your PATH or in the
build directory.

### Installation

```bash
make cli                        # build the taguchi binary
pip install bindings/python/    # install Python package
```

### Key Classes

#### `Experiment`

Defines an orthogonal array experiment and generates runs.

```python
from taguchi import Experiment

exp = Experiment()
exp.add_factor("learning_rate", ["0.001", "0.01", "0.1"])
exp.add_factor("batch_size", ["32", "64", "128"])
exp.add_factor("weight_decay", ["0.0", "0.1", "0.2"])

runs = exp.generate()
print(f"Array: {exp.array_type}, Runs: {exp.num_runs}")
for run in runs:
    print(f"  Run {run['run_id']}: {run['factors']}")
```

Use as a context manager to ensure temp files are cleaned up:

```python
with Experiment() as exp:
    exp.add_factor("depth", ["4", "6", "8"])
    runs = exp.generate()
    # temp .tgu file removed on __exit__
```

#### `Analyzer`

Collects results and calculates main effects.

```python
from taguchi import Experiment, Analyzer

exp = Experiment()
exp.add_factor("depth", ["4", "6", "8"])
exp.add_factor("lr", ["0.02", "0.04", "0.08"])

runs = exp.generate()
results = {1: 1.05, 2: 1.02, 3: 1.08, 4: 1.03, 5: 0.998,
           6: 1.045, 7: 1.04, 8: 1.015, 9: 1.055}

with Analyzer(exp, metric_name="val_loss") as analyzer:
    analyzer.add_results_from_dict(results)
    print(analyzer.summary())
    optimal = analyzer.recommend_optimal(higher_is_better=False)
```

### Saving and Loading `.tgu` Files

```python
exp.save("my_experiment.tgu")
exp2 = Experiment.from_tgu("my_experiment.tgu")
```

---

## Node.js Examples (`examples/nodejs/`)

The Node.js examples use `ffi-napi` to call `libtaguchi.so` directly.

### Requirements

- Node.js 14+
- `npm install ffi-napi ref-napi ref-array-napi`
- Built shared library (`make lib`)

### Running

```bash
cd examples/nodejs
npm install
node basic_examples.js
node advanced_examples.js
```

### FFI Pattern

```javascript
const ffi = require('ffi-napi');

const taguchiLib = ffi.Library('libtaguchi.so', {
  'taguchi_parse_definition': ['pointer', ['string', 'pointer']],
  'taguchi_free_definition': ['void', ['pointer']]
});

const defHandle = taguchiLib.taguchi_parse_definition(content, errorBuf);
```

---

## Choosing an Approach

| Approach | Language | Requires | Best for |
|----------|----------|----------|----------|
| Python bindings | Python 3.8+ | `taguchi` CLI binary | Scripting, ML hyperparameter search |
| Node.js FFI | JavaScript | `libtaguchi.so` | Web services, automation pipelines |
| Direct CLI | Any shell | `taguchi` binary | CI/CD, shell scripts |
| C API | C/C++ | `libtaguchi.a/.so` | Embedded use, performance-critical |

---

## Best Practices

- **Resource management**: Use context managers (`with` blocks) in Python to ensure
  temp files are cleaned up even on exceptions.
- **Input validation**: Validate factor names before passing to the library;
  names containing `=` will be rejected by the CLI.
- **Error handling**: Catch `TaguchiError` in Python; check return codes in Node.js.
- **Array selection**: Let the library auto-select arrays unless you have a specific
  reason to override — use `Experiment()` without `array_type` to auto-select.
