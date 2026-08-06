# Taguchi Array Tool

A robust, Unix-philosophy command-line tool for designing and analyzing experiments using Taguchi orthogonal arrays. The tool is small, fast, composable, and follows strict POSIX conventions. The core is built as a shared library to enable bindings for Python, Node.js, and other languages.

## What are Taguchi Arrays?

Taguchi arrays are a statistical method for designing experiments using orthogonal arrays. Instead of testing every possible combination of factors (which grows exponentially), Taguchi methods use mathematically designed arrays that allow you to study the effect of multiple factors with a minimal number of experiments.

**Example Problem:**
Suppose you want to optimize cookie baking with 4 factors:
- Temperature: 350°F, 375°F, 400°F
- Time: 8min, 10min, 12min  
- Sugar: 1/2 cup, 3/4 cup, 1 cup
- Flour: 2 cups, 2.5 cups, 3 cups

Testing all combinations would require 3^4 = 81 experiments!

**Taguchi Solution:**
Using an L9 orthogonal array, you only need 9 experiments that still give you statistically valid information about each factor's effect:

| Run | Temperature | Time | Sugar | Flour |
|-----|-------------|------|-------|-------|
| 1   | 350°F       | 8min | 1/2c  | 2c    |
| 2   | 350°F       | 10min| 3/4c  | 2.5c  |
| 3   | 350°F       | 12min| 1c    | 3c    |
| 4   | 375°F       | 8min | 3/4c  | 3c    |
| 5   | 375°F       | 10min| 1c    | 1/2c  |
| 6   | 375°F       | 12min| 1/2c  | 2.5c  |
| 7   | 400°F       | 8min | 1c    | 2.5c  |
| 8   | 400°F       | 10min| 1/2c  | 3c    |
| 9   | 400°F       | 12min| 3/4c  | 1/2c  |

This design ensures that each factor's levels are evenly distributed and balanced against other factors, allowing main effects to be studied independently.

## Features

- **Orthogonal Array Support**:
  - GF(2) series: L4, L8, L16, L32, L64, L128, L256, L512, L1024 (2-level)
  - GF(3) series: L9, L27, L81, L243, L729, L2187 (3-level)
  - GF(5) series: L25, L125, L625, L3125 (5-level)
  - Mixed-level: **L18** (1 factor × 2 levels + up to 7 factors × 3 levels, 18 runs)
- **Smart Auto-Selection**: Prefers arrays with 50-200% capacity margin for better statistical power, with exact level matching prioritized
- **Column Pairing**: Multi-level factors (4-27 levels) via automatic column pairing/tripling
- **Mixed-Level Support**: Factors with different level counts in the same experiment
- **Main Effects Analysis**: Calculate factor significance, level means, and optimal configurations
- **YAML-like Configuration**: Human-readable `.tgu` experiment definition files
- **JSON Serialization**: Output for language bindings and external tools
- **Memory Safe**: No leaks, bounds checking, safe string handling
- **Cross-Platform**: Pure C99 with POSIX libc
- **Extensible**: Library-first design for maximum reusability

## Build Status

- ✅ **Shared Library** (`libtaguchi.so` / `libtaguchi.dylib` / `taguchi.dll`)
- ✅ **Static Library** (`libtaguchi.a`)
- ✅ **CLI Tool** (`taguchi`) - Complete
- ✅ **Build System**: Makefile with comprehensive targets
- ✅ **Tests**: Comprehensive unit and integration tests
- ✅ **Memory Safety**: Valgrind clean

## 🎯 **Project Status: COMPLETE**

The Taguchi Array Tool has been successfully implemented as per the original specifications. All core functionality is complete and working with production-quality code.

### ✅ **Core Modules Delivered**
- **Infrastructure**: Memory management, error handling, utilities
- **Arrays**: All orthogonal arrays (L4, L8, L9, L16, L27, L81, L243) with verified orthogonality
- **Parser**: `.tgu` file parsing and validation
- **Generator**: Mapping factor definitions to experiment runs  
- **Serializer**: JSON serialization for language bindings
- **Analyzer**: Statistical analysis and main effects calculation
- **Public API**: Complete facade connecting all modules
- **CLI**: Command-line interface with generate, run, analyze, effects, validate, suggest-array, list-arrays commands

### 🔄 **Active Development**
- **Language Bindings**: Python and Node.js examples implemented

## Quick Start

### Building the Library
```bash
make lib          # Build shared and static libraries
make cli          # Build command-line tool
make test         # Run comprehensive test suite
make check        # Run static analysis and valgrind
```

### Command-Line Examples
```bash
# Create experiment definition file (experiment.tgu)
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30, 60, 120
array: L9

# Generate experiment runs (9 runs instead of 3^3 = 27 full factorial)
./taguchi generate experiment.tgu

# Execute experiments with external script (runs once per experimental configuration)
./taguchi run experiment.tgu "./my_test.sh"

# Validate experiment definition
./taguchi validate experiment.tgu

# List available orthogonal arrays
./taguchi list-arrays

# Analyze results — simple two-column CSV (run_id, response)
./taguchi analyze experiment.tgu results.csv

# Analyze results — multi-column CSV, pick a named metric
./taguchi analyze experiment.tgu results.csv --metric throughput

# Multiple metrics in one results file — analyse each independently
./taguchi effects experiment.tgu results.csv --metric system_COP
./taguchi effects experiment.tgu results.csv --metric heating_COP

# Minimize a metric (e.g., latency)
./taguchi analyze experiment.tgu results.csv --metric latency --minimize

# Ask the tool which array it would pick for a given .tgu file
./taguchi suggest-array experiment.tgu
```

### Mixed-Level Experiments (L18)

L18 is the standard array for experiments that combine one 2-level factor with up
to seven 3-level factors — 18 runs instead of 27 (the next pure GF(3) array).

```bash
# hot_glue.tgu — one binary factor + six 3-level process parameters
factors:
  wax_type: beeswax, carnauba
  temp: low, medium, high
  pressure: 10psi, 20psi, 30psi
  time: 30s, 60s, 90s
  humidity: 40%, 60%, 80%
  binder: type_a, type_b, type_c
array: L18

./taguchi generate hot_glue.tgu    # 18 runs, perfectly balanced
./taguchi analyze hot_glue.tgu results.csv
```

The 2-level factor must be listed anywhere in the factors block — the tool
automatically assigns it to the 2-level column and all 3-level factors to
the 3-level columns.

### Column Pairing for Multi-Level Factors

Factors with more levels than the array's base level count are automatically handled
via column pairing. For example, a 9-level factor in a 3-level array (L81) uses 2
columns paired together (3^2 = 9 combinations):

```bash
# experiment.tgu with mixed levels
factors:
  n_stages: 1, 2, 3, 4, 5, 6, 7, 8, 9
  mode: pumped, static
  temp: low, medium, high
array: L81

# Generates 81 runs - the 9-level factor uses column pairing automatically
./taguchi generate experiment.tgu
```

### C Library Integration Example
```c
#include <taguchi.h>
#include <stdio.h>

int main() {
    char error[TAGUCHI_ERROR_SIZE];

    // Parse experiment definition from string
    const char *tgu_content =
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4, 8\n"
        "array: L9\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(tgu_content, error);
    if (!def) {
        fprintf(stderr, "Parse error: %s\n", error);
        return 1;
    }

    // Generate experiment runs
    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
        fprintf(stderr, "Generate error: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }

    // Print runs
    printf("Generated %zu runs:\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("Run %zu: cache_size=%s, threads=%s\n",
               taguchi_run_get_id(runs[i]),
               taguchi_run_get_value(runs[i], "cache_size"),
               taguchi_run_get_value(runs[i], "threads"));
    }

    // Cleanup
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);

    return 0;
}
```

### Python Integration Example
```python
import ctypes
import os

# Load the library
lib_path = "./libtaguchi.so"  # Adjust path as needed
lib = ctypes.CDLL(lib_path)

# Use ctypes to interface with the C library
# (Refer to examples/python/ for full implementation)
```

### Node.js Integration Example
```javascript
const ffi = require('ffi-napi');
const ref = require('ref-napi');

// Load the C library
const taguchiLib = ffi.Library('./libtaguchi.so', {
  'taguchi_list_arrays': ['pointer', []],
  'taguchi_parse_definition': ['pointer', ['string', 'pointer']]
  // (Refer to examples/nodejs/ for full implementation)
});
```

## Multi-Column Results CSV

Simulations often produce multiple output metrics alongside factor columns in a single
results file. The `effects` and `analyze` commands handle this natively:

```
# results.csv produced by a simulation
run_id,endpoint_type,pressure,system_COP,heating_COP,T_fridge_C,Qh_W
1,endpoint_only,low,3.21,4.12,-18.5,1200
2,endpoint_only,medium,2.98,3.87,-19.1,1180
...
```

```bash
# Analyse any named column without pre-splitting the file
./taguchi effects experiment.tgu results.csv --metric system_COP
./taguchi effects experiment.tgu results.csv --metric Qh_W
./taguchi analyze experiment.tgu results.csv --metric T_fridge_C --minimize
```

The `--metric` flag locates the column by header name. Non-numeric columns (factor
columns, string labels) in all other positions are simply ignored.

## File Format (.tgu)

The `.tgu` file format is YAML-like for defining experiments:

```yaml
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30, 60, 120
  algorithm: algo1, algo2, algo3
array: L9  # Optional - auto-selected if omitted
```

The `array:` line is optional. If omitted, the tool automatically selects the smallest
orthogonal array that can accommodate all factors. Factors can have different numbers of
levels (mixed-level designs), and factors with more levels than the array's base use
column pairing automatically.

## API Overview

### Core Function Categories
- **Definition**: `taguchi_parse_definition()`, `taguchi_validate_definition()`
- **Generation**: `taguchi_generate_runs()`, `taguchi_run_get_value()`
- **Analysis**: `taguchi_calculate_main_effects()`, `taguchi_recommend_optimal()`
- **Utility**: `taguchi_list_arrays()`, `taguchi_get_array_info()`

### CLI Commands
- `generate <file.tgu>`: Generate experiment runs from definition
- `run <file.tgu> <script>`: Execute external script for each run
- `analyze <file.tgu> <results.csv>`: Full analysis with optimal configuration recommendation
- `effects <file.tgu> <results.csv>`: Calculate and display main effects table
- `validate <file.tgu>`: Validate experiment definition
- `list-arrays`: List available orthogonal arrays with details (rows, columns, levels)
- `--help`, `--version`: Standard utilities

## Architecture

The system follows a modular design with clear separation of concerns:
- **Core Library**: Pure C implementation with no I/O dependencies
- **Public API**: Stable interface with opaque handles and error handling
- **CLI Interface**: Thin wrapper that calls library functions
- **Language Bindings**: FFI-based integration for other languages

## Quality Assurance

- **Memory Safe**: All tests pass valgrind with zero leaks/errors
- **Static Analysis**: Clean with -Wall -Wextra -Werror -pedantic -std=c99
- **Comprehensive Testing**: Unit tests for all modules with integration tests
- **API Consistent**: Stable, well-documented interface with proper error handling

## Usage Scenarios

### Manufacturing Optimization
Optimize production processes with multiple parameters (temperature, pressure, time, material type)

### Software Performance Testing
Optimize system parameters (cache sizes, thread counts, buffer sizes, algorithms)

### Cooking/Baking Recipes
Optimize ingredient proportions and cooking parameters (temperature, time, ingredient ratios)

### Scientific Experiments
Design experiments with multiple controlled variables efficiently

## Language Bindings

Pre-built examples for:
- **Python**: ctypes interface with comprehensive examples
- **Node.js**: ffi-napi interface with callback patterns
- **Other Languages**: Any language with C FFI support

## Build and Installation

### Prerequisites
- C99 compiler (GCC, Clang, etc.)
- Make
- Standard POSIX libraries

### Building
```bash
# Build library and CLI
make all

# Build just the library
make lib

# Build just the CLI
make cli

# Run tests
make test

# Memory analysis
make check
```

### Installation
```bash
# Install to system (requires root)
sudo make install

# Install to custom prefix
make install PREFIX=$HOME/.local

# Reinstall (uninstall then install)
sudo make reinstall

# Uninstall
sudo make uninstall
```

After installation:
- Library: `/usr/local/lib/libtaguchi.so` and `libtaguchi.a`
- Header: `/usr/local/include/taguchi.h`
- CLI: `/usr/local/bin/taguchi`

### Project Structure
```
taguchi/
├── build/          # All build artifacts (objects, libs, binaries)
├── docs/           # Documentation and reference materials
├── examples/       # Example code and usage patterns
├── include/        # Public API headers
├── src/
│   ├── lib/        # Core library implementation
│   └── cli/        # Command-line interface
└── tests/          # Unit and integration tests
```

## License
Public Domain (CC0) - no restrictions on use, modification, or distribution.