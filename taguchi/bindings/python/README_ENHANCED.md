# Taguchi Python Bindings - Enhanced Version

Enhanced Python bindings for the Taguchi orthogonal array library with improved error handling, configuration management, validation, async support, and comprehensive diagnostics while maintaining full backward compatibility.

## New Features in v1.6.0

### 🚀 **Phase 1: Critical Infrastructure**
- **Enhanced Binary Discovery**: `TAGUCHI_CLI_PATH` environment variable support with detailed error reporting
- **Rich Error Messages**: Comprehensive error context including commands, suggestions, and diagnostic information
- **Flexible Configuration**: `TaguchiConfig` class with environment variable support and validation

### 🔧 **Phase 2: API Improvements**  
- **Validation Methods**: `validate()` and `is_valid()` for experiments and analyzers
- **Dependency Injection**: Pass configured `Taguchi` instances to `Experiment` and `Analyzer`
- **Format Versioning**: CLI version checking and compatibility validation

### ⚡ **Phase 3: Advanced Features**
- **Async Support**: `AsyncTaguchi` for non-blocking operations
- **Debug Logging**: Configurable logging with command tracing
- **Enhanced Diagnostics**: Installation verification and environment analysis

## Quick Start

### Basic Usage (Backward Compatible)
```python
from taguchi import Experiment, Analyzer

# Create experiment (same as before)
with Experiment() as exp:
    exp.add_factor("temperature", ["low", "medium", "high"])
    exp.add_factor("pressure", ["low", "high"])
    runs = exp.generate()

# Analyze results (same as before)  
with Analyzer(exp, metric_name="yield") as analyzer:
    analyzer.add_result(1, 0.95)
    analyzer.add_result(2, 0.87)
    # ... add more results
    
    optimal = analyzer.recommend_optimal()
    print(f"Optimal settings: {optimal}")
```

### Enhanced Usage with Configuration
```python
from taguchi import Experiment, Analyzer, TaguchiConfig, Taguchi

# Create custom configuration
config = TaguchiConfig(
    cli_timeout=120,        # Longer timeout for large arrays
    debug_mode=True,        # Enable debug logging  
    max_retries=3,          # Retry failed commands
    environment_variables={"CUSTOM_VAR": "value"}
)

# Use dependency injection for consistent configuration
taguchi = Taguchi(config=config)

with Experiment(taguchi=taguchi) as exp:
    exp.add_factor("temperature", ["low", "medium", "high"])
    
    # Enhanced validation
    if not exp.is_valid():
        print("Validation errors:", exp.validate())
        return
    
    # Enhanced analysis capabilities
    comparison = exp.compare_with_full_factorial()
    print(f"Efficiency: {comparison['percentage_reduction']:.1f}% reduction")
    
    runs = exp.generate()
    
    with Analyzer(exp, taguchi=taguchi) as analyzer:
        # Enhanced result management
        results = {1: 0.95, 2: 0.87, 3: 0.92}
        analyzer.add_results_from_dict(results)
        
        # Enhanced analysis
        rankings = analyzer.get_factor_rankings()
        prediction = analyzer.predict_response({"temperature": "high"})
        
        print(analyzer.summary())
```

### Environment Variable Configuration
```bash
# Set environment variables
export TAGUCHI_CLI_PATH="/usr/local/bin/taguchi"
export TAGUCHI_CLI_TIMEOUT="120"
export TAGUCHI_DEBUG="true"
export TAGUCHI_MAX_RETRIES="3"
```

```python
from taguchi import configure_from_environment, Taguchi

# Load configuration from environment
config = configure_from_environment()
taguchi = Taguchi(config=config)
```

### Async Support
```python
from taguchi import AsyncTaguchi
import asyncio

async def async_workflow():
    async_taguchi = AsyncTaguchi()
    
    # Non-blocking operations
    arrays = await async_taguchi.list_arrays_async()
    runs = await async_taguchi.generate_runs_async("experiment.tgu")
    
    return arrays, runs

# Run async workflow
arrays, runs = asyncio.run(async_workflow())
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `TAGUCHI_CLI_PATH` | Path to CLI binary | Auto-detected |
| `TAGUCHI_CLI_TIMEOUT` | Command timeout (seconds) | 30 |
| `TAGUCHI_DEBUG` | Enable debug logging | false |
| `TAGUCHI_MAX_RETRIES` | Number of retry attempts | 0 |
| `TAGUCHI_RETRY_DELAY` | Delay between retries (seconds) | 1.0 |
| `TAGUCHI_WORKING_DIR` | Working directory for commands | Current dir |
| `TAGUCHI_ENV_VARS` | Additional environment variables | None |

## Enhanced Error Handling

### Rich Error Context
```python
try:
    taguchi = Taguchi()
except BinaryDiscoveryError as e:
    print(f"Error: {e}")
    # Includes:
    # - Searched paths with existence/permission status
    # - Environment variable values  
    # - Actionable suggestions
    print(f"Suggestions: {e.suggestions}")
```

### Validation Errors
```python
from taguchi import Experiment, ValidationError

try:
    with Experiment() as exp:
        exp.add_factor("invalid name with spaces", ["low", "high"])
except ValidationError as e:
    print(f"Validation failed: {e}")
    # Detailed error messages with specific issues
```

## Enhanced Validation

### Experiment Validation
```python
with Experiment() as exp:
    exp.add_factor("temperature", ["low", "medium", "high"])
    exp.add_factor("pressure", ["low", "high"])
    
    # Validate configuration
    if exp.is_valid():
        print("✓ Experiment is valid")
    else:
        print("Validation errors:")
        for error in exp.validate():
            print(f"  - {error}")
    
    # Get efficiency metrics
    comparison = exp.compare_with_full_factorial()
    runtime = exp.estimate_runtime(seconds_per_run=60)
```

### Analyzer Validation
```python
with Analyzer(exp) as analyzer:
    analyzer.add_results_from_dict(results)
    
    # Check completeness
    completeness = analyzer.check_completeness()
    print(f"Data completeness: {completeness['completion_percentage']:.1f}%")
    
    if analyzer.is_valid():
        # Enhanced analysis features
        rankings = analyzer.get_factor_rankings()
        significant = analyzer.get_significant_factors()
        prediction = analyzer.predict_response({"temp": "high"})
```

## Diagnostics and Troubleshooting

### Installation Verification
```python
from taguchi import verify_installation

status = verify_installation()
if status['cli_found']:
    print(f"✓ CLI found at: {status['cli_path']}")
    print(f"✓ Version: {status['cli_version']}")
else:
    print("Installation issues:")
    for error in status['errors']:
        print(f"  {error['message']}")
        for suggestion in error['suggestions']:
            print(f"    - {suggestion}")
```

### Environment Diagnostics
```python
from taguchi import diagnose_environment

diagnostics = diagnose_environment()
print(f"Working directory: {diagnostics['working_directory']}")
print(f"Python version: {diagnostics['python_version']}")

# Check binary search results
for location, info in diagnostics['path_search_results'].items():
    print(f"{location}: {info}")
```

### Global Configuration
```python
from taguchi import set_global_config, TaguchiConfig

# Set global defaults
config = TaguchiConfig(debug_mode=True, cli_timeout=120)
set_global_config(config)

# All new instances use global config
taguchi = Taguchi()  # Uses global config
exp = Experiment()   # Uses global config
```

## Migration Guide

The enhanced bindings are **fully backward compatible**. Existing code continues to work unchanged:

### No Changes Needed
```python
# This code works exactly the same
from taguchi import Experiment, Analyzer, TaguchiError

with Experiment() as exp:
    exp.add_factor("temp", ["low", "high"])
    runs = exp.generate()
    
with Analyzer(exp) as analyzer:
    analyzer.add_result(1, 0.95)
    effects = analyzer.main_effects()
```

### Gradual Enhancement
```python
# Gradually add enhanced features
from taguchi import TaguchiConfig

# Start with configuration
config = TaguchiConfig(debug_mode=True)

# Add validation
if not exp.is_valid():
    print(exp.validate())

# Add enhanced analysis
rankings = analyzer.get_factor_rankings()
```

## Error Reference

| Error Type | Description | Common Causes |
|------------|-------------|---------------|
| `BinaryDiscoveryError` | CLI binary not found | Missing installation, wrong PATH |
| `TimeoutError` | Command timeout | Large arrays, slow system |
| `ValidationError` | Invalid configuration | Bad factor names, incompatible arrays |
| `CommandExecutionError` | CLI command failed | Malformed .tgu, missing files |

## Performance Notes

- **Configuration overhead**: Minimal (<1ms per instance)
- **Validation caching**: Results cached until configuration changes
- **Retry logic**: Exponential backoff for transient failures
- **Async operations**: True parallelism for independent operations

## Examples

See the `examples/` directory for comprehensive usage examples:

- `enhanced_basic_usage.py` - Complete feature demonstration
- `async_usage_example.py` - Async workflows and patterns  
- `troubleshooting_guide.py` - Diagnostic and troubleshooting

## Backward Compatibility

- ✅ All existing APIs unchanged
- ✅ Same import paths and class names  
- ✅ Original error types still caught
- ✅ No performance regression
- ✅ Original classes available as `OriginalTaguchi`, etc.

## Requirements

- Python 3.7+
- Taguchi CLI binary (built from source or installed)
- Optional: `asyncio` for async features

## Installation

The enhanced bindings are included in the standard package:

```bash
# Install from source
cd ~/workspace/taguchi/bindings/python
pip install -e .

# Verify installation
python -c "from taguchi import verify_installation; print(verify_installation())"
```

---

**Need Help?**
- Use `verify_installation()` for installation issues
- Use `diagnose_environment()` for environment problems  
- Enable debug mode with `TaguchiConfig(debug_mode=True)`
- Check examples in the `examples/` directory