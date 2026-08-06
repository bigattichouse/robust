"""
Unit tests for enhanced error handling.
"""

import subprocess
import pytest
from unittest.mock import Mock

from taguchi.errors import (
    TaguchiError, BinaryDiscoveryError, CommandExecutionError,
    TimeoutError, ValidationError
)


class TestTaguchiError:
    """Test the enhanced TaguchiError class."""
    
    def test_basic_error(self):
        """Test basic error creation."""
        error = TaguchiError("Basic error message")
        
        assert str(error) == "Basic error message"
        assert error.operation is None
        assert error.command is None
        assert error.suggestions == []
    
    def test_error_with_context(self):
        """Test error with full context."""
        error = TaguchiError(
            "Command failed",
            operation="test_operation",
            command=["taguchi", "list-arrays"],
            exit_code=1,
            stdout="Some output",
            stderr="Error details",
            working_directory="/tmp",
            cli_path="/usr/bin/taguchi",
            suggestions=["Check installation", "Verify permissions"],
            diagnostic_info={"version": "1.5.0", "config": "debug"}
        )
        
        error_str = str(error)
        assert "Command failed" in error_str
        assert "Operation: test_operation" in error_str
        assert "Command: taguchi list-arrays" in error_str
        assert "Exit code: 1" in error_str
        assert "Working directory: /tmp" in error_str
        assert "CLI path: /usr/bin/taguchi" in error_str
        assert "stderr output:" in error_str
        assert "Error details" in error_str
        assert "stdout output:" in error_str
        assert "Some output" in error_str
        assert "Suggested solutions:" in error_str
        assert "Check installation" in error_str
        assert "Verify permissions" in error_str
        assert "Diagnostic information:" in error_str
        assert "version: 1.5.0" in error_str
        assert "config: debug" in error_str
    
    def test_add_suggestion(self):
        """Test adding suggestions to an error."""
        error = TaguchiError("Base error")
        
        error.add_suggestion("First suggestion")
        error.add_suggestion("Second suggestion")
        
        assert len(error.suggestions) == 2
        assert "First suggestion" in error.suggestions
        assert "Second suggestion" in error.suggestions
        
        # Should be included in string representation
        error_str = str(error)
        assert "Suggested solutions:" in error_str
        assert "First suggestion" in error_str
        assert "Second suggestion" in error_str
    
    def test_add_diagnostic_info(self):
        """Test adding diagnostic information."""
        error = TaguchiError("Base error")
        
        error.add_diagnostic_info("key1", "value1")
        error.add_diagnostic_info("key2", 42)
        
        assert error.diagnostic_info["key1"] == "value1"
        assert error.diagnostic_info["key2"] == 42
        
        # Should be included in string representation
        error_str = str(error)
        assert "Diagnostic information:" in error_str
        assert "key1: value1" in error_str
        assert "key2: 42" in error_str
    
    def test_indent_text(self):
        """Test text indentation utility."""
        text = "Line 1\nLine 2\nLine 3"
        indented = TaguchiError._indent_text(text)
        
        expected = "  Line 1\n  Line 2\n  Line 3"
        assert indented == expected
        
        # Test custom indent
        custom_indented = TaguchiError._indent_text(text, ">>> ")
        expected_custom = ">>> Line 1\n>>> Line 2\n>>> Line 3"
        assert custom_indented == expected_custom


class TestBinaryDiscoveryError:
    """Test the BinaryDiscoveryError class."""
    
    def test_basic_discovery_error(self):
        """Test basic binary discovery error."""
        searched_paths = [
            "/usr/bin/taguchi",
            "/usr/local/bin/taguchi",
            "/home/user/taguchi/build/taguchi"
        ]
        
        error = BinaryDiscoveryError(
            searched_paths=searched_paths,
            cli_path_env="/custom/path/taguchi",
            path_env="/usr/bin:/usr/local/bin"
        )
        
        error_str = str(error)
        assert "Could not find taguchi CLI binary" in error_str
        assert "Searched paths:" in error_str
        
        # Check that all paths are listed
        for path in searched_paths:
            assert path in error_str
        
        # Check suggestions are included
        assert "Set TAGUCHI_CLI_PATH environment variable" in error_str
        assert "Add taguchi binary to your PATH" in error_str
        assert "Build with: cd ~/workspace/taguchi && make" in error_str
        
        # Check diagnostic info
        assert error.diagnostic_info["TAGUCHI_CLI_PATH"] == "/custom/path/taguchi"
        assert error.diagnostic_info["PATH"] == "/usr/bin:/usr/local/bin"
        assert error.diagnostic_info["searched_paths_count"] == 3
    
    def test_discovery_error_without_env_vars(self):
        """Test discovery error when environment variables not set."""
        error = BinaryDiscoveryError(
            searched_paths=["/usr/bin/taguchi"],
            cli_path_env=None,
            path_env=None
        )
        
        assert error.diagnostic_info["TAGUCHI_CLI_PATH"] is None
        assert error.diagnostic_info["PATH"] is None


class TestCommandExecutionError:
    """Test the CommandExecutionError class."""
    
    def test_from_subprocess_error_basic(self):
        """Test creating error from basic subprocess failure."""
        # Mock subprocess result
        result = Mock()
        result.returncode = 1
        result.stdout = "Command output"
        result.stderr = "Error message"
        
        cmd = ["taguchi", "list-arrays"]
        
        error = CommandExecutionError.from_subprocess_error(
            cmd=cmd,
            result=result,
            operation="test_operation",
            working_directory="/tmp",
            cli_path="/usr/bin/taguchi"
        )
        
        assert "Command failed with exit code 1" in str(error)
        assert error.operation == "test_operation"
        assert error.command == cmd
        assert error.exit_code == 1
        assert error.stdout == "Command output"
        assert error.stderr == "Error message"
        assert error.working_directory == "/tmp"
        assert error.cli_path == "/usr/bin/taguchi"
    
    def test_command_not_found_error(self):
        """Test handling of 'command not found' error."""
        result = Mock()
        result.returncode = 127
        result.stdout = ""
        result.stderr = "command not found: taguchi"
        
        error = CommandExecutionError.from_subprocess_error(
            cmd=["taguchi", "list-arrays"],
            result=result
        )
        
        # Should include specific suggestions for command not found
        suggestions_str = " ".join(error.suggestions)
        assert "Verify the CLI binary exists" in suggestions_str
        assert "Check TAGUCHI_CLI_PATH" in suggestions_str
        assert "Ensure taguchi is installed" in suggestions_str
    
    def test_file_not_found_error(self):
        """Test handling of 'file not found' error."""
        result = Mock()
        result.returncode = 1
        result.stdout = ""
        result.stderr = "Error: file not found: experiment.tgu"
        
        error = CommandExecutionError.from_subprocess_error(
            cmd=["taguchi", "generate", "experiment.tgu"],
            result=result
        )
        
        # Should include file-specific suggestions
        suggestions_str = " ".join(error.suggestions)
        assert "Verify the .tgu file path is correct" in suggestions_str
        assert "Ensure the .tgu file exists" in suggestions_str
        assert "Check file permissions" in suggestions_str
    
    def test_invalid_format_error(self):
        """Test handling of invalid format error."""
        result = Mock()
        result.returncode = 1
        result.stdout = ""
        result.stderr = "Error: invalid factor definition in line 5"
        
        error = CommandExecutionError.from_subprocess_error(
            cmd=["taguchi", "generate", "experiment.tgu"],
            result=result
        )
        
        # Should include format-specific suggestions
        suggestions_str = " ".join(error.suggestions)
        assert "Check .tgu file format" in suggestions_str
        assert "Verify factor names and levels" in suggestions_str
        assert "Review the experiment configuration" in suggestions_str
    
    def test_sigkill_error(self):
        """Test handling of SIGKILL (likely timeout)."""
        result = Mock()
        result.returncode = -9
        result.stdout = ""
        result.stderr = ""
        
        error = CommandExecutionError.from_subprocess_error(
            cmd=["taguchi", "generate", "large_experiment.tgu"],
            result=result
        )
        
        # Should include timeout-specific suggestions
        suggestions_str = " ".join(error.suggestions)
        assert "Increase CLI timeout" in suggestions_str
        assert "TAGUCHI_CLI_TIMEOUT" in suggestions_str
        assert "large arrays" in suggestions_str


class TestTimeoutError:
    """Test the TimeoutError class."""
    
    def test_timeout_error_creation(self):
        """Test timeout error creation."""
        cmd = ["taguchi", "generate", "large_experiment.tgu"]
        
        error = TimeoutError(
            cmd=cmd,
            timeout=30,
            operation="experiment_generation",
            working_directory="/tmp",
            cli_path="/usr/bin/taguchi"
        )
        
        error_str = str(error)
        assert "Command timed out after 30 seconds" in error_str
        assert error.operation == "experiment_generation"
        assert error.command == cmd
        
        # Check timeout-specific suggestions
        suggestions_str = " ".join(error.suggestions)
        assert "Increase timeout" in suggestions_str
        assert "current: 30s" in suggestions_str
        assert "TAGUCHI_CLI_TIMEOUT" in suggestions_str
        assert "60s or more" in suggestions_str
        assert "system resources" in suggestions_str


class TestValidationError:
    """Test the ValidationError class."""
    
    def test_single_validation_error(self):
        """Test single validation error."""
        error = ValidationError(
            ["Factor name cannot be empty"],
            operation="factor_validation"
        )
        
        error_str = str(error)
        assert "Validation failed: Factor name cannot be empty" in error_str
        assert error.operation == "factor_validation"
    
    def test_multiple_validation_errors(self):
        """Test multiple validation errors."""
        errors = [
            "Factor name cannot be empty",
            "Factor levels must be strings",
            "Duplicate level found: 'high'"
        ]
        
        error = ValidationError(errors, operation="experiment_validation")
        
        error_str = str(error)
        assert "Validation failed:" in error_str
        assert "Factor name cannot be empty" in error_str
        assert "Factor levels must be strings" in error_str
        assert "Duplicate level found" in error_str
        
        # Should include documentation suggestion
        suggestions_str = " ".join(error.suggestions)
        assert "Review the documentation" in suggestions_str


class TestErrorInheritance:
    """Test error inheritance and polymorphism."""
    
    def test_all_errors_are_taguchi_errors(self):
        """Test that all custom errors inherit from TaguchiError."""
        # Create instances of all error types
        base_error = TaguchiError("Base error")
        discovery_error = BinaryDiscoveryError(["/usr/bin/taguchi"])
        
        result = Mock()
        result.returncode = 1
        result.stdout = ""
        result.stderr = ""
        execution_error = CommandExecutionError.from_subprocess_error(
            ["taguchi"], result
        )
        
        timeout_error = TimeoutError(["taguchi"], 30)
        validation_error = ValidationError(["Invalid input"])
        
        # All should be instances of TaguchiError
        assert isinstance(base_error, TaguchiError)
        assert isinstance(discovery_error, TaguchiError)
        assert isinstance(execution_error, TaguchiError)
        assert isinstance(timeout_error, TaguchiError)
        assert isinstance(validation_error, TaguchiError)
        
        # All should be catchable as TaguchiError
        errors = [
            base_error, discovery_error, execution_error,
            timeout_error, validation_error
        ]
        
        for error in errors:
            try:
                raise error
            except TaguchiError as e:
                assert e is error
            except Exception:
                pytest.fail(f"Error {type(error)} not caught as TaguchiError")
    
    def test_error_message_consistency(self):
        """Test that all errors produce useful string representations."""
        errors = [
            TaguchiError("Basic error"),
            BinaryDiscoveryError(["/usr/bin/taguchi"]),
            TimeoutError(["taguchi"], 30),
            ValidationError(["Invalid input"]),
        ]
        
        for error in errors:
            error_str = str(error)
            assert len(error_str) > 10  # Should have substantial content
            assert error_str.strip()  # Should not be just whitespace
            
            # All should mention what went wrong
            assert any(word in error_str.lower() for word in [
                "error", "failed", "invalid", "timeout", "not found"
            ])