#!/usr/bin/env python3
"""
Troubleshooting Guide

This example demonstrates how to use the enhanced diagnostic features
to troubleshoot common issues with the Taguchi Python bindings.
"""

import os
import sys
from pathlib import Path

# Add taguchi package to path for examples
sys.path.insert(0, str(Path(__file__).parent.parent))

from taguchi import (
    verify_installation, diagnose_environment, TaguchiConfig, Taguchi,
    BinaryDiscoveryError, ValidationError, TaguchiError
)


def check_installation():
    """Comprehensive installation check."""
    print("=== Installation Check ===")
    
    try:
        # Step 1: Basic installation verification
        print("1. Checking basic installation...")
        status = verify_installation()
        
        if status.get('cli_found'):
            print(f"✓ CLI found at: {status.get('cli_path')}")
            print(f"✓ CLI executable: {status.get('cli_executable')}")
            print(f"✓ CLI version: {status.get('cli_version')}")
            print(f"✓ Test command successful: {status.get('test_command_successful')}")
            
            if status.get('errors'):
                print("⚠ Issues found:")
                for error in status['errors']:
                    print(f"  - {error.get('message')}")
            
        else:
            print("✗ CLI not found")
            print("\nTroubleshooting steps:")
            
            if 'errors' in status:
                for error in status['errors']:
                    print(f"\nError: {error.get('message')}")
                    if 'suggestions' in error:
                        print("Suggestions:")
                        for suggestion in error['suggestions']:
                            print(f"  - {suggestion}")
        
        # Step 2: Environment diagnostics
        print("\n2. Environment diagnostics...")
        diagnostics = diagnose_environment()
        
        print(f"Working directory: {diagnostics.get('working_directory')}")
        print(f"Python version: {diagnostics.get('python_version')}")
        print(f"Platform: {diagnostics.get('platform')}")
        
        # Check environment variables
        env_vars = diagnostics.get('environment_variables', {})
        print("\nEnvironment variables:")
        for var, value in env_vars.items():
            if var.startswith('TAGUCHI_'):
                print(f"  {var}: {value}")
        
        # Check PATH search results
        path_results = diagnostics.get('path_search_results', {})
        print("\nBinary search results:")
        for location, info in path_results.items():
            if isinstance(info, dict):
                exists = info.get('exists', info.get('found', False))
                executable = info.get('executable', False)
                path = info.get('path') or info.get('absolute')
                
                status_icon = "✓" if exists and executable else "✗"
                print(f"  {status_icon} {location}: {path}")
        
        return status.get('cli_found', False)
        
    except Exception as e:
        print(f"✗ Installation check failed: {e}")
        return False


def troubleshoot_configuration():
    """Troubleshoot configuration issues."""
    print("\n=== Configuration Troubleshooting ===")
    
    # Test 1: Valid configuration
    print("1. Testing valid configuration...")
    try:
        config = TaguchiConfig(cli_timeout=30, debug_mode=False)
        print(f"✓ Valid configuration created")
        print(f"  Timeout: {config.cli_timeout}s")
        print(f"  Debug: {config.debug_mode}")
    except Exception as e:
        print(f"✗ Valid configuration failed: {e}")
    
    # Test 2: Invalid configurations
    print("\n2. Testing invalid configurations...")
    invalid_configs = [
        ("Negative timeout", {"cli_timeout": -1}),
        ("Non-existent path", {"cli_path": "/does/not/exist"}),
        ("Invalid retry count", {"max_retries": -1}),
        ("Invalid retry delay", {"retry_delay": -1.0}),
    ]
    
    for desc, config_kwargs in invalid_configs:
        try:
            TaguchiConfig(**config_kwargs)
            print(f"✗ {desc}: Should have failed but didn't")
        except ValueError as e:
            print(f"✓ {desc}: Correctly rejected - {e}")
        except Exception as e:
            print(f"? {desc}: Unexpected error - {e}")
    
    # Test 3: Environment configuration
    print("\n3. Testing environment configuration...")
    
    # Set test environment variables
    test_env = {
        'TAGUCHI_CLI_TIMEOUT': '60',
        'TAGUCHI_DEBUG': 'true',
        'TAGUCHI_MAX_RETRIES': '2'
    }
    
    original_env = {}
    for key, value in test_env.items():
        original_env[key] = os.environ.get(key)
        os.environ[key] = value
    
    try:
        config = TaguchiConfig.from_environment()
        print(f"✓ Environment configuration loaded:")
        print(f"  Timeout: {config.cli_timeout}s (from TAGUCHI_CLI_TIMEOUT)")
        print(f"  Debug: {config.debug_mode} (from TAGUCHI_DEBUG)")
        print(f"  Retries: {config.max_retries} (from TAGUCHI_MAX_RETRIES)")
        
    except Exception as e:
        print(f"✗ Environment configuration failed: {e}")
    
    finally:
        # Restore original environment
        for key, original_value in original_env.items():
            if original_value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = original_value


def troubleshoot_binary_discovery():
    """Troubleshoot binary discovery issues."""
    print("\n=== Binary Discovery Troubleshooting ===")
    
    # Test 1: Standard discovery
    print("1. Testing standard binary discovery...")
    try:
        taguchi = Taguchi()
        print(f"✓ CLI found via standard discovery: {taguchi._cli_path}")
    except BinaryDiscoveryError as e:
        print("✗ Standard discovery failed")
        print(f"Error: {e}")
        print("\nThis error includes detailed troubleshooting information:")
        print("- List of searched paths")
        print("- Environment variable status")
        print("- Actionable suggestions")
        
    except Exception as e:
        print(f"✗ Unexpected error during discovery: {e}")
    
    # Test 2: Environment variable override
    print("\n2. Testing environment variable override...")
    
    # Create a fake binary file for testing
    fake_binary_path = None
    try:
        import tempfile
        with tempfile.NamedTemporaryFile(delete=False, mode='w') as f:
            f.write("#!/bin/bash\necho 'fake taguchi'\n")
            fake_binary_path = f.name
        
        # Make it executable
        os.chmod(fake_binary_path, 0o755)
        
        # Test with environment variable
        original_path = os.environ.get('TAGUCHI_CLI_PATH')
        os.environ['TAGUCHI_CLI_PATH'] = fake_binary_path
        
        try:
            taguchi = Taguchi()
            discovered_path = str(Path(taguchi._cli_path).resolve())
            expected_path = str(Path(fake_binary_path).resolve())
            
            if discovered_path == expected_path:
                print(f"✓ Environment variable override works: {fake_binary_path}")
            else:
                print(f"✗ Environment variable override failed:")
                print(f"  Expected: {expected_path}")
                print(f"  Got: {discovered_path}")
                
        finally:
            if original_path is None:
                os.environ.pop('TAGUCHI_CLI_PATH', None)
            else:
                os.environ['TAGUCHI_CLI_PATH'] = original_path
                
    except Exception as e:
        print(f"✗ Environment variable test failed: {e}")
        
    finally:
        if fake_binary_path and os.path.exists(fake_binary_path):
            try:
                os.unlink(fake_binary_path)
            except OSError:
                pass


def troubleshoot_validation():
    """Troubleshoot validation issues."""
    print("\n=== Validation Troubleshooting ===")
    
    from taguchi import Experiment, Analyzer
    
    # Test 1: Experiment validation
    print("1. Testing experiment validation...")
    
    try:
        with Experiment() as exp:
            # Test invalid factor name
            try:
                exp.add_factor("invalid name with spaces", ["low", "high"])
                print("✗ Invalid factor name was accepted")
            except ValidationError as e:
                print("✓ Invalid factor name correctly rejected")
                print(f"  Error: {e}")
                if hasattr(e, 'suggestions'):
                    print(f"  Suggestions: {e.suggestions}")
            
            # Test valid factors
            exp.add_factor("temperature", ["low", "medium", "high"])
            exp.add_factor("pressure", ["low", "high"])
            
            # Test validation
            errors = exp.validate()
            if errors:
                print(f"✗ Validation failed with {len(errors)} errors:")
                for error in errors:
                    print(f"  - {error}")
            else:
                print("✓ Experiment validation passed")
                
    except Exception as e:
        print(f"✗ Experiment validation test failed: {e}")
    
    # Test 2: Analyzer validation
    print("\n2. Testing analyzer validation...")
    
    try:
        # Create a mock experiment
        class MockExperiment:
            def generate(self):
                return [
                    {"run_id": 1, "factors": {"temp": "low"}},
                    {"run_id": 2, "factors": {"temp": "high"}}
                ]
            
            @property
            def factors(self):
                return {"temp": ["low", "high"]}
            
            def get_tgu_path(self):
                return "/tmp/mock.tgu"
        
        mock_exp = MockExperiment()
        
        with Analyzer(mock_exp, metric_name="response") as analyzer:
            # Test invalid metric name
            analyzer._metric_name = ""  # Directly set invalid name
            errors = analyzer.validate()
            if "Metric name cannot be empty" in str(errors):
                print("✓ Invalid metric name correctly detected")
            else:
                print("✗ Invalid metric name not detected")
            
            # Reset to valid name
            analyzer._metric_name = "response"
            
            # Test missing results
            errors = analyzer.validate()
            if any("Missing results" in error for error in errors):
                print("✓ Missing results correctly detected")
            else:
                print("✗ Missing results not detected")
            
            # Add results
            analyzer.add_result(1, 1.0)
            analyzer.add_result(2, 1.1)
            
            # Test complete validation
            errors = analyzer.validate()
            if not errors:
                print("✓ Complete analyzer validation passed")
            else:
                print(f"✗ Analyzer validation failed: {errors}")
                
    except Exception as e:
        print(f"✗ Analyzer validation test failed: {e}")


def troubleshoot_common_errors():
    """Show how to handle common error scenarios."""
    print("\n=== Common Error Scenarios ===")
    
    # Scenario 1: CLI not found
    print("1. CLI not found scenario:")
    print("   Error: BinaryDiscoveryError")
    print("   Solutions:")
    print("   - Set TAGUCHI_CLI_PATH environment variable")
    print("   - Add taguchi binary to PATH")
    print("   - Build/install taguchi CLI")
    print("   - Check binary permissions (must be executable)")
    
    # Scenario 2: Command timeout
    print("\n2. Command timeout scenario:")
    print("   Error: TimeoutError")
    print("   Solutions:")
    print("   - Increase TAGUCHI_CLI_TIMEOUT (default: 30s)")
    print("   - Use TaguchiConfig(cli_timeout=120) for large arrays")
    print("   - Check system resources and load")
    print("   - Verify .tgu file is not malformed")
    
    # Scenario 3: Invalid configuration
    print("\n3. Invalid configuration scenario:")
    print("   Error: ValueError during TaguchiConfig creation")
    print("   Solutions:")
    print("   - Check timeout is positive")
    print("   - Verify paths exist")
    print("   - Check retry settings are non-negative")
    
    # Scenario 4: Validation errors
    print("\n4. Validation error scenario:")
    print("   Error: ValidationError")
    print("   Solutions:")
    print("   - Check factor names (no spaces, special characters)")
    print("   - Verify factor levels are strings")
    print("   - Ensure no duplicate levels")
    print("   - Check array compatibility (factors vs columns, levels)")
    
    # Scenario 5: File not found
    print("\n5. File not found scenario:")
    print("   Error: CommandExecutionError with 'file not found'")
    print("   Solutions:")
    print("   - Check .tgu file path is correct")
    print("   - Verify file permissions")
    print("   - Ensure temporary files aren't deleted prematurely")


def run_diagnostic_workflow():
    """Run a complete diagnostic workflow."""
    print("\n=== Complete Diagnostic Workflow ===")
    
    diagnostics = {
        "installation": False,
        "configuration": False,
        "binary_discovery": False,
        "validation": False
    }
    
    print("Running comprehensive diagnostics...")
    
    # Check each component
    try:
        print("\n1/4 Checking installation...")
        diagnostics["installation"] = check_installation()
        
        print("\n2/4 Checking configuration...")
        troubleshoot_configuration()
        diagnostics["configuration"] = True
        
        print("\n3/4 Checking binary discovery...")
        troubleshoot_binary_discovery()
        diagnostics["binary_discovery"] = True
        
        print("\n4/4 Checking validation...")
        troubleshoot_validation()
        diagnostics["validation"] = True
        
    except Exception as e:
        print(f"Diagnostic workflow interrupted: {e}")
    
    # Summary
    print(f"\n{'='*40}")
    print("Diagnostic Summary:")
    for component, status in diagnostics.items():
        icon = "✓" if status else "✗"
        print(f"  {icon} {component.replace('_', ' ').title()}")
    
    working_components = sum(diagnostics.values())
    total_components = len(diagnostics)
    
    if working_components == total_components:
        print(f"\n✓ All components working ({working_components}/{total_components})")
        print("The Taguchi Python bindings are ready to use!")
    else:
        print(f"\n⚠ Some issues found ({working_components}/{total_components} working)")
        print("Review the troubleshooting information above.")


if __name__ == "__main__":
    print("Taguchi Python Bindings - Troubleshooting Guide")
    print("=" * 50)
    
    print("This guide helps diagnose and resolve common issues.")
    print("Each section provides specific troubleshooting steps.")
    
    # Run the complete diagnostic workflow
    run_diagnostic_workflow()
    
    # Show common error information
    troubleshoot_common_errors()
    
    print(f"\n{'='*50}")
    print("For additional help:")
    print("- Check the documentation")
    print("- Review environment variables (TAGUCHI_*)")
    print("- Use verify_installation() and diagnose_environment()")
    print("- Enable debug mode for detailed logging")