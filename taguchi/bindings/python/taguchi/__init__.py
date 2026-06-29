"""
Taguchi Array Tool - Python Bindings

A Python interface to the Taguchi orthogonal array library for design of experiments.

Enhanced with improved error handling, configuration management, validation,
async support, and comprehensive diagnostics while maintaining full backward
compatibility.

Basic Usage:
    from taguchi import Experiment, Analyzer
    
    exp = Experiment()
    exp.add_factor("temp", ["350F", "375F", "400F"])
    runs = exp.generate()

Enhanced Usage with Configuration:
    from taguchi import Experiment, Analyzer, TaguchiConfig, Taguchi
    
    # Custom configuration
    config = TaguchiConfig(cli_timeout=120, debug_mode=True)
    taguchi = Taguchi(config=config)
    
    # Dependency injection
    with Experiment(taguchi=taguchi) as exp:
        exp.add_factor("temp", ["350F", "375F", "400F"])
        runs = exp.generate()
        
        with Analyzer(exp, taguchi=taguchi) as analyzer:
            analyzer.add_results_from_dict({1: 0.95, 2: 0.87, 3: 0.92})
            print(analyzer.summary())

Environment Variables:
    TAGUCHI_CLI_PATH       - Path to CLI binary
    TAGUCHI_CLI_TIMEOUT    - Command timeout in seconds  
    TAGUCHI_DEBUG          - Enable debug logging (true/false)
    TAGUCHI_MAX_RETRIES    - Number of retry attempts
    TAGUCHI_ENV_VARS       - Additional environment variables (KEY=value,...)

Async Support:
    from taguchi import AsyncTaguchi
    
    async def async_workflow():
        async_taguchi = AsyncTaguchi()
        arrays = await async_taguchi.list_arrays_async()
        return arrays
"""

# Import original modules for backward compatibility
from .core import Taguchi as _OriginalTaguchi, TaguchiError as _OriginalTaguchiError
from .experiment import Experiment as _OriginalExperiment  
from .analyzer import Analyzer as _OriginalAnalyzer

# Import enhanced modules
from .config import TaguchiConfig, ConfigManager
from .errors import (
    TaguchiError, BinaryDiscoveryError, CommandExecutionError, 
    TimeoutError, ValidationError
)
from .core_enhanced import Taguchi, AsyncTaguchi
from .experiment_enhanced import Experiment
from .analyzer_enhanced import Analyzer

# For backward compatibility, make sure the enhanced error inherits from original
# This ensures existing exception handling continues to work
class BackwardCompatibleTaguchiError(TaguchiError, _OriginalTaguchiError):
    """Backward compatible TaguchiError that inherits from both old and new."""
    pass

# Replace the enhanced TaguchiError with backward compatible version
TaguchiError = BackwardCompatibleTaguchiError

# Version info
__version__ = "1.6.0"  # Incremented for enhanced features

# Public API - maintains backward compatibility while adding enhanced features
__all__ = [
    # Core classes (enhanced but backward compatible)
    "Taguchi", 
    "TaguchiError",
    "Experiment", 
    "Analyzer",
    
    # Enhanced configuration and error handling
    "TaguchiConfig",
    "ConfigManager",
    "BinaryDiscoveryError",
    "CommandExecutionError", 
    "TimeoutError",
    "ValidationError",
    
    # Async support
    "AsyncTaguchi",
    
    # Original classes for explicit access if needed
    "OriginalTaguchi",
    "OriginalTaguchiError", 
    "OriginalExperiment",
    "OriginalAnalyzer",
]

# Expose original classes for advanced users who need explicit access
OriginalTaguchi = _OriginalTaguchi
OriginalTaguchiError = _OriginalTaguchiError
OriginalExperiment = _OriginalExperiment
OriginalAnalyzer = _OriginalAnalyzer

# Convenience functions for quick setup
def configure_from_environment() -> TaguchiConfig:
    """
    Create configuration from environment variables.
    
    Reads standard TAGUCHI_* environment variables to configure
    the library behavior.
    
    Returns:
        TaguchiConfig: Configuration loaded from environment
        
    Example:
        import os
        os.environ['TAGUCHI_DEBUG'] = 'true'
        os.environ['TAGUCHI_CLI_TIMEOUT'] = '120'
        
        config = configure_from_environment()
        taguchi = Taguchi(config=config)
    """
    return TaguchiConfig.from_environment()

def set_global_config(config: TaguchiConfig) -> None:
    """
    Set global default configuration for all new instances.
    
    Args:
        config: Configuration to use as default
        
    Example:
        config = TaguchiConfig(debug_mode=True, cli_timeout=120)
        set_global_config(config)
        
        # All new instances will use this config by default
        taguchi = Taguchi()  # Uses global config
        exp = Experiment()   # Uses global config  
    """
    ConfigManager.set_default_config(config)

def reset_global_config() -> None:
    """
    Reset global configuration to environment-based defaults.
    
    Example:
        reset_global_config()
        
        # New instances will read from environment variables
        taguchi = Taguchi()
    """
    ConfigManager.reset_default_config()

def verify_installation() -> dict:
    """
    Verify Taguchi installation and return diagnostic information.
    
    Returns:
        dict: Installation status and diagnostic information
        
    Example:
        status = verify_installation()
        if not status['cli_found']:
            print("Taguchi CLI not found!")
            print("Suggestions:", status['errors'][0]['suggestions'])
    """
    try:
        taguchi = Taguchi()
        return taguchi.verify_installation()
    except Exception as e:
        return {
            "cli_found": False,
            "errors": [{"type": "initialization_failure", "message": str(e)}],
            "environment_diagnostics": ConfigManager.diagnose_environment(),
        }

def diagnose_environment() -> dict:
    """
    Get detailed environment diagnostic information.
    
    Returns:
        dict: Environment diagnostic data
        
    Example:
        diagnostics = diagnose_environment()
        print("Python version:", diagnostics['python_version'])
        print("CLI found via PATH:", diagnostics['path_search_results']['PATH lookup']['found'])
    """
    return ConfigManager.diagnose_environment()

# Quick start example for documentation
def _example_usage():
    """Example usage (for documentation only)."""
    # Basic usage (backward compatible)
    with Experiment() as exp:
        exp.add_factor("temperature", ["low", "medium", "high"])
        exp.add_factor("pressure", ["low", "high"])
        runs = exp.generate()
        
        with Analyzer(exp) as analyzer:
            # Add your experimental results
            analyzer.add_result(1, 0.95)
            analyzer.add_result(2, 0.87)
            # ... add more results
            
            # Get analysis
            optimal = analyzer.recommend_optimal()
            print(f"Optimal settings: {optimal}")
    
    # Enhanced usage with configuration
    config = TaguchiConfig(debug_mode=True, cli_timeout=120)
    taguchi = Taguchi(config=config)
    
    with Experiment(taguchi=taguchi) as exp:
        exp.add_factor("temperature", ["low", "medium", "high"])
        
        # Enhanced validation
        if not exp.is_valid():
            print("Validation errors:", exp.validate())
            return
        
        runs = exp.generate()
        
        with Analyzer(exp, taguchi=taguchi) as analyzer:
            # Enhanced result management
            results = {1: 0.95, 2: 0.87, 3: 0.92}
            analyzer.add_results_from_dict(results)
            
            # Enhanced analysis
            rankings = analyzer.get_factor_rankings()
            prediction = analyzer.predict_response({"temperature": "high"})
            
            print(analyzer.summary())
