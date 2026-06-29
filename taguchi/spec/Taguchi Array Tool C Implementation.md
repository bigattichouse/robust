# Taguchi Array Tool - C Implementation Blueprint

## Project Overview

### Mission Statement
Build a robust, Unix-philosophy command-line tool for designing and analyzing experiments using Taguchi orthogonal arrays. The tool will be small, fast, composable, and follow strict POSIX conventions. The core will be built as a shared library to enable bindings for Python, Node.js, and other languages.

### Design Principles
1. **Do One Thing Well**: Focus solely on Taguchi experiment design and analysis
2. **Text Streams**: All I/O via stdin/stdout with explicit file options
3. **Composable**: Plays well with pipes, shell scripts, and other Unix tools
4. **Zero Dependencies**: Pure C99 with only POSIX libc
5. **Fail Fast**: Clear error messages, non-zero exit codes on failure
6. **Memory Safe**: No leaks, bounds checking, safe string handling
7. **Testable**: Every module has comprehensive unit tests
8. **Library First**: Core logic as shared library for maximum reusability

### Success Criteria
- Compiles with `-Wall -Wextra -Werror -std=c99 -pedantic`
- Passes valgrind with zero leaks/errors
- 90%+ test coverage
- Shared library <300KB, CLI binary <200KB (stripped)
- Processes 10K+ experiments/second
- Clean separation between library and CLI
- Working Python and Node.js bindings

---

## Architecture

### Dual Build Strategy

The project will build both:
1. **Shared Library** (`libtaguchi.so` / `libtaguchi.dylib` / `taguchi.dll`) - Core logic for language bindings
2. **Static Library** (`libtaguchi.a`) - For static linking if needed
3. **CLI Tool** (`taguchi`) - Command-line interface that links against the shared library

This allows:
- Python bindings via ctypes/cffi
- Node.js bindings via node-ffi or N-API
- Go bindings via cgo
- Rust bindings via bindgen
- Any language with C FFI support

### Module Breakdown

```
taguchi/
├── src/
│   ├── lib/                   # Shared library core (no I/O dependencies)
│   │   ├── taguchi.c/h        # Public API facade
│   │   ├── parser.c/h         # .tgu file parsing
│   │   ├── generator.c/h      # Orthogonal array generation
│   │   ├── arrays.c/h         # Predefined array definitions
│   │   ├── analyzer.c/h       # Statistical analysis
│   │   ├── serializer.c/h     # JSON serialization
│   │   └── utils.c/h          # String handling, memory
│   ├── cli/                   # CLI-specific code
│   │   ├── main.c             # CLI entry point
│   │   ├── commands.c/h       # Command dispatch
│   │   ├── runner.c/h         # Experiment execution (fork/exec)
│   │   └── output.c/h         # CLI formatting
│   └── config.h               # Build-time configuration
├── include/
│   └── taguchi.h              # Public library API header
├── bindings/                  # Language bindings
│   ├── python/
│   │   ├── taguchi.py         # Python wrapper
│   │   ├── setup.py
│   │   └── test_taguchi.py
│   ├── node/
│   │   ├── index.js           # Node.js wrapper
│   │   ├── package.json
│   │   └── test.js
│   └── examples/
│       ├── example.py
│       └── example.js
├── tests/
│   ├── test_parser.c
│   ├── test_generator.c
│   ├── test_arrays.c
│   ├── test_analyzer.c
│   ├── test_serializer.c
│   ├── test_utils.c
│   ├── test_integration.c
│   ├── test_framework.h
│   └── test_runner.c          # Test harness main
├── examples/
│   ├── webserver.tgu
│   ├── compiler.tgu
│   └── benchmark.sh
├── docs/
│   ├── API.md
│   └── DESIGN.md
├── Makefile
├── taguchi.pc.in              # pkg-config template
└── README.md
```

---

## Data Structures

### Core Types (Internal - src/lib/)

```c
/* config.h - Build configuration */
#define MAX_FACTORS 16
#define MAX_LEVELS 5
#define MAX_FACTOR_NAME 64
#define MAX_LEVEL_VALUE 128
#define MAX_EXPERIMENTS 512
#define BUFFER_SIZE 4096

/* parser.h - Experiment definition (internal) */
typedef struct {
    char name[MAX_FACTOR_NAME];
    char values[MAX_LEVELS][MAX_LEVEL_VALUE];
    size_t level_count;
} Factor;

typedef struct {
    Factor factors[MAX_FACTORS];
    size_t factor_count;
    char array_type[8];  /* "L4", "L9", "L16", "L27", etc. */
} ExperimentDef;

/* arrays.h - Orthogonal array structure (internal) */
typedef struct {
    const char *name;
    size_t rows;      /* Number of experiments */
    size_t cols;      /* Number of factors */
    size_t levels;    /* Levels per factor (2 or 3) */
    const int *data;  /* Row-major array */
} OrthogonalArray;

/* generator.h - Generated experiment run (internal) */
typedef struct {
    size_t run_id;
    char values[MAX_FACTORS][MAX_LEVEL_VALUE];
    size_t factor_count;
    char factor_names[MAX_FACTORS][MAX_FACTOR_NAME];
} ExperimentRun;

/* analyzer.h - Results and statistics (internal) */
typedef struct {
    char factor_name[MAX_FACTOR_NAME];
    double *level_means;  /* Dynamic array of means per level */
    size_t level_count;
    double range;         /* Max - Min */
} MainEffect;

typedef struct {
    ExperimentRun *runs;
    double *responses;    /* Metric values per run */
    size_t run_count;
    char metric_name[MAX_FACTOR_NAME];
} ResultSet;
```

---

## Public Library API

### Design Principles for FFI
1. **C89 ABI compatibility**: Stable across compilers and platforms
2. **No callbacks**: Simplifies FFI (callbacks are difficult in some languages)
3. **Opaque pointers**: Hide implementation details, stable ABI
4. **Explicit memory management**: Caller-controlled allocation/deallocation
5. **Return codes**: -1 for error, 0 for success (no exceptions)
6. **Thread-safe**: No global mutable state
7. **Const correctness**: Clear ownership semantics

### Public API Header (`include/taguchi.h`)

```c
#ifndef TAGUCHI_H
#define TAGUCHI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/* Library version */
#define TAGUCHI_VERSION_MAJOR 1
#define TAGUCHI_VERSION_MINOR 0
#define TAGUCHI_VERSION_PATCH 0

/* Error buffer size */
#define TAGUCHI_ERROR_SIZE 256

/* Opaque handles - never exposed to user */
typedef struct taguchi_experiment_def taguchi_experiment_def_t;
typedef struct taguchi_experiment_run taguchi_experiment_run_t;
typedef struct taguchi_result_set taguchi_result_set_t;
typedef struct taguchi_main_effect taguchi_main_effect_t;

/*
 * ============================================================================
 * Experiment Definition API
 * ============================================================================
 */

/**
 * Create experiment definition from .tgu file content (string).
 * 
 * @param content  .tgu file content as string
 * @param error_buf Buffer for error message (size TAGUCHI_ERROR_SIZE)
 * @return Experiment definition handle, or NULL on error
 */
taguchi_experiment_def_t *taguchi_parse_definition(
    const char *content,
    char *error_buf
);

/**
 * Create experiment definition programmatically.
 * 
 * @param array_type Array type (e.g., "L4", "L9", "L16")
 * @return Experiment definition handle, or NULL on error
 */
taguchi_experiment_def_t *taguchi_create_definition(const char *array_type);

/**
 * Add factor to definition.
 * 
 * @param def Experiment definition
 * @param name Factor name
 * @param levels Array of level values (strings)
 * @param level_count Number of levels
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_add_factor(
    taguchi_experiment_def_t *def,
    const char *name,
    const char **levels,
    size_t level_count,
    char *error_buf
);

/**
 * Validate experiment definition.
 * 
 * @param def Experiment definition
 * @param error_buf Buffer for error message
 * @return true if valid, false otherwise
 */
bool taguchi_validate_definition(
    const taguchi_experiment_def_t *def,
    char *error_buf
);

/**
 * Free experiment definition.
 * 
 * @param def Experiment definition to free
 */
void taguchi_free_definition(taguchi_experiment_def_t *def);

/*
 * ============================================================================
 * Array Information API
 * ============================================================================
 */

/**
 * Get available array names.
 * 
 * @return NULL-terminated array of array names (do not free)
 */
const char **taguchi_list_arrays(void);

/**
 * Get array information.
 * 
 * @param name Array name (e.g., "L9")
 * @param rows_out Output: number of runs
 * @param cols_out Output: number of factors
 * @param levels_out Output: number of levels
 * @return 0 on success, -1 if array not found
 */
int taguchi_get_array_info(
    const char *name,
    size_t *rows_out,
    size_t *cols_out,
    size_t *levels_out
);

/*
 * ============================================================================
 * Generation API
 * ============================================================================
 */

/**
 * Generate experiment runs from definition.
 * 
 * @param def Experiment definition
 * @param runs_out Output: array of run pointers (caller must free)
 * @param count_out Output: number of runs
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_generate_runs(
    const taguchi_experiment_def_t *def,
    taguchi_experiment_run_t ***runs_out,
    size_t *count_out,
    char *error_buf
);

/**
 * Get run configuration value by factor name.
 * 
 * @param run Experiment run
 * @param factor_name Factor name
 * @return Factor value, or NULL if not found (do not free)
 */
const char *taguchi_run_get_value(
    const taguchi_experiment_run_t *run,
    const char *factor_name
);

/**
 * Get run ID.
 * 
 * @param run Experiment run
 * @return Run ID (1-indexed)
 */
size_t taguchi_run_get_id(const taguchi_experiment_run_t *run);

/**
 * Get all factor names in run.
 * 
 * @param run Experiment run
 * @return NULL-terminated array of factor names (do not free)
 */
const char **taguchi_run_get_factor_names(const taguchi_experiment_run_t *run);

/**
 * Free experiment runs.
 * 
 * @param runs Array of run pointers
 * @param count Number of runs
 */
void taguchi_free_runs(taguchi_experiment_run_t **runs, size_t count);

/*
 * ============================================================================
 * Results API
 * ============================================================================
 */

/**
 * Create result set for collecting experimental data.
 * 
 * @param def Experiment definition
 * @param metric_name Name of metric being measured
 * @return Result set handle, or NULL on error
 */
taguchi_result_set_t *taguchi_create_result_set(
    const taguchi_experiment_def_t *def,
    const char *metric_name
);

/**
 * Add result to set.
 * 
 * @param results Result set
 * @param run_id Run ID (from taguchi_run_get_id)
 * @param response_value Measured response value
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_add_result(
    taguchi_result_set_t *results,
    size_t run_id,
    double response_value,
    char *error_buf
);

/**
 * Free result set.
 * 
 * @param results Result set to free
 */
void taguchi_free_result_set(taguchi_result_set_t *results);

/*
 * ============================================================================
 * Analysis API
 * ============================================================================
 */

/**
 * Calculate main effects from results.
 * 
 * @param results Result set
 * @param effects_out Output: array of effect pointers (caller must free)
 * @param count_out Output: number of effects
 * @param error_buf Buffer for error message
 * @return 0 on success, -1 on error
 */
int taguchi_calculate_main_effects(
    const taguchi_result_set_t *results,
    taguchi_main_effect_t ***effects_out,
    size_t *count_out,
    char *error_buf
);

/**
 * Get effect factor name.
 * 
 * @param effect Main effect
 * @return Factor name (do not free)
 */
const char *taguchi_effect_get_factor(const taguchi_main_effect_t *effect);

/**
 * Get level means from effect.
 * 
 * @param effect Main effect
 * @param level_count_out Output: number of levels
 * @return Array of level means (do not free)
 */
const double *taguchi_effect_get_level_means(
    const taguchi_main_effect_t *effect,
    size_t *level_count_out
);

/**
 * Get effect range (max - min).
 * 
 * @param effect Main effect
 * @return Range value
 */
double taguchi_effect_get_range(const taguchi_main_effect_t *effect);

/**
 * Free main effects.
 * 
 * @param effects Array of effect pointers
 * @param count Number of effects
 */
void taguchi_free_effects(taguchi_main_effect_t **effects, size_t count);

/**
 * Recommend optimal configuration based on effects.
 * 
 * @param effects Array of effect pointers
 * @param effect_count Number of effects
 * @param higher_is_better True if maximizing metric
 * @param recommendation_buf Output buffer for recommendation
 * @param buf_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int taguchi_recommend_optimal(
    const taguchi_main_effect_t **effects,
    size_t effect_count,
    bool higher_is_better,
    char *recommendation_buf,
    size_t buf_size
);

/*
 * ============================================================================
 * Serialization API (for language bindings)
 * ============================================================================
 */

/**
 * Serialize runs to JSON string.
 * 
 * @param runs Array of run pointers
 * @param count Number of runs
 * @return JSON string (caller must free with taguchi_free_string)
 */
char *taguchi_runs_to_json(
    const taguchi_experiment_run_t **runs,
    size_t count
);

/**
 * Serialize effects to JSON string.
 * 
 * @param effects Array of effect pointers
 * @param count Number of effects
 * @return JSON string (caller must free with taguchi_free_string)
 */
char *taguchi_effects_to_json(
    const taguchi_main_effect_t **effects,
    size_t count
);

/**
 * Free string allocated by library.
 * 
 * @param str String to free
 */
void taguchi_free_string(char *str);

#ifdef __cplusplus
}
#endif

#endif /* TAGUCHI_H */
```

### C Usage Example

```c
#include <taguchi.h>
#include <stdio.h>

int main(void) {
    char error[TAGUCHI_ERROR_SIZE];
    
    /* Parse definition */
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
    
    /* Generate runs */
    taguchi_experiment_run_t **runs = NULL;
    size_t count = 0;
    
    if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
        fprintf(stderr, "Generate error: %s\n", error);
        taguchi_free_definition(def);
        return 1;
    }
    
    /* Print runs */
    printf("Generated %zu runs:\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("Run %zu: cache_size=%s, threads=%s\n",
               taguchi_run_get_id(runs[i]),
               taguchi_run_get_value(runs[i], "cache_size"),
               taguchi_run_get_value(runs[i], "threads"));
    }
    
    /* Create results and add data */
    taguchi_result_set_t *results = taguchi_create_result_set(def, "throughput");
    taguchi_add_result(results, 1, 1250.5, error);
    taguchi_add_result(results, 2, 2100.3, error);
    taguchi_add_result(results, 3, 2850.7, error);
    /* ... add remaining results ... */
    
    /* Analyze */
    taguchi_main_effect_t **effects = NULL;
    size_t effect_count = 0;
    
    if (taguchi_calculate_main_effects(results, &effects, &effect_count, error) == 0) {
        printf("\nMain Effects:\n");
        for (size_t i = 0; i < effect_count; i++) {
            printf("  %s: range=%.2f\n",
                   taguchi_effect_get_factor(effects[i]),
                   taguchi_effect_get_range(effects[i]));
        }
        
        /* Get recommendation */
        char rec[512];
        taguchi_recommend_optimal(
            (const taguchi_main_effect_t **)effects,
            effect_count,
            true,
            rec,
            sizeof(rec)
        );
        printf("\nRecommendation: %s\n", rec);
        
        taguchi_free_effects(effects, effect_count);
    }
    
    /* Cleanup */
    taguchi_free_result_set(results);
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
    
    return 0;
}
```

---

## Language Bindings

### Python Binding (`bindings/python/taguchi.py`)

```python
"""
Python bindings for libtaguchi using ctypes.
"""

import ctypes
import json
import os
from typing import List, Dict, Optional, Tuple

# Locate library
def _find_library():
    """Find libtaguchi in standard locations."""
    lib_names = ['libtaguchi.so', 'libtaguchi.dylib', 'taguchi.dll']
    search_paths = [
        '.',
        '/usr/local/lib',
        '/usr/lib',
        os.path.join(os.path.dirname(__file__), '../../'),
    ]
    
    for path in search_paths:
        for name in lib_names:
            full_path = os.path.join(path, name)
            if os.path.exists(full_path):
                return full_path
    
    # Try system default
    for name in lib_names:
        try:
            return ctypes.util.find_library(name.replace('lib', '').replace('.so', ''))
        except:
            pass
    
    raise RuntimeError("Could not find libtaguchi")

# Load library
_lib = ctypes.CDLL(_find_library())

# Configure function signatures
_lib.taguchi_parse_definition.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
_lib.taguchi_parse_definition.restype = ctypes.c_void_p

_lib.taguchi_free_definition.argtypes = [ctypes.c_void_p]
_lib.taguchi_free_definition.restype = None

_lib.taguchi_generate_runs.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.c_char_p
]
_lib.taguchi_generate_runs.restype = ctypes.c_int

_lib.taguchi_runs_to_json.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.taguchi_runs_to_json.restype = ctypes.c_char_p

_lib.taguchi_free_runs.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.taguchi_free_runs.restype = None

_lib.taguchi_free_string.argtypes = [ctypes.c_char_p]
_lib.taguchi_free_string.restype = None

_lib.taguchi_create_result_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.taguchi_create_result_set.restype = ctypes.c_void_p

_lib.taguchi_add_result.argtypes = [
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_double,
    ctypes.c_char_p
]
_lib.taguchi_add_result.restype = ctypes.c_int

_lib.taguchi_calculate_main_effects.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.c_char_p
]
_lib.taguchi_calculate_main_effects.restype = ctypes.c_int

_lib.taguchi_effects_to_json.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.taguchi_effects_to_json.restype = ctypes.c_char_p

_lib.taguchi_free_effects.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.taguchi_free_effects.restype = None

_lib.taguchi_free_result_set.argtypes = [ctypes.c_void_p]
_lib.taguchi_free_result_set.restype = None

_lib.taguchi_list_arrays.argtypes = []
_lib.taguchi_list_arrays.restype = ctypes.POINTER(ctypes.c_char_p)


class TaguchiError(Exception):
    """Exception raised for Taguchi library errors."""
    pass


class ExperimentDef:
    """Taguchi experiment definition."""
    
    def __init__(self, tgu_content: str):
        """
        Create experiment definition from .tgu content.
        
        Args:
            tgu_content: Content of .tgu file as string
            
        Raises:
            TaguchiError: If parsing fails
        """
        error_buf = ctypes.create_string_buffer(256)
        self._handle = _lib.taguchi_parse_definition(
            tgu_content.encode('utf-8'),
            error_buf
        )
        if not self._handle:
            raise TaguchiError(error_buf.value.decode('utf-8'))
    
    def generate_runs(self) -> List[Dict[str, str]]:
        """
        Generate experiment runs.
        
        Returns:
            List of run dictionaries with run_id and factor values
            
        Raises:
            TaguchiError: If generation fails
        """
        runs_ptr = ctypes.c_void_p()
        count = ctypes.c_size_t()
        error_buf = ctypes.create_string_buffer(256)
        
        result = _lib.taguchi_generate_runs(
            self._handle,
            ctypes.byref(runs_ptr),
            ctypes.byref(count),
            error_buf
        )
        
        if result != 0:
            raise TaguchiError(error_buf.value.decode('utf-8'))
        
        # Serialize to JSON for easy handling
        json_str = _lib.taguchi_runs_to_json(runs_ptr, count)
        runs = json.loads(json_str.decode('utf-8'))
        
        _lib.taguchi_free_string(json_str)
        _lib.taguchi_free_runs(runs_ptr, count)
        
        return runs
    
    def __del__(self):
        """Free experiment definition."""
        if hasattr(self, '_handle') and self._handle:
            _lib.taguchi_free_definition(self._handle)


class ResultSet:
    """Container for experiment results."""
    
    def __init__(self, experiment_def: ExperimentDef, metric_name: str):
        """
        Create result set.
        
        Args:
            experiment_def: Experiment definition
            metric_name: Name of metric being measured
        """
        self._def = experiment_def
        self._handle = _lib.taguchi_create_result_set(
            experiment_def._handle,
            metric_name.encode('utf-8')
        )
        if not self._handle:
            raise TaguchiError("Failed to create result set")
    
    def add_result(self, run_id: int, value: float):
        """
        Add a result.
        
        Args:
            run_id: Run ID (from run dictionary)
            value: Measured response value
            
        Raises:
            TaguchiError: If adding result fails
        """
        error_buf = ctypes.create_string_buffer(256)
        result = _lib.taguchi_add_result(
            self._handle,
            run_id,
            value,
            error_buf
        )
        if result != 0:
            raise TaguchiError(error_buf.value.decode('utf-8'))
    
    def calculate_main_effects(self) -> List[Dict]:
        """
        Calculate main effects.
        
        Returns:
            List of effect dictionaries with factor, level_means, range
            
        Raises:
            TaguchiError: If calculation fails
        """
        effects_ptr = ctypes.c_void_p()
        count = ctypes.c_size_t()
        error_buf = ctypes.create_string_buffer(256)
        
        result = _lib.taguchi_calculate_main_effects(
            self._handle,
            ctypes.byref(effects_ptr),
            ctypes.byref(count),
            error_buf
        )
        
        if result != 0:
            raise TaguchiError(error_buf.value.decode('utf-8'))
        
        # Serialize to JSON
        json_str = _lib.taguchi_effects_to_json(effects_ptr, count)
        effects = json.loads(json_str.decode('utf-8'))
        
        _lib.taguchi_free_string(json_str)
        _lib.taguchi_free_effects(effects_ptr, count)
        
        return effects
    
    def __del__(self):
        """Free result set."""
        if hasattr(self, '_handle') and self._handle:
            _lib.taguchi_free_result_set(self._handle)


def list_arrays() -> List[str]:
    """
    Get list of available orthogonal arrays.
    
    Returns:
        List of array names (e.g., ['L4', 'L9', 'L16'])
    """
    ptr = _lib.taguchi_list_arrays()
    arrays = []
    i = 0
    while ptr[i]:
        arrays.append(ptr[i].decode('utf-8'))
        i += 1
    return arrays


# Example usage
if __name__ == '__main__':
    tgu = """
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
array: L9
"""
    
    # Create experiment
    exp = ExperimentDef(tgu)
    runs = exp.generate_runs()
    
    print(f"Generated {len(runs)} runs:")
    for run in runs:
        print(f"  Run {run['run_id']}: {run}")
    
    # Collect results (normally from actual experiments)
    results = ResultSet(exp, "throughput")
    for i, run in enumerate(runs):
        # Simulate experiment results
        value = 1000 + i * 100 + run['run_id'] * 50
        results.add_result(run['run_id'], value)
    
    # Analyze
    effects = results.calculate_main_effects()
    print("\nMain Effects:")
    for effect in effects:
        print(f"  {effect['factor']}: range={effect['range']:.2f}")
```

### Node.js Binding (`bindings/node/index.js`)

```javascript
/**
 * Node.js bindings for libtaguchi using ffi-napi.
 */

const ffi = require('ffi-napi');
const ref = require('ref-napi');
const path = require('path');

// Load library
const libPath = process.platform === 'darwin' 
  ? 'libtaguchi.dylib'
  : process.platform === 'win32'
  ? 'taguchi.dll'
  : 'libtaguchi.so';

const lib = ffi.Library(libPath, {
  'taguchi_parse_definition': ['pointer', ['string', 'char *']],
  'taguchi_free_definition': ['void', ['pointer']],
  'taguchi_generate_runs': ['int', ['pointer', 'pointer', 'pointer', 'char *']],
  'taguchi_runs_to_json': ['string', ['pointer', 'size_t']],
  'taguchi_free_runs': ['void', ['pointer', 'size_t']],
  'taguchi_free_string': ['void', ['string']],
  'taguchi_create_result_set': ['pointer', ['pointer', 'string']],
  'taguchi_add_result': ['int', ['pointer', 'size_t', 'double', 'char *']],
  'taguchi_calculate_main_effects': ['int', ['pointer', 'pointer', 'pointer', 'char *']],
  'taguchi_effects_to_json': ['string', ['pointer', 'size_t']],
  'taguchi_free_effects': ['void', ['pointer', 'size_t']],
  'taguchi_free_result_set': ['void', ['pointer']],
  'taguchi_list_arrays': ['pointer', []]
});

class TaguchiError extends Error {
  constructor(message) {
    super(message);
    this.name = 'TaguchiError';
  }
}

class ExperimentDef {
  constructor(tguContent) {
    const errorBuf = Buffer.alloc(256);
    this.handle = lib.taguchi_parse_definition(tguContent, errorBuf);
    
    if (this.handle.isNull()) {
      throw new TaguchiError(`Parse error: ${errorBuf.toString('utf8')}`);
    }
  }
  
  generateRuns() {
    const runsPtr = ref.alloc('pointer');
    const count = ref.alloc('size_t');
    const errorBuf = Buffer.alloc(256);
    
    const result = lib.taguchi_generate_runs(
      this.handle,
      runsPtr,
      count,
      errorBuf
    );
    
    if (result !== 0) {
      throw new TaguchiError(`Generate error: ${errorBuf.toString('utf8')}`);
    }
    
    const jsonStr = lib.taguchi_runs_to_json(
      runsPtr.deref(),
      count.deref()
    );
    
    const runs = JSON.parse(jsonStr);
    
    lib.taguchi_free_runs(runsPtr.deref(), count.deref());
    
    return runs;
  }
  
  destroy() {
    if (this.handle && !this.handle.isNull()) {
      lib.taguchi_free_definition(this.handle);
      this.handle = null;
    }
  }
}

class ResultSet {
  constructor(experimentDef, metricName) {
    this.def = experimentDef;
    this.handle = lib.taguchi_create_result_set(
      experimentDef.handle,
      metricName
    );
    
    if (this.handle.isNull()) {
      throw new TaguchiError('Failed to create result set');
    }
  }
  
  addResult(runId, value) {
    const errorBuf = Buffer.alloc(256);
    const result = lib.taguchi_add_result(
      this.handle,
      runId,
      value,
      errorBuf
    );
    
    if (result !== 0) {
      throw new TaguchiError(`Add result error: ${errorBuf.toString('utf8')}`);
    }
  }
  
  calculateMainEffects() {
    const effectsPtr = ref.alloc('pointer');
    const count = ref.alloc('size_t');
    const errorBuf = Buffer.alloc(256);
    
    const result = lib.taguchi_calculate_main_effects(
      this.handle,
      effectsPtr,
      count,
      errorBuf
    );
    
    if (result !== 0) {
      throw new TaguchiError(`Calculate error: ${errorBuf.toString('utf8')}`);
    }
    
    const jsonStr = lib.taguchi_effects_to_json(
      effectsPtr.deref(),
      count.deref()
    );
    
    const effects = JSON.parse(jsonStr);
    
    lib.taguchi_free_effects(effectsPtr.deref(), count.deref());
    
    return effects;
  }
  
  destroy() {
    if (this.handle && !this.handle.isNull()) {
      lib.taguchi_free_result_set(this.handle);
      this.handle = null;
    }
  }
}

function listArrays() {
  const ptr = lib.taguchi_list_arrays();
  const arrays = [];
  let i = 0;
  
  while (true) {
    const strPtr = ref.readPointer(ptr, i * ref.sizeof.pointer);
    if (strPtr.isNull()) break;
    arrays.push(ref.readCString(strPtr, 0));
    i++;
  }
  
  return arrays;
}

// Example usage
if (require.main === module) {
  const tgu = `
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
array: L9
`;

  const exp = new ExperimentDef(tgu);
  const runs = exp.generateRuns();
  
  console.log(`Generated ${runs.length} runs:`);
  runs.forEach(run => {
    console.log(`  Run ${run.run_id}:`, run);
  });
  
  const results = new ResultSet(exp, 'throughput');
  runs.forEach((run, i) => {
    const value = 1000 + i * 100 + run.run_id * 50;
    results.addResult(run.run_id, value);
  });
  
  const effects = results.calculateMainEffects();
  console.log('\nMain Effects:');
  effects.forEach(effect => {
    console.log(`  ${effect.factor}: range=${effect.range.toFixed(2)}`);
  });
  
  results.destroy();
  exp.destroy();
}

module.exports = { ExperimentDef, ResultSet, listArrays, TaguchiError };
```

---

## Module Specifications

### 1. `parser.c/h` - Configuration File Parser

**Purpose**: Parse `.tgu` files into `ExperimentDef` structures

**Functions**:
```c
/* Parse .tgu file content (string) */
int parse_experiment_def_from_string(
    const char *content,
    ExperimentDef *def,
    char *error_buf
);

/* Validate parsed definition */
bool validate_experiment_def(const ExperimentDef *def, char *error_buf);

/* Free any dynamically allocated parser resources */
void free_experiment_def(ExperimentDef *def);
```

**File Format** (Simple YAML-like):
```
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30, 60, 120
array: L9
```

**Error Handling**:
- Line numbers in error messages
- Clear diagnostic for malformed syntax
- Check for duplicate factor names
- Validate array type exists

**Testing**:
- Valid files with 2-level, 3-level, mixed factors
- Malformed syntax (missing colons, invalid arrays)
- Edge cases (empty file, max factors/levels)
- UTF-8 handling (accept, but factor names ASCII-only)

---

### 2. `arrays.c/h` - Orthogonal Array Definitions

**Purpose**: Provide predefined standard Taguchi arrays

**Data**:
```c
/* Predefined arrays (const static data) */
extern const OrthogonalArray L4_2_3;   /* 4 runs, 3 factors, 2 levels */
extern const OrthogonalArray L8_2_7;   /* 8 runs, 7 factors, 2 levels */
extern const OrthogonalArray L9_3_4;   /* 9 runs, 4 factors, 3 levels */
extern const OrthogonalArray L16_2_15; /* 16 runs, 15 factors, 2 levels */
extern const OrthogonalArray L27_3_13; /* 27 runs, 13 factors, 3 levels */

/* Lookup array by name */
const OrthogonalArray *get_array(const char *name);

/* List all available arrays (for public API) */
const char **list_array_names(void);
```

**Array Encoding**:
- Store as compact row-major int arrays
- Values are 0-indexed level indicators
- Example L4 encoding: `{0,0,0, 0,1,1, 1,0,1, 1,1,0}`

**Testing**:
- Verify orthogonality properties
- Check array dimensions match specifications
- Validate lookup by name

---

### 3. `generator.c/h` - Experiment Generator

**Purpose**: Map factor definitions to array runs

**Functions**:
```c
/* Generate experiments from definition */
int generate_experiments(
    const ExperimentDef *def,
    ExperimentRun **runs_out,
    size_t *count_out,
    char *error_buf
);

/* Free generated runs */
void free_experiments(ExperimentRun *runs, size_t count);

/* Check if factors fit in specified array */
bool check_array_compatibility(
    const ExperimentDef *def,
    const OrthogonalArray *array,
    char *error_buf
);
```

**Algorithm**:
1. Look up specified array (e.g., "L9")
2. Verify factor count ≤ array columns
3. Verify all factors have ≤ array level count
4. Map array indices to factor values
5. Generate `ExperimentRun` structures

**Error Handling**:
- Too many factors for array
- Level mismatch (3-level factor in 2-level array)
- Suggest appropriate array if current doesn't fit

**Testing**:
- Generate from valid definitions
- Detect incompatibilities
- Verify all factor combinations present
- Edge case: 1 factor, max factors

---

### 4. `serializer.c/h` - JSON Serialization

**Purpose**: Convert internal structures to JSON for language bindings

**Functions**:
```c
/* Serialize runs to JSON */
char *serialize_runs_to_json(const ExperimentRun *runs, size_t count);

/* Serialize effects to JSON */
char *serialize_effects_to_json(const MainEffect *effects, size_t count);

/* Free serialized string */
void free_serialized_string(char *str);
```

**JSON Format Examples**:

Runs:
```json
[
  {
    "run_id": 1,
    "cache_size": "64M",
    "threads": "2",
    "timeout": "30"
  },
  {
    "run_id": 2,
    "cache_size": "64M",
    "threads": "4",
    "timeout": "60"
  }
]
```

Effects:
```json
[
  {
    "factor": "cache_size",
    "level_means": [1250.5, 2100.3, 2850.7],
    "range": 1600.2
  },
  {
    "factor": "threads",
    "level_means": [1800.2, 2200.4, 2400.6],
    "range": 600.4
  }
]
```

**Testing**:
- Valid JSON output
- Proper escaping
- Round-trip with JSON parser
- Large datasets

---

### 5. `analyzer.c/h` - Statistical Analysis

**Purpose**: Calculate main effects, interactions, S/N ratios

**Functions**:
```c
/* Calculate main effects for a metric */
int calculate_main_effects_internal(
    const ResultSet *results,
    const ExperimentDef *def,
    MainEffect **effects_out,
    size_t *count_out,
    char *error_buf
);

/* Recommend optimal levels */
int recommend_levels(
    const MainEffect *effects,
    size_t count,
    bool higher_is_better,
    char *recommendation_buf,
    size_t buf_size
);

/* Free analysis results */
void free_main_effects(MainEffect *effects, size_t count);
```

**Main Effects Algorithm**:
1. For each factor:
   - Group runs by level
   - Calculate mean response per level
   - Compute range (max - min)
2. Rank factors by range (largest effect first)

**Testing**:
- Known datasets with expected effects
- Edge cases (all same value, missing data)
- Numerical precision
- Ranking correctness

---

### 6. `utils.c/h` - Utilities

**Purpose**: Common helpers, safe string handling, memory management

**Functions**:
```c
/* Safe string operations */
size_t safe_strncpy(char *dst, const char *src, size_t size);
char *trim_whitespace(char *str);
int split_string(const char *str, char delim, char **tokens, size_t max_tokens);

/* Memory management */
void *xmalloc(size_t size);  /* Malloc with error handling */
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t size);

/* Error reporting */
void set_error(char *error_buf, const char *fmt, ...);

/* String building (for JSON) */
typedef struct StringBuilder StringBuilder;
StringBuilder *sb_create(void);
void sb_append(StringBuilder *sb, const char *str);
void sb_append_char(StringBuilder *sb, char c);
char *sb_finalize(StringBuilder *sb);  /* Returns string, frees StringBuilder */
void sb_free(StringBuilder *sb);
```

**Testing**:
- String edge cases (empty, max length, null terminators)
- Memory allocation failures (mock malloc)
- String builder with large strings

---

### 7. `taguchi.c/h` (Library Facade)

**Purpose**: Implement public API by wrapping internal modules

This is the bridge between the public API (`include/taguchi.h`) and internal implementation. It handles:
- Opaque pointer conversions
- Memory management
- Error propagation
- Thread safety (if needed)

**Example Implementation Pattern**:
```c
/* taguchi.c */
#include "taguchi.h"
#include "parser.h"
#include "generator.h"
#include "serializer.h"

taguchi_experiment_def_t *taguchi_parse_definition(
    const char *content,
    char *error_buf
) {
    ExperimentDef *def = xcalloc(1, sizeof(ExperimentDef));
    
    if (parse_experiment_def_from_string(content, def, error_buf) != 0) {
        free(def);
        return NULL;
    }
    
    return (taguchi_experiment_def_t *)def;
}

void taguchi_free_definition(taguchi_experiment_def_t *def) {
    if (def) {
        free_experiment_def((ExperimentDef *)def);
        free(def);
    }
}

/* ... implement all public API functions ... */
```

---

### 8. CLI Implementation (`src/cli/`)

#### `main.c` - CLI Entry Point

**Purpose**: Argument parsing, subcommand dispatch

**Commands**:
```bash
taguchi <file.tgu> generate [--output FILE] [--format csv|json|tsv]
taguchi <file.tgu> run <script> [--output FILE] [--args env|flags|positional]
taguchi <file.tgu> analyze <results.csv> --metric NAME [--higher-better|--lower-better]
taguchi <file.tgu> effects <results.csv> [--output FILE]
taguchi <file.tgu> validate
taguchi --list-arrays
taguchi --help
taguchi --version
```

**Argument Parsing**:
- Use `getopt_long()` for POSIX compliance
- Validate required arguments
- Clear usage messages

**Exit Codes**:
- 0: Success
- 1: Usage error
- 2: File I/O error
- 3: Parse error
- 4: Runtime error

---

#### `runner.c/h` - Experiment Execution

**Purpose**: Execute external scripts for each experiment (CLI-only, not in library)

**Functions**:
```c
typedef enum {
    ARG_STYLE_ENV,    /* Pass as environment variables */
    ARG_STYLE_FLAGS,  /* Pass as --flag value */
    ARG_STYLE_POSITIONAL
} ArgStyle;

/* Run experiments via external command */
int run_experiments_cli(
    const taguchi_experiment_def_t *def,
    taguchi_experiment_run_t **runs,
    size_t count,
    const char *command,
    ArgStyle arg_style,
    FILE *output,
    char *error_buf
);
```

**Execution**:
- Fork/exec for each run
- Set environment variables: `TAGUCHI_RUN_ID`, `CACHE_SIZE`, etc.
- Capture stdout for metrics
- Parse CSV/JSON from stdout
- Timeout handling

---

#### `output.c/h` - CLI Output Formatting

**Purpose**: Human-readable output formatting (different from library JSON)

**Functions**:
```c
typedef enum {
    FORMAT_CSV,
    FORMAT_JSON,
    FORMAT_TSV,
    FORMAT_TABLE  /* ASCII table */
} OutputFormat;

/* Write runs in specified format */
void write_runs_cli(
    FILE *out,
    taguchi_experiment_run_t **runs,
    size_t count,
    OutputFormat format
);

/* Write effects in specified format */
void write_effects_cli(
    FILE *out,
    taguchi_main_effect_t **effects,
    size_t count,
    OutputFormat format
);
```

---

## Build System

### Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -pedantic -O2 -g -fPIC
LDFLAGS = -lm

SRC_DIR = src
LIB_DIR = $(SRC_DIR)/lib
CLI_DIR = $(SRC_DIR)/cli
TEST_DIR = tests
BUILD_DIR = build
INCLUDE_DIR = include

# Library sources (core logic)
LIB_SOURCES = $(wildcard $(LIB_DIR)/*.c)
LIB_OBJECTS = $(LIB_SOURCES:$(LIB_DIR)/%.c=$(BUILD_DIR)/lib/%.o)

# CLI sources (main + CLI-specific)
CLI_SOURCES = $(wildcard $(CLI_DIR)/*.c)
CLI_OBJECTS = $(CLI_SOURCES:$(CLI_DIR)/%.c=$(BUILD_DIR)/cli/%.o)

# Test sources
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TEST_DIR)/%.c=$(BUILD_DIR)/test/%.o)

# Targets
SHARED_LIB = libtaguchi.so
STATIC_LIB = libtaguchi.a
CLI_TARGET = taguchi
TEST_TARGET = $(BUILD_DIR)/test_runner

# Platform-specific library extension
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_LIB = libtaguchi.dylib
    SHARED_FLAGS = -dynamiclib -install_name @rpath/$(SHARED_LIB)
else ifeq ($(OS),Windows_NT)
    SHARED_LIB = taguchi.dll
    SHARED_FLAGS = -shared
else
    SHARED_FLAGS = -shared -Wl,-soname,$(SHARED_LIB)
endif

.PHONY: all lib cli test check install clean

all: lib cli

# Build shared library
lib: $(SHARED_LIB) $(STATIC_LIB)

$(SHARED_LIB): $(LIB_OBJECTS)
	$(CC) $(SHARED_FLAGS) $(LIB_OBJECTS) -o $@ $(LDFLAGS)

$(STATIC_LIB): $(LIB_OBJECTS)
	ar rcs $@ $(LIB_OBJECTS)

$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c | $(BUILD_DIR)/lib
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(LIB_DIR) -c $< -o $@

# Build CLI
cli: $(CLI_TARGET)

$(CLI_TARGET): $(CLI_OBJECTS) $(SHARED_LIB)
	$(CC) $(CLI_OBJECTS) -L. -ltaguchi -o $@ $(LDFLAGS)

$(BUILD_DIR)/cli/%.o: $(CLI_DIR)/%.c | $(BUILD_DIR)/cli
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(LIB_DIR) -c $< -o $@

# Build tests
test: $(TEST_TARGET)
	LD_LIBRARY_PATH=. ./$(TEST_TARGET)
	@echo "Running valgrind..."
	LD_LIBRARY_PATH=. valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS) $(STATIC_LIB)
	$(CC) $(TEST_OBJECTS) $(STATIC_LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/test/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)/test
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(LIB_DIR) -c $< -o $@

# Static analysis
check: test
	@echo "Running static analysis..."
	cppcheck --enable=all --suppress=missingIncludeSystem $(LIB_DIR) $(CLI_DIR)

# Create build directories
$(BUILD_DIR)/lib $(BUILD_DIR)/cli $(BUILD_DIR)/test:
	mkdir -p $@

# Installation
PREFIX = /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include
BINDIR = $(PREFIX)/bin

install: lib cli
	install -d $(LIBDIR) $(INCDIR) $(BINDIR)
	install -m 755 $(SHARED_LIB) $(LIBDIR)/
	install -m 644 $(STATIC_LIB) $(LIBDIR)/
	install -m 644 $(INCLUDE_DIR)/taguchi.h $(INCDIR)/
	install -m 755 $(CLI_TARGET) $(BINDIR)/
	@if command -v ldconfig >/dev/null 2>&1; then ldconfig; fi

# Uninstall
uninstall:
	rm -f $(LIBDIR)/$(SHARED_LIB)
	rm -f $(LIBDIR)/$(STATIC_LIB)
	rm -f $(INCDIR)/taguchi.h
	rm -f $(BINDIR)/$(CLI_TARGET)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(SHARED_LIB) $(STATIC_LIB) $(CLI_TARGET)

# Python bindings
.PHONY: python-install python-test
python-install: lib
	cd bindings/python && python3 setup.py install --user

python-test: python-install
	cd bindings/python && python3 test_taguchi.py

# Node bindings
.PHONY: node-install node-test
node-install: lib
	cd bindings/node && npm install

node-test: node-install
	cd bindings/node && npm test

# Documentation
.PHONY: docs
docs:
	doxygen Doxyfile
```

---

## Testing Strategy

### Unit Test Framework

Use a minimal testing framework (single-header, no dependencies):

```c
/* tests/test_framework.h */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) void test_##name(void)

#define RUN_TEST(name) do { \
    printf("Running %s...\n", #name); \
    test_##name(); \
    printf("  PASSED\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERTION FAILED: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_LT(a, b) ASSERT((a) < (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0)
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)
#define ASSERT_DOUBLE_EQ(a, b, epsilon) ASSERT(fabs((a) - (b)) < (epsilon))

#endif /* TEST_FRAMEWORK_H */
```

### Test Coverage Requirements

Each module must have:
1. **Happy path tests**: Valid inputs, expected outputs
2. **Edge cases**: Empty, max, boundary conditions
3. **Error handling**: Invalid inputs, resource failures
4. **Integration tests**: Cross-module interactions

### Example Test Structure

```c
/* tests/test_parser.c */
#include "test_framework.h"
#include "taguchi.h"

TEST(parse_valid_definition) {
    const char *tgu = 
        "factors:\n"
        "  cache_size: 64M, 128M, 256M\n"
        "  threads: 2, 4, 8\n"
        "array: L9\n";
    
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(tgu, error);
    
    ASSERT_NOT_NULL(def);
    taguchi_free_definition(def);
}

TEST(parse_malformed_syntax) {
    const char *tgu = "invalid syntax here";
    char error[TAGUCHI_ERROR_SIZE];
    
    taguchi_experiment_def_t *def = taguchi_parse_definition(tgu, error);
    
    ASSERT_NULL(def);
    ASSERT(strlen(error) > 0);  /* Error message set */
}

TEST(parse_duplicate_factor_names) {
    const char *tgu = 
        "factors:\n"
        "  cache: 64M, 128M\n"
        "  cache: 2, 4\n"
        "array: L4\n";
    
    char error[TAGUCHI_ERROR_SIZE];
    taguchi_experiment_def_t *def = taguchi_parse_definition(tgu, error);
    
    ASSERT_NULL(def);
}

/* tests/test_runner.c - Main test harness */
#include "test_framework.h"

/* Declare test functions */
extern void test_parse_valid_definition(void);
extern void test_parse_malformed_syntax(void);
extern void test_parse_duplicate_factor_names(void);
/* ... more test declarations ... */

int main(void) {
    printf("=== Taguchi Library Test Suite ===\n\n");
    
    /* Parser tests */
    printf("Parser Tests:\n");
    RUN_TEST(parse_valid_definition);
    RUN_TEST(parse_malformed_syntax);
    RUN_TEST(parse_duplicate_factor_names);
    
    /* Generator tests */
    printf("\nGenerator Tests:\n");
    /* ... */
    
    /* Analyzer tests */
    printf("\nAnalyzer Tests:\n");
    /* ... */
    
    printf("\n=== All Tests Passed ===\n");
    return 0;
}
```

---

## Error Handling Standards

### Principles
1. **Always check return values**: malloc, fopen, library calls
2. **Propagate errors up**: Return codes, not exceptions
3. **Clear messages**: Include context (operation, reason)
4. **Clean up resources**: Free memory, close files on error paths
5. **Fail fast**: Don't continue with invalid state

### Error Return Convention
```c
/* Library functions return 0 on success, -1 on error */
/* error_buf is populated with human-readable message */
int function_name(..., char *error_buf);

/* Usage */
char error[TAGUCHI_ERROR_SIZE];
if (taguchi_function(args, error) != 0) {
    fprintf(stderr, "Error: %s\n", error);
    /* cleanup */
    return -1;
}
```

### Error Message Guidelines
```c
/* Good error messages */
"Parse error on line 5: expected ':' after factor name"
"Array 'L9' supports max 4 factors, but 6 were provided"
"Factor 'threads' has 3 levels, but array 'L4' only supports 2 levels"

/* Bad error messages */
"Parse error"
"Invalid input"
"Failed"
```

---

## Memory Management

### Rules
1. **Every malloc has a free**: Track allocations in design
2. **Use valgrind**: Zero leaks, zero errors required
3. **RAII-style helpers**: `taguchi_free_*()` functions
4. **Avoid global state**: Prefer stack or explicit allocation
5. **Bounds checking**: Never trust input sizes

### Patterns
```c
/* Library functions allocate, caller frees */
taguchi_experiment_run_t **runs = NULL;
size_t count = 0;

if (taguchi_generate_runs(def, &runs, &count, error) != 0) {
    /* Error - nothing to free */
    return -1;
}

/* Use runs */
for (size_t i = 0; i < count; i++) {
    /* ... */
}

/* Always free */
taguchi_free_runs(runs, count);
```

```c
/* Internal cleanup with goto */
int internal_function(void) {
    char *buffer = NULL;
    FILE *file = NULL;
    int result = -1;
    
    buffer = malloc(SIZE);
    if (!buffer) goto cleanup;
    
    file = fopen("file.txt", "r");
    if (!file) goto cleanup;
    
    /* ... do work ... */
    result = 0;
    
cleanup:
    if (buffer) free(buffer);
    if (file) fclose(file);
    return result;
}
```

---

## Documentation Requirements

### Code Comments
- **Header files**: Document all public APIs with Doxygen-style comments
- **Implementation**: Complex algorithms, non-obvious choices
- **Data structures**: Layout, invariants, ownership

### Example Doxygen Comment:
```c
/**
 * Generate experiment runs from definition.
 * 
 * This function allocates an array of experiment run pointers based on
 * the orthogonal array specified in the definition. The caller is responsible
 * for freeing the returned array using taguchi_free_runs().
 * 
 * @param def Experiment definition (must not be NULL)
 * @param runs_out Output pointer to array of runs (allocated by function)
 * @param count_out Output pointer to number of runs generated
 * @param error_buf Buffer for error message (size TAGUCHI_ERROR_SIZE)
 * @return 0 on success, -1 on error
 * 
 * @note The runs_out array and all runs must be freed with taguchi_free_runs()
 * @see taguchi_free_runs()
 */
int taguchi_generate_runs(
    const taguchi_experiment_def_t *def,
    taguchi_experiment_run_t ***runs_out,
    size_t *count_out,
    char *error_buf
);
```

### README.md Sections
1. **Overview**: What is Taguchi, what does this library do
2. **Features**: Key capabilities
3. **Installation**: Building from source, using packages
4. **Quick Start**: Minimal example in C, Python, Node
5. **Usage Examples**: CLI usage, library usage
6. **File Format**: .tgu file specification
7. **API Reference**: Link to generated docs
8. **Contributing**: How to contribute
9. **License**: MIT or similar

### Man Page
Generate `taguchi.1` using standard man format:
```
.TH TAGUCHI 1 "January 2026" "version 1.0" "User Commands"
.SH NAME
taguchi \- Taguchi orthogonal array experiment design tool
.SH SYNOPSIS
.B taguchi
.I file.tgu
.B generate
[\fIOPTIONS\fR]
.br
.B taguchi
.I file.tgu
.B run
.I script
[\fIOPTIONS\fR]
...
```

---

## Development Phases

### Phase 1: Core Library Infrastructure (Week 1)
- [ ] Project structure (`src/lib/`, `src/cli/`, `include/`, `tests/`)
- [ ] Makefile with shared/static lib targets
- [ ] Public API header (`include/taguchi.h`) - complete
- [ ] `utils.c` with tests (string, memory helpers)
- [ ] Test framework (`tests/test_framework.h`)
- [ ] Basic test runner

**Deliverable**: Empty library that compiles, links, runs tests

### Phase 2: Parsing and Arrays (Week 2)
- [ ] `arrays.c` with L4, L8, L9, L16, L27 definitions
- [ ] `arrays.c` tests (verify orthogonality)
- [ ] `parser.c` for .tgu file parsing
- [ ] `parser.c` comprehensive tests
- [ ] Public API wrappers for parsing
- [ ] Verify library exports symbols correctly

**Deliverable**: Can parse .tgu files, lookup arrays

### Phase 3: Generation and Serialization (Week 3)
- [ ] `generator.c` for mapping definitions to runs
- [ ] `generator.c` tests
- [ ] `serializer.c` for JSON output
- [ ] `serializer.c` tests
- [ ] Public API wrappers for generation
- [ ] Integration test: parse → generate → serialize

**Deliverable**: Library can generate runs and output JSON

### Phase 4: Analysis (Week 4)
- [ ] `analyzer.c` for main effects calculation
- [ ] `analyzer.c` tests with known datasets
- [ ] Result set management
- [ ] Public API wrappers for analysis
- [ ] Integration test: full workflow

**Deliverable**: Complete library functionality

### Phase 5: CLI Implementation (Week 5)
- [ ] `main.c` with argument parsing
- [ ] `commands.c` for command dispatch
- [ ] `runner.c` for experiment execution
- [ ] `output.c` for CLI formatting
- [ ] CLI integration tests
- [ ] Man page

**Deliverable**: Working CLI tool

### Phase 6: Language Bindings (Week 6)
- [ ] Python binding (`bindings/python/taguchi.py`)
- [ ] Python tests (`bindings/python/test_taguchi.py`)
- [ ] Python setup.py
- [ ] Node.js binding (`bindings/node/index.js`)
- [ ] Node.js tests (`bindings/node/test.js`)
- [ ] Node.js package.json
- [ ] Example scripts for each language

**Deliverable**: Working Python and Node.js bindings

### Phase 7: Polish and Release (Week 7)
- [ ] Complete README with examples
- [ ] API documentation (Doxygen)
- [ ] Performance testing and optimization
- [ ] Valgrind clean (zero leaks)
- [ ] 90%+ test coverage (gcov)
- [ ] Static analysis clean (cppcheck)
- [ ] pkg-config file
- [ ] Release packages (source tarball, binaries)

**Deliverable**: Production-ready v1.0

---

## Implementation Guidelines

### Coding Style
- **Indentation**: 4 spaces, no tabs
- **Braces**: K&R style (opening brace on same line for functions)
- **Naming**: 
  - Functions: `snake_case`
  - Types: `PascalCase` (internal), `snake_case_t` (public)
  - Constants: `UPPER_SNAKE_CASE`
  - Macros: `UPPER_SNAKE_CASE`
- **Line length**: 100 characters max
- **Comments**: `/* C-style */` for all comments

### Example Style:
```c
/* Good */
int calculate_main_effects(
    const ResultSet *results,
    const ExperimentDef *def,
    MainEffect **effects_out,
    size_t *count_out,
    char *error_buf
) {
    size_t i, j;
    
    for (i = 0; i < def->factor_count; i++) {
        /* Calculate mean for each level */
        for (j = 0; j < def->factors[i].level_count; j++) {
            /* ... */
        }
    }
    
    return 0;
}

/* Bad */
int calculateMainEffects(const ResultSet* results,const ExperimentDef* def,
MainEffect** effects_out,size_t* count_out,char* error_buf){
  size_t i,j;
  for(i=0;i<def->factor_count;i++){
    for(j=0;j<def->factors[i].level_count;j++){
      // ...
    }
  }
  return 0;
}
```

### Performance Guidelines
- **Avoid premature optimization**: Correctness first
- **Profile before optimizing**: Use gprof/perf
- **Expected throughput**: 10K experiments/sec on modern hardware
- **Memory target**: <100MB for typical workloads
- **Startup time**: <10ms (library), <50ms (CLI)

### Security Guidelines
- **Input validation**: Never trust file contents
- **Buffer overflow protection**: Always check bounds
- **String safety**: Use `strncpy`, `snprintf`, never `strcpy`/`sprintf`
- **Integer overflow**: Check before multiplication/addition
- **No shell injection**: Don't use `system()`, validate all exec args

---

## Success Metrics

### Code Quality
- ✓ Compiles with no warnings (`-Wall -Wextra -Werror`)
- ✓ Passes all unit tests
- ✓ Zero valgrind errors/leaks
- ✓ >90% test coverage (gcov)
- ✓ Clean cppcheck static analysis
- ✓ No undefined behavior (address sanitizer)

### Functionality
- ✓ Generates valid L4, L8, L9, L16, L27 arrays
- ✓ Parses .tgu files correctly
- ✓ Shared library exports stable C API
- ✓ Python bindings work correctly
- ✓ Node.js bindings work correctly
- ✓ CLI executes experiments with env/flag/positional args
- ✓ Calculates main effects accurately
- ✓ Outputs valid CSV/JSON

### Performance
- ✓ Generates 10K experiments in <1s
- ✓ Shared library <300KB (stripped)
- ✓ CLI binary size <200KB (stripped)
- ✓ Library startup time <5ms
- ✓ No memory leaks under load

### Usability
- ✓ Clear error messages with context
- ✓ Helpful usage/help text
- ✓ CLI works in pipes
- ✓ Library has example code for C/Python/Node
- ✓ Man page installed
- ✓ API documentation generated (Doxygen)
- ✓ Works on Linux, macOS, Windows (MinGW)

---

## Library Distribution

### pkg-config Support

Create `taguchi.pc.in`:
```
prefix=@PREFIX@
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: taguchi
Description: Taguchi orthogonal array library
Version: @VERSION@
Libs: -L${libdir} -ltaguchi
Cflags: -I${includedir}
```

Installation:
```bash
# Generate .pc file during install
sed 's|@PREFIX@|/usr/local|g; s|@VERSION@|1.0.0|g' \
    taguchi.pc.in > /usr/local/lib/pkgconfig/taguchi.pc
```

Usage:
```bash
# Compile against library
gcc myapp.c $(pkg-config --cflags --libs taguchi)
```

### Python Package (`bindings/python/setup.py`)

```python
from setuptools import setup
import os
import subprocess

# Ensure library is built
lib_path = '../../libtaguchi.so'
if not os.path.exists(lib_path):
    print("Building library...")
    subprocess.run(['make', '-C', '../..', 'lib'], check=True)

setup(
    name='taguchi',
    version='1.0.0',
    description='Taguchi orthogonal array design and analysis',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    author='Your Name',
    author_email='you@example.com',
    url='https://github.com/yourusername/taguchi',
    py_modules=['taguchi'],
    python_requires='>=3.6',
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Topic :: Scientific/Engineering',
    ],
    data_files=[
        ('lib', ['../../libtaguchi.so']),
    ],
)
```

Installation:
```bash
pip install ./bindings/python
# or for development
pip install -e ./bindings/python
```

### Node Package (`bindings/node/package.json`)

```json
{
  "name": "taguchi",
  "version": "1.0.0",
  "description": "Taguchi orthogonal array design and analysis",
  "main": "index.js",
  "scripts": {
    "test": "node test.js",
    "install": "cd ../.. && make lib"
  },
  "keywords": [
    "taguchi",
    "design-of-experiments",
    "doe",
    "orthogonal-array",
    "statistics"
  ],
  "author": "Your Name",
  "license": "MIT",
  "dependencies": {
    "ffi-napi": "^4.0.3",
    "ref-napi": "^3.0.3"
  },
  "devDependencies": {
    "jest": "^29.0.0"
  },
  "repository": {
    "type": "git",
    "url": "https://github.com/yourusername/taguchi.git"
  }
}
```

Installation:
```bash
npm install taguchi
# or for development
npm link ./bindings/node
```

---

## Platform Support

### Linux
- Primary development platform
- GCC 4.9+ or Clang 3.5+
- Full support for shared libraries (.so)

### macOS
- GCC or Clang (from Xcode Command Line Tools)
- Dynamic libraries (.dylib)
- May need to adjust install_name for library

### Windows
- MinGW-w64 or Cygwin
- MSVC support optional (would require Makefile adjustments)
- DLL support

### CI/CD
Consider setting up GitHub Actions:
```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    
    steps:
    - uses: actions/checkout@v2
    - name: Build
      run: make all
    - name: Test
      run: make test
    - name: Check
      run: make check
```

---

## Future Enhancements (Post-v1.0)

### Library Features
- Full ANOVA tables with F-statistics
- Interaction plot data generation
- Signal-to-Noise (S/N) ratio calculations
- Custom array generation algorithm (beyond predefined)
- Confidence intervals for effects
- Multi-response optimization
- Outer array support (noise factors)

### Language Bindings
- Rust bindings (via bindgen + wrapper)
- Go bindings (via cgo)
- Ruby bindings (via FFI gem)
- Java bindings (via JNI)
- R package (via Rcpp)
- Julia bindings (via ccall)

### Tools and Integrations
- Web interface (WASM compilation with Emscripten)
- Jupyter notebook integration
- REST API server
- Database integration (PostgreSQL, SQLite)
- Cloud function support (AWS Lambda, Google Cloud Functions)

### Advanced Features
- Bayesian optimization on top of Taguchi
- Automated experiment scheduling
- Real-time experiment monitoring
- Visualization library (plots for effects, interactions)
- Report generation (PDF, HTML)

---

## Getting Started (Implementation Session)

When implementing, proceed in this order:

### Step 1: Repository Setup
```bash
mkdir taguchi
cd taguchi
git init
mkdir -p src/lib src/cli include tests bindings/python bindings/node examples docs
touch Makefile README.md LICENSE
```

### Step 2: Define Public API First
- Write complete `include/taguchi.h`
- Document all functions with Doxygen comments
- Get API review/feedback before implementation
- This is your contract with users

### Step 3: Build System
- Create Makefile with lib/cli/test targets
- Verify shared library builds on your platform
- Test linking with minimal C program
- Set up test framework

### Step 4: Implement Core Library (TDD)
For each module:
1. Write tests first (based on API)
2. Implement to pass tests
3. Refactor for clarity
4. Verify no memory leaks (valgrind)

Order:
1. `utils.c` - foundation
2. `arrays.c` - data
3. `parser.c` - input
4. `generator.c` - logic
5. `analyzer.c` - analysis
6. `serializer.c` - output
7. `taguchi.c` - public API facade

### Step 5: Implement CLI
- CLI should be thin wrapper around library
- All logic in library, CLI just handles I/O
- Use library API exclusively (dogfood)

### Step 6: Language Bindings
- Python first (simpler, ctypes built-in)
- Test thoroughly with pytest
- Node.js second (requires ffi-napi)
- Document with examples

### Step 7: Documentation and Release
- Write comprehensive README
- Generate API docs
- Performance testing
- Create release tarball
- Tag v1.0.0

---

## Example Workflows

### C Library Usage
```c
#include <taguchi.h>

int main() {
    char error[256];
    
    // Parse definition
    taguchi_experiment_def_t *def = 
        taguchi_parse_definition("factors:\n  x: 1,2,3\narray: L9\n", error);
    
    // Generate runs
    taguchi_experiment_run_t **runs;
    size_t count;
    taguchi_generate_runs(def, &runs, &count, error);
    
    // Analyze results
    taguchi_result_set_t *results = taguchi_create_result_set(def, "y");
    for (size_t i = 0; i < count; i++) {
        double y = run_experiment(runs[i]);  // Your code
        taguchi_add_result(results, i+1, y, error);
    }
    
    taguchi_main_effect_t **effects;
    size_t effect_count;
    taguchi_calculate_main_effects(results, &effects, &effect_count, error);
    
    // Cleanup
    taguchi_free_effects(effects, effect_count);
    taguchi_free_result_set(results);
    taguchi_free_runs(runs, count);
    taguchi_free_definition(def);
    return 0;
}
```

### Python Library Usage
```python
import taguchi

# Define experiment
exp = taguchi.ExperimentDef("""
factors:
  temperature: 100, 150, 200
  pressure: 10, 20, 30
  time: 5, 10, 15
array: L9
""")

# Generate runs
runs = exp.generate_runs()

# Run experiments and collect results
results = taguchi.ResultSet(exp, "yield")
for run in runs:
    y = run_experiment(run)  # Your code
    results.add_result(run['run_id'], y)

# Analyze
effects = results.calculate_main_effects()
for effect in effects:
    print(f"{effect['factor']}: range={effect['range']}")
```

### CLI Usage
```bash
# Create experiment definition
cat > experiment.tgu <<EOF
factors:
  cache_size: 64M, 128M, 256M
  threads: 2, 4, 8
  timeout: 30, 60, 120
array: L9
EOF

# Generate runs
taguchi experiment.tgu generate --format csv > runs.csv

# Run experiments
taguchi experiment.tgu run ./benchmark.sh --output results.csv

# Analyze
taguchi experiment.tgu analyze results.csv --metric throughput --higher-better

# View effects
taguchi experiment.tgu effects results.csv
```

---

## Troubleshooting Guide

### Common Build Issues

**Issue**: `cannot find -ltaguchi`
```bash
# Solution: Set library path
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
# or install library
sudo make install
```

**Issue**: Python can't find library
```bash
# Solution: Copy library to package
cp libtaguchi.so bindings/python/
# or set library path
export LD_LIBRARY_PATH=/path/to/lib:$LD_LIBRARY_PATH
```

**Issue**: Valgrind reports leaks
```bash
# Debug with
valgrind --leak-check=full --show-leak-kinds=all ./test_runner
# Check for missing free calls
```

---

## License Recommendation

Release into Public Domain using CC0 1.0 Universal (CC0 1.0) - Public Domain Dedication:

```
Creative Commons Legal Code

CC0 1.0 Universal

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator
and subsequent owner(s) (each and all, an "owner") of an original work
of authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for
the purpose of contributing to a commons of creative, cultural and
scientific works ("Commons") that the public can reliably and without
expectation of compensation, use, modify, merge, make available with
other works, incorporate into other works, reuse and redistribute as
freely as possible in any form whatsoever and for any purposes,
including without limitation commercial purposes. These owners may
contribute to the Commons to promote the ideal of a free culture and the
further production of creative, cultural and scientific works, or to
gain reputation or greater distribution for their Work in part through
the use and efforts of others.

For these and/or other purposes and motivations, and without any
expectation of additional consideration or compensation, the person
associating CC0 with a Work (the "Affirmer"), to the extent that he or
she is an owner of Copyright and Related Rights in the Work, voluntarily
elects to apply CC0 to the Work and publicly distribute the Work under
its terms, with knowledge of his or her Copyright and Related Rights in
the Work and the meaning and intended legal effect of CC0 on those
rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not
limited to, the following:

  i. the right to reproduce, adapt, distribute, perform, display,
     communicate, and translate a Work;
 ii. moral rights retained by the original author(s) and/or performer(s);
iii. publicity and privacy rights pertaining to a person's image or
     likeness depicted in a Work;
 iv. rights protecting against unfair competition in regards to a Work,
     subject to the limitations in paragraph 4(a), below;
  v. rights protecting the extraction, dissemination, use and reuse of data
     in a Work;
 vi. database rights (such as those arising under Directive 96/9/EC of the
     European Parliament and of the Council of 11 March 1996 on the legal
     protection of databases, and under any national implementation
     thereof, including any amended or successor version of such
     directive); and
vii. other similar, equivalent or corresponding rights throughout the
     world based on applicable law or treaty, and any national
     implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention
of, applicable law, Affirmer hereby overtly, fully, permanently,
irrevocably and unconditionally waives, abandons, and surrenders all of
Affirmer's Copyright and Related Rights and associated claims and causes
of action, whether now known or unknown (including existing as well as
future claims and causes of action), in the Work (i) in all territories
worldwide, (ii) for the maximum duration provided by applicable law or
treaty (including future time extensions), (iii) in any current or future
medium and for any number of copies, and (iv) for any purpose whatsoever,
including without limitation commercial, advertising or promotional
purposes (the "Waiver"). Affirmer makes the Waiver for the benefit of each
member of the public at large and to the detriment of Affirmer's heirs and
successors, fully intending that such Waiver shall not be subject to
revocation, rescission, cancellation, termination, or any other legal or
equitable action to disrupt the quiet enjoyment of the Work by the public
as contemplated by Affirmer's express Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason
be judged legally invalid or ineffective under applicable law, then the
Waiver shall be preserved to the maximum extent permitted taking into
account Affirmer's express Statement of Purpose. In addition, to the
extent the Waiver is so judged Affirmer hereby grants to each affected
person a royalty-free, non transferable, non sublicensable, non exclusive,
irrevocable and unconditional license to exercise Affirmer's Copyright and
Related Rights in the Work (i) in all territories worldwide, (ii) for the
maximum duration provided by applicable law or treaty (including future
time extensions), (iii) in any current or future medium and for any number
of copies, and (iv) for any purpose whatsoever, including without
limitation commercial, advertising or promotional purposes (the
"License"). The License shall be deemed effective as of the date CC0 was
applied by Affirmer to the Work. Should any part of the License for any
reason be judged legally invalid or ineffective under applicable law, such
partial invalidity or ineffectiveness shall not invalidate the remainder
of the License, and in such case Affirmer hereby affirms that he or she
will not (i) exercise any of his or her remaining Copyright and Related
Rights in the Work or (ii) assert any associated claims and causes of
action with respect to the Work, in either case contrary to Affirmer's
express Statement of Purpose.

4. Limitations and Disclaimers.

 a. No trademark or patent rights held by Affirmer are waived, abandoned,
    surrendered, licensed or otherwise affected by this document.
 b. Affirmer offers the Work as-is and makes no representations or
    warranties of any kind concerning the Work, express, implied,
    statutory or otherwise, including without limitation warranties of
    title, merchantability, fitness for a particular purpose, non
    infringement, or the absence of latent or other defects, accuracy, or
    the present or absence of errors, whether or not discoverable, all to
    the greatest extent permissible under applicable law.
 c. Affirmer disclaims responsibility for clearing rights of other persons
    that may apply to the Work or any use thereof, including without
    limitation any person's Copyright and Related Rights in the Work.
    Further, Affirmer disclaims responsibility for obtaining any necessary
    consents, permissions or other rights required for any use of the
    Work.
 d. Affirmer understands and acknowledges that Creative Commons is not a
    party to this document and has no duty or obligation with respect to
    this CC0 or use of the Work.
```

---

## Conclusion

This blueprint provides a complete, production-ready design for a Taguchi array tool with:

1. **Solid Foundation**: C99 library with clean API
2. **Maximum Reusability**: Shared library for language bindings
3. **Unix Philosophy**: Small, composable CLI tool
4. **Quality First**: Comprehensive testing, memory safety
5. **Developer Friendly**: Clear documentation, examples
6. **Future Proof**: Extensible design for enhancements

**Key Success Factors**:
- Library-first approach (CLI wraps library)
- Clean separation of concerns
- Comprehensive error handling
- Extensive testing (unit + integration)
- Memory safety (valgrind clean)
- Cross-platform support
- Multiple language bindings

**Ready to implement!** Follow the development phases, maintain test coverage, and you'll have a robust, reusable Taguchi library that serves both CLI users and programmers across multiple languages.

---

*This blueprint is ready for implementation. Each section has been carefully designed for clarity, completeness, and best practices. Good luck with your implementation!*

