# Taguchi Array Tool - Complete Project Documentation

## Project Overview

The Taguchi Array Tool is a robust, Unix-philosophy command-line tool for designing and analyzing experiments using Taguchi orthogonal arrays. The tool is small, fast, composable, and follows strict POSIX conventions. The core is built as a shared library to enable bindings for Python, Node.js, and other languages.

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

- **Orthogonal Array Support**: L4, L8, L9, L16, L27, L81, L243 (GF(3) algorithmic generation)
- **Column Pairing**: Multi-level factors (4-27 levels) via automatic column pairing/tripling
- **Mixed-Level Support**: Factors with different level counts in the same experiment
- **Auto Array Selection**: Automatically picks the smallest suitable array when not specified
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

## Current Implementation Status

### ✅ Complete Modules
1. **Infrastructure** - Memory management, error handling, utilities (utils.c/h)
2. **Arrays** - Orthogonal arrays (L4, L8, L9, L16, L27, L81, L243) with mathematical verification (arrays.c/h)
3. **Parser** - YAML-like `.tgu` file parsing and validation (parser.c/h)
4. **Generator** - Mapping factor definitions to experimental runs (generator.c/h)
5. **Serializer** - JSON serialization for language bindings (serializer.c/h)
6. **Analyzer** - Statistical analysis and main effects calculation (analyzer.c/h)
7. **Public API** - Complete facade connecting all modules (taguchi.c/h)
8. **CLI** - Command-line interface with generate, run, analyze, effects, validate, list-arrays commands (main.c)
9. **API Extensions** - Enhanced functions for enumeration, auto-selection, and analysis

### 🔧 Key Capabilities
- **Column Pairing**: Factors with 4-9 levels use 2 paired columns; 10-27 levels use 3 columns
- **Mixed-Level Designs**: Factors with fewer levels than base use modular wrapping
- **GF(3) Array Generation**: L27, L81, L243 generated algorithmically for guaranteed orthogonality
- **Full Analysis Pipeline**: Main effects calculation, optimal level recommendation, JSON export

## Quick Start

### Building the Library
```bash
make lib          # Build shared and static libraries
make cli          # Build command-line tool
make test         # Run comprehensive test suite
```

### Basic Usage
```bash
# Create experiment definition (experiment.tgu)
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30, 60, 120
# array: L9  (optional - will auto-select appropriate array)

# Generate runs (will auto-select L9 for 3 factors with 3 levels each)
./taguchi generate experiment.tgu

# Execute with external script (sets environment variables for each run)
./taguchi run experiment.tgu './test_script.sh'

# List available arrays
./taguchi list-arrays

# Validate definition
./taguchi validate experiment.tgu
```

### Programmatic Usage (C)
```c
#include <taguchi.h>
#include <stdio.h>

int main() {
    char error[TAGUCHI_ERROR_SIZE];

    // Parse experiment definition
    const char *content = 
        "factors:\\n"
        "  cache_size: 64M, 128M, 256M\\n" 
        "  threads: 2, 4, 8\\n"
        "# array: L9  (optional)\\n";

    taguchi_experiment_def_t *def = taguchi_parse_definition(content, error);
    if (!def) {
        fprintf(stderr, "Error: %s\\n", error);
        return 1;
    }

    // Generate runs (auto-selects appropriate array)
    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;

    if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
        fprintf(stderr, "Generate error: %s\\n", error);
        taguchi_free_definition(def);
        return 1;
    }

    printf("Generated %zu runs:\\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("Run %zu:\\n", taguchi_run_get_id(runs[i]));
        // Access factor values...
    }

    // Cleanup
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
    return 0;
}
```

## File Format (.tgu)

The `.tgu` format is YAML-like for experiment definitions:

```yaml
factors:
  cache_size: 64M, 128M, 256M    # Factor with 3 levels
  threads: 2, 4, 8              # Another factor with 3 levels  
  timeout: 30s, 60s, 120s       # Third factor with 3 levels
array: L9                        # Optional - auto-selected if omitted
```

## API Functions

### Definition Management
```c
taguchi_experiment_def_t *taguchi_parse_definition(const char *content, char *error_buf);
int taguchi_add_factor(taguchi_experiment_def_t *def, const char *name, const char **levels, size_t level_count, char *error_buf);
bool taguchi_validate_definition(const taguchi_experiment_def_t *def, char *error_buf);
void taguchi_free_definition(taguchi_experiment_def_t *def);
```

### Run Generation
```c
int taguchi_generate_runs(const taguchi_experiment_def_t *def, taguchi_experiment_run_t ***runs_out, size_t *count_out, char *error_buf);
size_t taguchi_run_get_id(const taguchi_experiment_run_t *run);
const char *taguchi_run_get_value(const taguchi_experiment_run_t *run, const char *factor_name);
size_t taguchi_run_get_factor_count(const taguchi_experiment_run_t *run);
const char *taguchi_run_get_factor_name_at_index(const taguchi_experiment_run_t *run, size_t index);
void taguchi_free_runs(taguchi_experiment_run_t **runs, size_t count);
```

### Array Information
```c
const char **taguchi_list_arrays(void);
const char *taguchi_suggest_optimal_array(const taguchi_experiment_def_t *def, char *error_buf);
int taguchi_get_array_info(const char *name, size_t *rows_out, size_t *cols_out, size_t *levels_out);
```

### Results Collection
```c
taguchi_result_set_t *taguchi_create_result_set(const taguchi_experiment_def_t *def, const char *metric_name);
int taguchi_add_result(taguchi_result_set_t *results, size_t run_id, double response_value, char *error_buf);
void taguchi_free_result_set(taguchi_result_set_t *results);
```

### Analysis
```c
int taguchi_calculate_main_effects(const taguchi_result_set_t *results, taguchi_main_effect_t ***effects_out, size_t *count_out, char *error_buf);
const char *taguchi_effect_get_factor(const taguchi_main_effect_t *effect);
const double *taguchi_effect_get_level_means(const taguchi_main_effect_t *effect, size_t *level_count_out);
double taguchi_effect_get_range(const taguchi_main_effect_t *effect);
int taguchi_recommend_optimal(const taguchi_main_effect_t **effects, size_t effect_count, bool higher_is_better, char *recommendation_buf, size_t buf_size);
void taguchi_free_effects(taguchi_main_effect_t **effects, size_t count);
```

## Command-Line Interface

The CLI tool provides Unix-style commands:

- `taguchi list-arrays` - List available orthogonal arrays with details (rows, columns, levels)
- `taguchi generate <file.tgu>` - Generate experiment runs from definition
- `taguchi run <file.tgu> <script.sh>` - Execute external script for each run with environment variables
- `taguchi analyze <file.tgu> <results.csv> [--metric name] [--minimize]` - Full analysis with recommendations
- `taguchi effects <file.tgu> <results.csv> [--metric name]` - Calculate and display main effects
- `taguchi validate <file.tgu>` - Validate experiment definition
- `taguchi --help` - Show help
- `taguchi --version` - Show version

### Environment Variables During Run Execution

When using `taguchi run`, each experiment execution receives environment variables:
- `TAGUCHI_FACTOR_NAME=value` for each factor
- `TAGUCHI_RUN_ID=run_number` for the experiment ID

## Quality Assurance

- **Memory Safety**: All code passes valgrind with zero leaks/errors
- **Static Analysis**: Clean with `-Wall -Wextra -Werror -std=c99 -pedantic`  
- **Testing**: Complete test suite with unit and integration tests
- **API Consistency**: Stable, well-documented public interface
- **Modular Design**: Clean separation between components
- **Error Handling**: Comprehensive error reporting with context

## Language Bindings

The C library is designed for easy FFI integration with other languages:

### Python Example
```python
import ctypes
lib = ctypes.CDLL('./libtaguchi.so')  # Load the shared library
# Use ctypes to interface with C functions
```

### Node.js Example  
```javascript
const ffi = require('ffi-napi');
const taguchiLib = ffi.Library('./libtaguchi', {
  'taguchi_list_arrays': ['pointer', []],
  'taguchi_parse_definition': ['pointer', ['string', 'pointer']]
  // Define other functions as needed
});
```

## Architecture

```
┌─────────────────┐
│   Public API    │  taguchi.h - Stable interface
└─────────────────┘
         │
┌─────────────────┐
│  Library Facade │  taguchi.c - Bridge between public API and modules
└─────────────────┘
         │
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│    Generator    │  │     Parser      │  │     Arrays      │
│  (generate.c)   │←→│   (parser.c)    │←→│   (arrays.c)    │
└─────────────────┘  └─────────────────┘  └─────────────────┘
         │                   │                   │
    Runs Generation    Definition parsing    Array storage/
         ↓                   ↓                lookup
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Serializer    │  │    Analyzer     │  │     Utils       │
│ (serialize.c)   │  │  (analyzer.c)   │  │   (utils.c)     │
└─────────────────┘  └─────────────────┘  └─────────────────┘
     JSON output       Statistical          Memory/string
     for bindings      analysis            utilities
```

## Design Principles

1. **Do One Thing Well**: Focus solely on Taguchi experiment design/generation
2. **Compose with Others**: Generate input for other tools, don't solve everything
3. **Small & Fast**: Minimal memory footprint, fast execution
4. **Unix Philosophy**: Text streams, pipe-friendly, composable
5. **Library First**: Core logic as reusable library with CLI wrapper
6. **Safe Memory**: No leaks, proper error handling, bounds checking
7. **Orthogonal Design**: Clean module separation with well-defined interfaces

## Mathematical Basis

The library implements mathematically correct orthogonal arrays:

**2-Level Arrays (hardcoded)**:
- **L4**: 4 runs, 3 columns, 2 levels
- **L8**: 8 runs, 7 columns, 2 levels
- **L16**: 16 runs, 15 columns, 2 levels

**3-Level Arrays (GF(3) generated)**:
- **L9**: 9 runs, 4 columns, 3 levels
- **L27**: 27 runs, 13 columns, 3 levels (GF(3)^3)
- **L81**: 81 runs, 40 columns, 3 levels (GF(3)^4)
- **L243**: 243 runs, 121 columns, 3 levels (GF(3)^5)

L27, L81, and L243 are generated algorithmically using linear combinations over GF(3).
Rows are all n-tuples in {0,1,2}^n; columns are inner products with canonical vectors
(one representative from each {v, 2v} pair). Unit vectors are placed first to guarantee
linear independence for multi-column factor assignments.

**Column Pairing**: Factors with more levels than the base use multiple columns:
- 1-3 levels: 1 column (base-3 array)
- 4-9 levels: 2 columns paired (3^2 = 9 slots)
- 10-27 levels: 3 columns tripled (3^3 = 27 slots)

Each array maintains orthogonality properties where every pair of columns contains all
possible level combinations with equal frequency.

## Usage Scenarios

### Software Performance Testing
Optimize system parameters affecting performance: CPU affinity, cache settings, I/O scheduling, etc.

### Manufacturing Process Optimization  
Optimize production parameters: temperature, pressure, timing, material composition, etc.

### Scientific Experiments
Design controlled experiments with multiple variables efficiently.

### A/B Testing Configuration
Optimize experimental designs for comparing different treatments.

### Cooking/Baking Optimization
Find optimal combinations of ingredients and cooking parameters.

## Performance Characteristics

- **Fast**: Near instantaneous run generation (milliseconds)
- **Lightweight**: Small memory footprint (~100KB shared library)
- **Efficient**: Direct memory access, no unnecessary copying
- **Scalable**: Handles all supported array sizes efficiently

## Build and Installation

### Prerequisites
- C99 compiler (gcc, clang, etc.)
- GNU make
- POSIX libc

### Building
```bash
make lib           # Build shared and static libraries
make cli           # Build command-line tool
make test          # Run test suite
make clean         # Clean build artifacts
```

### Installation
```bash
sudo make install  # Install to /usr/local
```

## Integration Examples

### With Shell Scripts
```bash
# Generate runs and save to CSV
taguchi generate experiment.tgu > runs.csv

# Process runs with custom script
while read -r run_line; do
  # Extract parameters and run experiments
  eval "$(echo "$run_line" | awk '{...}' | xargs -I {} sh -c 'export {}')"
  ./my_experiment.sh
done < runs.csv
```

### With Build Systems
```bash
# Include in Makefiles easily
EXPERIMENTS := $(shell taguchi generate experiment.tgu | wc -l)
for i in $(seq 1 $EXPERIMENTS); do
  # Run each experiment configuration
done
```

## License
Public Domain (CC0) - no restrictions on use, modification, or distribution.