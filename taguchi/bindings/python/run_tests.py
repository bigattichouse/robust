#!/usr/bin/env python3
"""
Test runner for enhanced Taguchi Python bindings.

This script runs the comprehensive test suite including unit tests,
integration tests, and compatibility tests.
"""

import sys
import subprocess
import os
from pathlib import Path


def run_command(cmd, description):
    """Run a command and report results."""
    print(f"\n{'='*60}")
    print(f"Running: {description}")
    print(f"Command: {cmd}")
    print('='*60)
    
    try:
        result = subprocess.run(
            cmd, 
            shell=True, 
            capture_output=True, 
            text=True,
            cwd=Path(__file__).parent
        )
        
        if result.stdout:
            print("STDOUT:")
            print(result.stdout)
        
        if result.stderr:
            print("STDERR:")
            print(result.stderr)
        
        if result.returncode == 0:
            print(f"✓ {description} PASSED")
            return True
        else:
            print(f"✗ {description} FAILED (exit code: {result.returncode})")
            return False
            
    except Exception as e:
        print(f"✗ {description} FAILED (exception: {e})")
        return False


def check_prerequisites():
    """Check that required tools are available."""
    print("Checking prerequisites...")
    
    # Check Python version
    if sys.version_info < (3, 7):
        print(f"✗ Python 3.7+ required, found {sys.version_info.major}.{sys.version_info.minor}")
        return False
    else:
        print(f"✓ Python {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}")
    
    # Check pytest
    try:
        import pytest
        print(f"✓ pytest available (version: {pytest.__version__})")
    except ImportError:
        print("✗ pytest not found. Install with: pip install pytest")
        return False
    
    # Check if package can be imported
    try:
        sys.path.insert(0, str(Path(__file__).parent))
        import taguchi
        print(f"✓ taguchi package importable (version: {taguchi.__version__})")
    except ImportError as e:
        print(f"✗ Cannot import taguchi package: {e}")
        return False
    
    return True


def run_unit_tests():
    """Run unit tests."""
    tests = [
        ("tests/test_config.py", "Configuration tests"),
        ("tests/test_errors.py", "Error handling tests"),  
        ("tests/test_core_enhanced.py", "Core functionality tests"),
        ("tests/test_experiment_enhanced.py", "Experiment tests"),
        ("tests/test_analyzer_enhanced.py", "Analyzer tests"),
    ]
    
    results = []
    for test_file, description in tests:
        if Path(test_file).exists():
            cmd = f"python -m pytest {test_file} -v"
            success = run_command(cmd, description)
            results.append((description, success))
        else:
            print(f"⚠ Skipping {description} - file not found: {test_file}")
            results.append((description, None))
    
    return results


def run_integration_tests():
    """Run integration tests."""
    test_file = "tests/test_integration_enhanced.py"
    
    if Path(test_file).exists():
        cmd = f"python -m pytest {test_file} -v"
        success = run_command(cmd, "Integration tests")
        return [("Integration tests", success)]
    else:
        print(f"⚠ Skipping integration tests - file not found: {test_file}")
        return [("Integration tests", None)]


def run_backward_compatibility_tests():
    """Run backward compatibility tests."""
    # Test that original imports still work
    compatibility_test = """
import sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd()))

try:
    # Test original imports
    from taguchi import Taguchi, TaguchiError, Experiment, Analyzer
    print("✓ Original imports work")
    
    # Test that classes are the enhanced versions but compatible
    taguchi = Taguchi.__name__
    experiment = Experiment.__name__ 
    analyzer = Analyzer.__name__
    print(f"✓ Classes available: {taguchi}, {experiment}, {analyzer}")
    
    # Test that original classes are accessible
    from taguchi import OriginalTaguchi, OriginalExperiment, OriginalAnalyzer
    print("✓ Original classes accessible via OriginalXxx")
    
    # Test exception compatibility
    from taguchi.core import TaguchiError as OriginalTaguchiError
    from taguchi import TaguchiError as EnhancedTaguchiError
    
    # Enhanced error should be instance of original
    try:
        raise EnhancedTaguchiError("test")
    except OriginalTaguchiError:
        print("✓ Enhanced TaguchiError is instance of original")
    except:
        print("✗ Enhanced TaguchiError not compatible with original")
        sys.exit(1)
    
    print("✓ All backward compatibility checks passed")
    
except Exception as e:
    print(f"✗ Backward compatibility failed: {e}")
    sys.exit(1)
"""
    
    success = run_command(f"python -c \"{compatibility_test}\"", "Backward compatibility")
    return [("Backward compatibility", success)]


def run_example_tests():
    """Test that examples run without errors."""
    examples = [
        ("examples/enhanced_basic_usage.py", "Enhanced basic usage example"),
        ("examples/async_usage_example.py", "Async usage example"),
        ("examples/troubleshooting_guide.py", "Troubleshooting guide"),
    ]
    
    results = []
    for example_file, description in examples:
        if Path(example_file).exists():
            # Examples might not have CLI available, so we just check they import and run
            cmd = f"python {example_file}"
            success = run_command(cmd, description)
            results.append((description, success))
        else:
            print(f"⚠ Skipping {description} - file not found: {example_file}")
            results.append((description, None))
    
    return results


def run_linting():
    """Run code quality checks if tools available."""
    linting_results = []
    
    # Check flake8
    try:
        import flake8
        cmd = "python -m flake8 taguchi/ --max-line-length=100 --ignore=E501,W503"
        success = run_command(cmd, "Flake8 linting")
        linting_results.append(("Flake8", success))
    except ImportError:
        print("⚠ Skipping flake8 - not installed")
        linting_results.append(("Flake8", None))
    
    # Check mypy
    try:
        import mypy
        cmd = "python -m mypy taguchi/ --ignore-missing-imports"
        success = run_command(cmd, "MyPy type checking")
        linting_results.append(("MyPy", success))
    except ImportError:
        print("⚠ Skipping mypy - not installed") 
        linting_results.append(("MyPy", None))
    
    return linting_results


def print_summary(all_results):
    """Print a summary of all test results."""
    print(f"\n{'='*80}")
    print("TEST SUMMARY")
    print('='*80)
    
    total_tests = 0
    passed_tests = 0
    failed_tests = 0
    skipped_tests = 0
    
    for category, results in all_results.items():
        print(f"\n{category}:")
        for test_name, result in results:
            total_tests += 1
            if result is True:
                print(f"  ✓ {test_name}")
                passed_tests += 1
            elif result is False:
                print(f"  ✗ {test_name}")
                failed_tests += 1
            else:
                print(f"  ⚠ {test_name} (skipped)")
                skipped_tests += 1
    
    print(f"\n{'='*80}")
    print(f"TOTAL: {total_tests} tests")
    print(f"PASSED: {passed_tests}")
    print(f"FAILED: {failed_tests}")
    print(f"SKIPPED: {skipped_tests}")
    
    if failed_tests == 0:
        print("\n🎉 ALL TESTS PASSED! 🎉")
        return True
    else:
        print(f"\n❌ {failed_tests} TESTS FAILED")
        return False


def main():
    """Main test runner function."""
    print("Taguchi Python Bindings - Enhanced Test Suite")
    print("=" * 80)
    
    # Check prerequisites
    if not check_prerequisites():
        print("Prerequisites not met. Exiting.")
        sys.exit(1)
    
    # Run all test categories
    all_results = {}
    
    print(f"\n{'='*80}")
    print("RUNNING TEST SUITE")
    print('='*80)
    
    # Unit tests
    print("\n📋 Running unit tests...")
    all_results["Unit Tests"] = run_unit_tests()
    
    # Integration tests
    print("\n🔗 Running integration tests...")
    all_results["Integration Tests"] = run_integration_tests()
    
    # Backward compatibility
    print("\n🔄 Running backward compatibility tests...")
    all_results["Backward Compatibility"] = run_backward_compatibility_tests()
    
    # Examples
    print("\n📖 Testing examples...")
    all_results["Examples"] = run_example_tests()
    
    # Code quality
    print("\n🔍 Running code quality checks...")
    all_results["Code Quality"] = run_linting()
    
    # Print summary and determine exit code
    success = print_summary(all_results)
    
    if success:
        print("\n✨ Enhanced Taguchi Python bindings are ready for use! ✨")
        sys.exit(0)
    else:
        print("\n💥 Some tests failed. Please review the output above.")
        sys.exit(1)


if __name__ == "__main__":
    main()