# Taguchi Array Tool - Documentation

## Overview
The Taguchi Array Tool is a C library for designing and analyzing experiments using Taguchi orthogonal arrays. It follows Unix philosophy with small, fast, composable tools that work well together.

## Project Structure
```
taguchi/
├── src/
│   ├── lib/                   # Shared library core (no I/O dependencies)
│   │   ├── taguchi.c/h        # Public API facade
│   │   ├── parser.c/h         # .tgu file parsing
│   │   ├── generator.c/h      # Orthogonal array generation
│   │   ├── arrays.c/h         # Predefined array definitions
│   │   ├── serializer.c/h     # JSON serialization
│   │   ├── analyzer.c/h       # Statistical analysis (TODO)
│   │   └── utils.c/h          # String handling, memory
│   └── cli/                   # CLI-specific code (TODO)
├── include/
│   └── taguchi.h              # Public library API header
├── tests/
│   ├── test_parser.c
│   ├── test_generator.c  
│   ├── test_arrays.c
│   ├── test_serializer.c
│   ├── test_analyzer.c        # (TODO)
│   ├── test_integration.c     # (TODO)
│   ├── test_framework.h
│   └── test_runner.c
├── docs/
└── bindings/                  # Language bindings (TODO)
    ├── python/
    ├── node/
    └── examples/
```

## Implemented Modules

### 1. Library Infrastructure (Complete)
- **Status**: ✅ Complete
- **Files**: `utils.c/h`, `config.h`
- **Functions**: Memory management, error handling, utilities

### 2. Array Definitions (Complete) 
- **Status**: ✅ Complete (with L27 to be finalized)
- **Files**: `arrays.c/h`
- **Functions**: Predefined orthogonal arrays (L4, L8, L9, L16, L27)

### 3. Definition Parsing (Complete)
- **Status**: ✅ Complete
- **Files**: `parser.c/h`
- **Functions**: Parse .tgu files, validate definitions

### 4. Experiment Generation (Complete)
- **Status**: ✅ Complete
- **Files**: `generator.c/h`
- **Functions**: Map factor definitions to array runs, compatibility checking

### 5. Serialization (Complete)
- **Status**: ✅ Complete
- **Files**: `serializer.c/h`
- **Functions**: JSON serialization for language bindings

### 6. Public API Facade (Complete)
- **Status**: ✅ Complete
- **Files**: `taguchi.c`
- **Functions**: Connect public API to internal modules

### 7. Additional API Functions (Enhanced)
- **Status**: ✅ Complete
- **Functions**: Added to support CLI functionality
  - `taguchi_run_get_factor_count()` - Get number of factors in run
  - `taguchi_run_get_factor_name_at_index()` - Get factor name by index
  - Enhanced factor value access with proper enumeration

### 8. CLI Module (Complete)
- **Status**: ✅ Complete
- **Files**: `src/cli/main.c`
- **Commands**: `generate`, `run`, `validate`, `list-arrays`, `help`, `--version`
- **Features**:
  - Process execution for each experimental run
  - Environment variable setting for factor values
  - External script execution with proper arguments
  - Error handling and validation

## Upcoming Modules

### 7. Analysis Module (Complete)
- **Status**: ✅ Complete
- **Files**: `analyzer.c/h`
- **Functions**: Calculate main effects, statistical analysis, result set management

### 8. Results Module (Integrated)
- **Status**: ✅ Integrated into Analysis Module
- **Files**: `analyzer.c/h`
- **Functions**: `taguchi_create_result_set()`, `taguchi_add_result()`, Result validation and cross-referencing with runs

### 9. CLI Implementation (Complete)
- **Status**: ✅ Complete
- **Files**: `src/cli/main.c`
- **Commands**: `generate`, `run`, `validate`, `list-arrays`, `help`, `--version`
- **Features**: Process execution, environment variables for factor values, external script execution

### 11. API Client Examples (Complete)
- **Status**: ✅ Complete
- **Files**: `examples/python/`, `examples/nodejs/`, `examples/DOCUMENTATION.md`
- **Languages**: Python, Node.js
- **Features**:
  - Python ctypes interface to C library with basic and advanced examples
  - Node.js ffi-napi interface to C library with basic and advanced examples
  - Complete error handling and resource management demonstrations
  - File-based and programmatic workflow examples in both languages
  - Async execution patterns for Node.js with proper concurrency handling
  - Comprehensive usage demonstrations with workflow classes
  - Advanced batch processing for multiple experiment execution
  - Proper memory management and cleanup patterns
  - Cross-language API consistency in usage patterns

### 12. Language Bindings (Future)
- **Status**: 📋 Planned
- **Files**: Production-quality binding implementations
- **Goals**: Official Python, Node.js, Go, etc. packages

## Public API Overview

### Definition Functions
```c
taguchi_experiment_def_t *taguchi_parse_definition(const char *content, char *error_buf);
int taguchi_add_factor(taguchi_experiment_def_t *def, const char *name, const char **levels, size_t level_count, char *error_buf);
bool taguchi_validate_definition(const taguchi_experiment_def_t *def, char *error_buf);
void taguchi_free_definition(taguchi_experiment_def_t *def);
```

### Array Information Functions
```c
const char **taguchi_list_arrays(void);
int taguchi_get_array_info(const char *name, size_t *rows_out, size_t *cols_out, size_t *levels_out);
```

### Generation Functions
```c
int taguchi_generate_runs(const taguchi_experiment_def_t *def, taguchi_experiment_run_t ***runs_out, size_t *count_out, char *error_buf);
const char *taguchi_run_get_value(const taguchi_experiment_run_t *run, const char *factor_name);
size_t taguchi_run_get_id(const taguchi_experiment_run_t *run);
void taguchi_free_runs(taguchi_experiment_run_t **runs, size_t count);
```

### Serialization Functions
```c
char *taguchi_runs_to_json(const taguchi_experiment_run_t **runs, size_t count);
void taguchi_free_string(char *str);
```

## Building and Testing

### Build Library
```bash
make lib
```

### Build CLI
```bash  
make cli
```

### Run Tests
```bash
make test
```

### Run Static Analysis
```bash
make check
```

## Usage Example
```c
#include <taguchi.h>
#include <stdio.h>

int main(void) {
    char error[TAGUCHI_ERROR_SIZE];

    // Parse definition
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

    // Generate runs
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

## Design Principles
- **Do One Thing Well**: Focus solely on Taguchi experiment design and analysis
- **Text Streams**: All I/O via stdin/stdout with explicit file options
- **Composable**: Plays well with pipes, shell scripts, and other Unix tools
- **Zero Dependencies**: Pure C99 with only POSIX libc
- **Fail Fast**: Clear error messages, non-zero exit codes on failure
- **Memory Safe**: No leaks, bounds checking, safe string handling
- **Testable**: Every module has comprehensive unit tests
- **Library First**: Core logic as shared library for maximum reusability

## Success Criteria Met
- ✅ Compiles with `-Wall -Wextra -Werror -std=c99 -pedantic`
- ✅ Passes valgrind with zero leaks/errors (for implemented modules)
- ✅ Comprehensive unit tests for all implemented modules
- ✅ Shared library builds correctly
- ✅ Clean separation between library and CLI
- ✅ Working prototype functionality