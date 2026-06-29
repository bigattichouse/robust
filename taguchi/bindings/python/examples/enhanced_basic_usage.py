#!/usr/bin/env python3
"""
Enhanced Basic Usage Example

This example demonstrates the enhanced Taguchi Python bindings with
improved error handling, configuration, and validation while maintaining
full backward compatibility.
"""

import os
import sys
from pathlib import Path

# Add taguchi package to path for examples
sys.path.insert(0, str(Path(__file__).parent.parent))

from taguchi import (
    Experiment, Analyzer, TaguchiConfig, Taguchi,
    configure_from_environment, verify_installation
)


def basic_usage_example():
    """Basic usage (fully backward compatible)."""
    print("=== Basic Usage Example (Backward Compatible) ===")
    
    try:
        with Experiment() as exp:
            # Add factors as before
            exp.add_factor("temperature", ["350F", "375F", "400F"])
            exp.add_factor("time", ["10min", "15min", "20min"])
            exp.add_factor("ingredient", ["A", "B"])
            
            print(f"Experiment configured with {exp.factor_count} factors")
            print(f"Expected runs: {exp.num_runs}")
            print(f"Array type: {exp.array_type}")
            
            # Generate experimental runs
            runs = exp.generate()
            print(f"\nGenerated {len(runs)} experimental runs:")
            for run in runs:
                factors_str = ", ".join(f"{k}={v}" for k, v in run["factors"].items())
                print(f"  Run {run['run_id']}: {factors_str}")
            
            # Analyze results (with mock data for this example)
            with Analyzer(exp, metric_name="taste_score") as analyzer:
                # Simulate adding experimental results
                mock_results = {1: 8.2, 2: 7.1, 3: 9.3, 4: 6.8, 5: 8.9, 
                               6: 7.5, 7: 8.7, 8: 6.2, 9: 9.1}
                
                for run_id in range(1, len(runs) + 1):
                    if run_id in mock_results:
                        analyzer.add_result(run_id, mock_results[run_id])
                
                # Get main effects and recommendations
                effects = analyzer.main_effects()
                optimal = analyzer.recommend_optimal(higher_is_better=True)
                
                print(f"\nMain Effects Analysis:")
                for effect in effects:
                    print(f"  {effect['factor']}: range = {effect['range']:.3f}")
                
                print(f"\nOptimal Settings (for higher taste scores):")
                for factor, level in optimal.items():
                    print(f"  {factor}: {level}")
                    
    except Exception as e:
        print(f"Error: {e}")
        return False
    
    return True


def enhanced_usage_example():
    """Enhanced usage with configuration and validation."""
    print("\n=== Enhanced Usage Example ===")
    
    # Create custom configuration
    config = TaguchiConfig(
        cli_timeout=60,     # Longer timeout for complex operations
        debug_mode=True,    # Enable debug logging
        max_retries=2       # Retry failed commands
    )
    
    # Create Taguchi instance with custom config
    taguchi = Taguchi(config=config)
    
    try:
        # Use dependency injection for consistent configuration
        with Experiment(taguchi=taguchi) as exp:
            print("Adding factors with enhanced validation...")
            
            # Add factors (with automatic validation)
            exp.add_factor("temperature", ["low", "medium", "high"])
            exp.add_factor("pressure", ["low", "high"])
            exp.add_factor("catalyst", ["A", "B", "C"])
            
            # Enhanced validation
            if not exp.is_valid():
                print("Validation errors found:")
                for error in exp.validate():
                    print(f"  - {error}")
                return False
            
            print(f"✓ Experiment validation passed")
            print(f"✓ {exp.factor_count} factors, max {exp.max_levels} levels")
            
            # Enhanced analysis capabilities
            comparison = exp.compare_with_full_factorial()
            print(f"✓ Efficiency: {comparison['taguchi_runs']} runs vs "
                  f"{comparison['full_factorial_runs']} full factorial "
                  f"({comparison['percentage_reduction']:.1f}% reduction)")
            
            # Generate runs
            runs = exp.generate()
            print(f"✓ Generated {len(runs)} experimental runs")
            
            # Enhanced analyzer with dependency injection
            with Analyzer(exp, metric_name="yield", taguchi=taguchi) as analyzer:
                print("\nAdding results with enhanced validation...")
                
                # Enhanced result management
                mock_results = {}
                for i, run in enumerate(runs):
                    # Simulate experimental results
                    mock_results[run["run_id"]] = 70 + i * 3 + (i % 3) * 2
                
                analyzer.add_results_from_dict(mock_results)
                
                # Check completeness
                completeness = analyzer.check_completeness()
                print(f"✓ Results completeness: {completeness['completion_percentage']:.1f}%")
                
                if not analyzer.is_valid():
                    print("Validation errors found:")
                    for error in analyzer.validate():
                        print(f"  - {error}")
                    return False
                
                print(f"✓ Analyzer validation passed")
                
                # Enhanced analysis features
                print("\nEnhanced Analysis:")
                
                # Factor rankings by importance
                rankings = analyzer.get_factor_rankings(higher_is_better=True)
                print(f"Factor importance ranking:")
                for i, ranking in enumerate(rankings, 1):
                    print(f"  {i}. {ranking['factor']}: "
                          f"range={ranking['range']:.3f} "
                          f"(relative importance: {ranking['relative_importance']:.1%})")
                
                # Significant factors
                significant = analyzer.get_significant_factors(threshold=0.3)
                print(f"\nSignificant factors (>30% of max effect): {significant}")
                
                # Predict response for optimal settings
                optimal = analyzer.recommend_optimal(higher_is_better=True)
                prediction = analyzer.predict_response(optimal)
                print(f"\nPredicted yield with optimal settings: {prediction['predicted_response']:.2f}")
                
                # Enhanced summary
                print(f"\n{analyzer.summary()}")
                
    except Exception as e:
        print(f"Error: {e}")
        print(f"Error type: {type(e).__name__}")
        if hasattr(e, 'suggestions') and e.suggestions:
            print(f"Suggestions:")
            for suggestion in e.suggestions:
                print(f"  - {suggestion}")
        return False
    
    return True


def environment_configuration_example():
    """Example using environment variables for configuration."""
    print("\n=== Environment Configuration Example ===")
    
    # Set environment variables
    os.environ['TAGUCHI_DEBUG'] = 'true'
    os.environ['TAGUCHI_CLI_TIMEOUT'] = '90'
    os.environ['TAGUCHI_MAX_RETRIES'] = '3'
    
    try:
        # Load configuration from environment
        config = configure_from_environment()
        print(f"✓ Configuration loaded from environment:")
        print(f"  Debug mode: {config.debug_mode}")
        print(f"  CLI timeout: {config.cli_timeout}s")
        print(f"  Max retries: {config.max_retries}")
        
        # Use environment-based configuration
        taguchi = Taguchi(config=config)
        
        # Quick experiment
        with Experiment(taguchi=taguchi) as exp:
            exp.add_factor("factor_a", ["low", "high"])
            print(f"✓ Simple experiment configured successfully")
            
    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        # Clean up environment variables
        for var in ['TAGUCHI_DEBUG', 'TAGUCHI_CLI_TIMEOUT', 'TAGUCHI_MAX_RETRIES']:
            os.environ.pop(var, None)
    
    return True


def diagnostic_example():
    """Example of diagnostic and troubleshooting features."""
    print("\n=== Diagnostic Example ===")
    
    # Verify installation
    print("Checking Taguchi installation...")
    verification = verify_installation()
    
    if verification.get('cli_found', False):
        print(f"✓ CLI found at: {verification.get('cli_path')}")
        print(f"✓ CLI version: {verification.get('cli_version')}")
        print(f"✓ Test command: {'✓' if verification.get('test_command_successful') else '✗'}")
    else:
        print("✗ CLI not found")
        if 'errors' in verification:
            for error in verification['errors']:
                print(f"  Error: {error.get('message')}")
                if 'suggestions' in error:
                    for suggestion in error['suggestions']:
                        print(f"    Suggestion: {suggestion}")
    
    # Environment diagnostics
    from taguchi import diagnose_environment
    print("\nEnvironment diagnostics:")
    diagnostics = diagnose_environment()
    
    print(f"  Working directory: {diagnostics.get('working_directory')}")
    print(f"  Python version: {diagnostics.get('python_version')}")
    
    # Check PATH search results
    path_results = diagnostics.get('path_search_results', {})
    if 'PATH lookup' in path_results:
        path_lookup = path_results['PATH lookup']
        print(f"  CLI in PATH: {'✓' if path_lookup.get('found') else '✗'}")
        if path_lookup.get('found'):
            print(f"    Path: {path_lookup.get('path')}")


def error_handling_example():
    """Example of enhanced error handling."""
    print("\n=== Error Handling Example ===")
    
    try:
        # Example: Invalid factor name
        with Experiment() as exp:
            exp.add_factor("invalid name with spaces", ["low", "high"])
    except Exception as e:
        print(f"✓ Caught validation error: {type(e).__name__}")
        print(f"  Message: {e}")
        if hasattr(e, 'suggestions'):
            print(f"  Suggestions: {e.suggestions}")
    
    try:
        # Example: Invalid configuration
        config = TaguchiConfig(cli_timeout=-1)  # Invalid timeout
    except ValueError as e:
        print(f"✓ Caught configuration error: {e}")
    
    try:
        # Example: Binary not found (simulated)
        from taguchi.errors import BinaryDiscoveryError
        raise BinaryDiscoveryError(
            searched_paths=["/usr/bin/taguchi", "/usr/local/bin/taguchi"],
            cli_path_env=None,
            path_env="/usr/bin:/usr/local/bin"
        )
    except BinaryDiscoveryError as e:
        print(f"✓ Caught binary discovery error")
        print(f"  Searched paths included in error message: {'searched paths' in str(e).lower()}")
        print(f"  Suggestions provided: {len(e.suggestions) > 0}")


if __name__ == "__main__":
    print("Taguchi Enhanced Python Bindings - Example Usage")
    print("=" * 50)
    
    # Run all examples
    examples = [
        ("Basic Usage", basic_usage_example),
        ("Enhanced Usage", enhanced_usage_example),
        ("Environment Configuration", environment_configuration_example), 
        ("Diagnostics", diagnostic_example),
        ("Error Handling", error_handling_example),
    ]
    
    results = {}
    for name, example_func in examples:
        try:
            print(f"\n{'='*20} {name} {'='*20}")
            success = example_func()
            results[name] = success
            if success is not False:  # None or True
                print(f"✓ {name} completed successfully")
            else:
                print(f"✗ {name} failed")
        except Exception as e:
            print(f"✗ {name} failed with exception: {e}")
            results[name] = False
    
    # Summary
    print(f"\n{'='*50}")
    print("Summary:")
    for name, success in results.items():
        status = "✓" if success is not False else "✗"
        print(f"  {status} {name}")
    
    successful = sum(1 for success in results.values() if success is not False)
    total = len(results)
    print(f"\nCompleted: {successful}/{total} examples")