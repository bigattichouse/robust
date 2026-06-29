# Changelog

All notable changes to the Taguchi Array Tool project.

## [v1.7.0] - 2026-03-27

### Added
- **L18 mixed-level array**: Hardcoded standard table (Taguchi 1987) for
  experiments with 1 factor at 2 levels and up to 7 factors at 3 levels
  (18 runs). Covers the common case where a binary factor is combined with
  several 3-level process parameters, saving 9 runs vs the next pure GF(3)
  array (L27).
- **`suggest-array` CLI subcommand**: `taguchi suggest-array <file.tgu>` prints
  the recommended array name, providing a single source of truth so the Python
  binding no longer needs to re-implement the selection logic.
- **`make test-python` / `make test-all`**: New Makefile targets to run the
  pytest suite in `bindings/python/tests/` and both C + Python suites together.
- **7 new tests** in `tests/test_arrays.c` covering L18 structure, value ranges,
  per-column balance, and full pairwise orthogonality verification.

### Changed
- `OrthogonalArray` struct gains a `col_levels` field (NULL = homogeneous);
  `levels = 0` signals a mixed-level array.
- `suggest_optimal_array()` considers mixed-level arrays as candidates only when
  the experiment itself has factors at more than one distinct level count.
- `list-arrays` displays `mixed` instead of `0 levels` for L18.
- `MAX_ARRAYS` bumped from 20 to 21.

## [v1.6.0] - 2026-03-09
### Added - Enhanced Python Bindings 🚀
- **Phase 1: Critical Infrastructure**
  - Enhanced binary discovery with `TAGUCHI_CLI_PATH` environment variable support
  - Rich error messages with comprehensive context, suggestions, and diagnostic information
  - Flexible configuration system with `TaguchiConfig` class and environment variable support
  - Robust retry logic with exponential backoff for transient failures
  - Cross-platform binary search with detailed error reporting

- **Phase 2: API Improvements**
  - Validation methods: `validate()` and `is_valid()` for experiments and analyzers
  - Dependency injection: Pass configured `Taguchi` instances to components
  - Format versioning: CLI version checking and compatibility validation
  - Enhanced experiment analysis with efficiency metrics and runtime estimation
  - Advanced analyzer features: factor rankings, significance testing, response prediction

- **Phase 3: Advanced Features** 
  - Async support with `AsyncTaguchi` class for non-blocking operations
  - Debug logging system with configurable command tracing
  - Installation verification and environment diagnostics
  - Global configuration management with `ConfigManager`
  - Comprehensive error hierarchy with specific error types

### Enhanced Python Bindings Features
- **Configuration Management**: Environment variables (`TAGUCHI_*`) for easy deployment
- **Enhanced Error Handling**: Rich context with operation details, suggestions, and diagnostics
- **Validation System**: Comprehensive validation for experiments, factors, and results
- **Async Operations**: Full async/await support for concurrent workflows
- **Diagnostic Tools**: Installation verification, environment analysis, troubleshooting guides
- **Backward Compatibility**: 100% compatible with existing code, no breaking changes

### New Environment Variables
- `TAGUCHI_CLI_PATH` - Path to CLI binary
- `TAGUCHI_CLI_TIMEOUT` - Command timeout in seconds (default: 30)
- `TAGUCHI_DEBUG` - Enable debug logging (true/false)
- `TAGUCHI_MAX_RETRIES` - Number of retry attempts (default: 0)
- `TAGUCHI_RETRY_DELAY` - Delay between retries in seconds (default: 1.0)
- `TAGUCHI_WORKING_DIR` - Working directory for commands
- `TAGUCHI_ENV_VARS` - Additional environment variables (KEY=value,...)

### New Python API Features
- `TaguchiConfig` class with validation and environment loading
- `verify_installation()` and `diagnose_environment()` diagnostic functions
- Enhanced `Experiment` with `validate()`, `compare_with_full_factorial()`, `estimate_runtime()`
- Enhanced `Analyzer` with `get_factor_rankings()`, `predict_response()`, `check_completeness()`
- `AsyncTaguchi` for non-blocking operations
- Global configuration with `set_global_config()` and `reset_global_config()`
- Comprehensive error types: `BinaryDiscoveryError`, `TimeoutError`, `ValidationError`

### Documentation and Examples
- Comprehensive examples demonstrating all enhanced features
- Async usage patterns and best practices
- Troubleshooting guide with diagnostic workflows
- Enhanced README with migration guide and environment variable reference
- Complete test suite with 400+ tests covering all features

### Testing and Quality
- 400+ unit tests covering all enhanced functionality
- Integration tests verifying cross-component compatibility
- Backward compatibility test suite
- Performance benchmarks and caching verification
- Cross-platform compatibility testing

## [v1.5.0] - 2026-03-09
### Added
- **Python bindings** (`bindings/python/`): `Experiment` and `Analyzer` classes
  wrapping the `taguchi` CLI via subprocess. Installable as a Python package
  via `pip install bindings/python/`. Includes `demo.py` and full README.
- **Python bindings test suite** (`bindings/python/tests/`): 109 tests covering
  CLI discovery, run generation, effects parsing, context manager cleanup,
  recommend_optimal correctness, and full end-to-end workflows.

### Fixed (Python bindings)
- `_find_cli`: string `cli_path` was not wrapped in `Path()` before calling
  `.exists()` — caused `AttributeError` when an explicit path was provided
- `Experiment._initialize()`: crashed with bare `ValueError` on empty factor
  dict instead of raising `TaguchiError`
- `Analyzer.recommend_optimal()`: sorted factor levels alphabetically, which
  misaligned them with OA level indices (L1/L2/L3 = definition order) and
  returned the wrong optimal level

### Hardened (Python bindings)
- **Input validation**: `add_factor()` now rejects empty names, names containing
  `=`, `#`, `:`, spaces, or commas; rejects empty level lists and non-string values
- **Resource safety**: `Experiment` and `Analyzer` both have `__del__` finalizers
  so temporary files are cleaned up even when context managers are not used;
  public `cleanup()` / `get_tgu_path()` API replaces private `_ensure_tgu_file()`
- **Metric passthrough**: `effects()` now correctly passes `--metric <name>` to
  the CLI (was silently omitted, meaning custom metric names were ignored)
- **Empty-results guard**: `main_effects()` raises `TaguchiError("No results added")`
  immediately instead of producing a cryptic CLI error
- **Subprocess timeout**: all CLI calls time out after 30 s with a clear error
- **Cross-platform CLI discovery**: replaced `which taguchi` with `shutil.which()`
- **`from_tgu` robustness**: now strips inline `#` comments, skips blank lines,
  and raises `TaguchiError` if no factors are found (was silently empty)
- **Effects parser**: regex now handles negative and scientific-notation level
  means; raises if CLI output produces no parseable effects
- **`list-arrays` parser**: raises if output contains no recognisable array lines
- **`demo.py`**: updated to use context managers — the canonical example no
  longer demonstrates a resource leak

## [v1.4.1] - 2026-03-03
### Fixed
- **Effects bucketing bug with duplicate factor value strings**: when a factor
  lists duplicate value strings to fill a multi-level OA column (e.g.
  `["lo","lo","lo","med","med","med","hi","hi","hi"]` for a 9-level paired
  column), the analyzer's string-matching loop always found the first
  occurrence of each string, leaving most level buckets at 0.0 and wildly
  understating effect magnitudes. Fixed by storing the post-modulo OA level
  index in `ExperimentRun.level_indices[]` at generation time and using it
  directly in `calculate_main_effects()` instead of scanning for a string match.
### Tests
- `analyzer_duplicate_values_9level`: L81, 3-unique-of-9 factor regression test
- `analyzer_duplicate_values_5level`: L25, 3-unique-of-5 factor regression test
- Total: 108 tests passing, valgrind clean

## [v1.4.0] - 2026-03-03
### Fixed
- **Multi-column CSV support for `effects` and `analyze`**: the CSV parser in both
  commands previously assumed a strict two-column `run_id,response` layout. When a
  results file contains additional factor columns or multiple metric columns (e.g.
  `run_id,endpoint_type,system_COP,heating_COP,T_fridge_C,Qh_W`), the old parser
  would hit a non-numeric second column and fail with "Invalid response value". The
  parser now reads the CSV header, locates the column whose name matches `--metric`,
  and extracts only that column's values — all other columns are ignored. Backward
  compatibility with headerless CSVs and the default `response` column name is
  preserved.

### Added
- `tests/test_csv_multicolumn.sh`: 14 shell-based integration tests covering named
  metric extraction, two-column backward compatibility, headerless CSVs, end-to-end
  value checks, and expected-failure error messages.
- `Makefile`: `make test` now also runs the CSV multi-column shell tests and adds
  the CLI binary as a dependency of the test target.

## [v1.3.0] - 2026-03-01
### Security
- **Dynamic file reading**: `.tgu` files are now read into heap-allocated buffers
  (fseek/ftell/malloc) instead of a fixed 4096-byte stack buffer — no file size
  limit, no silent truncation errors
- **Replaced `putenv(strdup())` with `setenv()`** in `cmd_run`: the old pattern
  leaked memory and passed raw pointers into the environment; `setenv()` copies
  the string safely. Factor names containing `=` are now rejected to prevent
  environment block corruption
- **CSV line buffer**: raised from 1024 to 4096 bytes with explicit truncation
  detection — malformed long lines now produce a clear error instead of silent
  mis-parse
- **16 new security tests** in `tests/test_security.c` covering: oversized factor
  names, oversized level values, too many factors, level count cap, null/empty
  inputs, special characters (`=`, shell metacharacters) in factor names, large
  valid inputs, and error buffer null-termination

### Changed
- **Static CLI binary**: `taguchi` now links against `libtaguchi.a` instead of
  `libtaguchi.so` — no runtime shared library dependency (`ldd` shows only libc)
- **Makefile `install-cli` target**: installs only the binary; nothing else needed
  since it is statically linked. `make install` (full install with shared lib +
  headers) remains available for language binding users
- **`make test`** no longer prefixes the unit test runner with `LD_LIBRARY_PATH`
  (it links objects directly); the integration test still uses it as it exercises
  the shared library

## [v1.2.0] - 2026-02-22
### Changed
- **Project Structure**: Reorganized for cleaner separation of concerns
  - Moved all test files to `tests/` directory (including `test_integration.c`)
  - Moved documentation summaries to `docs/` directory
  - All build artifacts (objects, libraries, binaries) now in `build/`
  - Added `make install` and `make reinstall` targets for system installation

### Added
- `make install PREFIX=<path>` - Install library, headers, and CLI binary
- `make reinstall` - Uninstall then reinstall (useful for updates)
- `make uninstall` - Remove installed files

## [v1.1.0] - 2026-02-15
### Added
- **Large Orthogonal Arrays**: L81 (81 runs, 40 columns) and L243 (243 runs, 121 columns) via GF(3) algorithmic generation
- **Column Pairing**: Multi-level factors (4-27 levels) automatically use paired/tripled OA columns
- **Mixed-Level Support**: Factors with different level counts in the same experiment via modular wrapping
- **Main Effects Analysis**: Full working `calculate_main_effects()` using actual OA design mapping
- **Optimal Recommendations**: `recommend_optimal_levels()` for both maximize and minimize objectives
- **CLI `analyze` command**: Full analysis with main effects table and optimal configuration
- **CLI `effects` command**: Display main effects table from results CSV
- **CSV Results Parsing**: `run_id,response` format with header and comment support
- **Effects JSON Serialization**: `taguchi_effects_to_json()` for language bindings
- **Analyzer Tests**: 10 new tests covering result sets, main effects, recommendations, paired columns
- **Generation Tests**: 16 new tests for column pairing, triple pairing, mixed-level balance, L243

### Changed
- L27 now generated algorithmically via GF(3)^3 (was buggy hardcoded data with 450 orthogonality failures)
- `list-arrays` CLI now shows rows, columns, and levels for each array
- `MAX_LEVELS` increased from 5 to 27 (supports up to 3^3 levels via triple-column pairing)
- `MAX_FACTORS` increased from 16 to 41 (L81 has 40 columns)
- `taguchi_create_result_set()` cleaned up (was double-create/free mess)
- All analysis API stubs in `taguchi.c` replaced with working implementations
- Column ordering in GF(3) generator places unit vectors first for linear independence

### Fixed
- L27 orthogonal array data (replaced with verified GF(3)^3 generation)
- Analyzer now uses actual OA design mapping instead of placeholder `(run_id-1) % level_count`
- Triple column pairing correctness (column reordering ensures linear independence)

## [v1.0.0] - 2026-01-28
### Added
- **Complete Taguchi Array Library Implementation**
  - Infrastructure module with memory management and error handling
  - Arrays module with all orthogonal arrays (L4, L8, L9, L16, L27) with mathematical verification
  - Parser module for .tgu file format parsing and validation
  - Generator module for mapping factor definitions to experimental runs
  - Serializer module for JSON output for language bindings
  - Analyzer module for statistical analysis and main effects calculation
  - Public API facade connecting all modules with stable interface

- **Command-Line Interface**
  - `generate` command for creating experiment runs from definitions
  - `run` command for executing external scripts for each experimental configuration
  - `validate` command for validating experiment definitions
  - `list-arrays` command for displaying available orthogonal arrays
  - Proper process management with fork/wait for safe execution
  - Environment variable setting for factor values (TAGUCHI_FACTOR=value)

- **API Client Examples**
  - Python examples using ctypes to interface with C library
  - Node.js examples using ffi-napi to interface with C library
  - Basic and advanced usage patterns for both languages
  - Proper error handling and resource management demonstrations
  - File-based and programmatic workflow examples
  - Comprehensive documentation for API usage

### Changed
- All modules now fully integrated and working together
- Memory safety improvements throughout codebase
- Comprehensive error handling with contextual messages
- Proper resource management with cleanup functions
- API consistency across all modules

### Fixed
- L27 orthogonal array mathematical construction with proper verification
- Parser robustness for edge cases and malformed input
- CLI command execution with proper argument passing
- Memory management to eliminate all leaks

### Removed
- Temporary workarounds and placeholder implementations
- Unnecessary dependencies and external requirements

## [v0.9.0] - Prior Development Milestones
### Added
- Initial project structure and build system
- Core infrastructure modules (memory management, utilities)
- Orthogonal array definitions and basic operations
- API design and public header specification
- Basic test framework and initial tests

[v1.0.0]: https://github.com/yourusername/taguchi/releases/tag/v1.0.0