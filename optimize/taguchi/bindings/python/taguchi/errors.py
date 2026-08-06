"""
Enhanced error handling for Taguchi Python bindings.

Provides rich error context, operation tracking, and actionable error messages
for better debugging experience.
"""

import os
import subprocess
import traceback
from typing import List, Optional, Dict, Any, Union


class TaguchiError(Exception):
    """
    Enhanced exception for Taguchi library errors.
    
    Provides rich context including operation details, command information,
    and diagnostic data for easier debugging.
    """
    
    def __init__(
        self,
        message: str,
        *,
        operation: Optional[str] = None,
        command: Optional[List[str]] = None,
        exit_code: Optional[int] = None,
        stdout: Optional[str] = None,
        stderr: Optional[str] = None,
        working_directory: Optional[str] = None,
        cli_path: Optional[str] = None,
        suggestions: Optional[List[str]] = None,
        diagnostic_info: Optional[Dict[str, Any]] = None,
    ):
        """
        Create a TaguchiError with rich context.
        
        Args:
            message: Primary error message
            operation: High-level operation that failed (e.g., "experiment_generation")
            command: Full command that was executed
            exit_code: Process exit code if applicable
            stdout: Command stdout output
            stderr: Command stderr output  
            working_directory: Directory where command was executed
            cli_path: Path to CLI binary that was used
            suggestions: List of suggested solutions
            diagnostic_info: Additional diagnostic information
        """
        self.operation = operation
        self.command = command
        self.exit_code = exit_code
        self.stdout = stdout
        self.stderr = stderr
        self.working_directory = working_directory
        self.cli_path = cli_path
        self.suggestions = suggestions or []
        self.diagnostic_info = diagnostic_info or {}
        
        # Build comprehensive error message
        full_message = self._build_error_message(message)
        super().__init__(full_message)
    
    def _build_error_message(self, base_message: str) -> str:
        """Build a comprehensive error message with context."""
        lines = [base_message]
        
        # Add operation context
        if self.operation:
            lines.append(f"Operation: {self.operation}")
        
        # Add command details
        if self.command:
            lines.append(f"Command: {' '.join(self.command)}")
        
        if self.exit_code is not None:
            lines.append(f"Exit code: {self.exit_code}")
        
        if self.working_directory:
            lines.append(f"Working directory: {self.working_directory}")
        
        if self.cli_path:
            lines.append(f"CLI path: {self.cli_path}")
        
        # Add command output
        if self.stderr and self.stderr.strip():
            lines.extend([
                "stderr output:",
                self._indent_text(self.stderr.strip()),
            ])
        
        if self.stdout and self.stdout.strip():
            lines.extend([
                "stdout output:",
                self._indent_text(self.stdout.strip()),
            ])
        
        # Add suggestions
        if self.suggestions:
            lines.extend([
                "",
                "Suggested solutions:",
                *[f"  - {suggestion}" for suggestion in self.suggestions],
            ])
        
        # Add diagnostic info
        if self.diagnostic_info:
            lines.extend([
                "",
                "Diagnostic information:",
                *[f"  {key}: {value}" for key, value in self.diagnostic_info.items()],
            ])
        
        return "\n".join(lines)
    
    @staticmethod
    def _indent_text(text: str, indent: str = "  ") -> str:
        """Indent multi-line text."""
        return "\n".join(indent + line for line in text.split("\n"))
    
    def add_suggestion(self, suggestion: str) -> "TaguchiError":
        """Add a suggestion to the error. Returns self for chaining."""
        self.suggestions.append(suggestion)
        # Rebuild the message with new suggestion
        lines = str(self).split("\n")
        base_message = lines[0] if lines else "Error"
        super().__init__(self._build_error_message(base_message))
        return self
    
    def add_diagnostic_info(self, key: str, value: Any) -> "TaguchiError":
        """Add diagnostic information. Returns self for chaining."""
        self.diagnostic_info[key] = value
        # Rebuild the message with new info
        lines = str(self).split("\n")
        base_message = lines[0] if lines else "Error"
        super().__init__(self._build_error_message(base_message))
        return self


class BinaryDiscoveryError(TaguchiError):
    """Specific error for CLI binary discovery failures."""
    
    def __init__(
        self,
        searched_paths: List[str],
        cli_path_env: Optional[str] = None,
        path_env: Optional[str] = None,
    ):
        """
        Create a binary discovery error with actionable solutions.
        
        Args:
            searched_paths: List of paths that were searched
            cli_path_env: Value of TAGUCHI_CLI_PATH environment variable
            path_env: Value of PATH environment variable
        """
        path_details = []
        for path in searched_paths:
            exists = os.path.exists(path)
            executable = exists and os.access(path, os.X_OK)
            path_details.append(f"  - {path} (exists: {exists}, executable: {executable})")
        
        suggestions = [
            "Set TAGUCHI_CLI_PATH environment variable to the binary location",
            "Add taguchi binary to your PATH",
            "Install with: cd ~/workspace/taguchi && make install",
            "Build with: cd ~/workspace/taguchi && make",
        ]
        
        # Add specific suggestions based on environment
        if cli_path_env:
            suggestions.insert(0, f"Verify TAGUCHI_CLI_PATH points to correct binary: {cli_path_env}")
        
        diagnostic_info = {
            "TAGUCHI_CLI_PATH": cli_path_env,
            "PATH": path_env,
            "searched_paths_count": len(searched_paths),
        }
        
        message_lines = [
            "Could not find taguchi CLI binary.",
            "Searched paths:",
            *path_details,
        ]
        
        super().__init__(
            "\n".join(message_lines),
            operation="binary_discovery",
            suggestions=suggestions,
            diagnostic_info=diagnostic_info,
        )


class CommandExecutionError(TaguchiError):
    """Specific error for CLI command execution failures."""
    
    @classmethod
    def from_subprocess_error(
        cls,
        cmd: List[str],
        result: subprocess.CompletedProcess,
        operation: Optional[str] = None,
        working_directory: Optional[str] = None,
        cli_path: Optional[str] = None,
    ) -> "CommandExecutionError":
        """Create error from subprocess result."""
        suggestions = []
        
        # Analyze common failure modes and provide specific suggestions
        if result.returncode == 127:  # Command not found
            suggestions.extend([
                "Verify the CLI binary exists and is executable",
                "Check TAGUCHI_CLI_PATH environment variable",
                "Ensure taguchi is installed and in PATH",
            ])
        elif result.returncode == 1 and result.stderr:
            if "file not found" in result.stderr.lower():
                suggestions.extend([
                    "Verify the .tgu file path is correct",
                    "Ensure the .tgu file exists and is readable",
                    "Check file permissions",
                ])
            elif "invalid" in result.stderr.lower():
                suggestions.extend([
                    "Check .tgu file format and syntax",
                    "Verify factor names and levels are valid",
                    "Review the experiment configuration",
                ])
        
        if result.returncode == -9:  # SIGKILL - likely timeout
            suggestions.extend([
                "Increase CLI timeout (TAGUCHI_CLI_TIMEOUT environment variable)",
                "Check if the operation requires more time for large arrays",
                "Verify system resources are available",
            ])
        
        return cls(
            f"Command failed with exit code {result.returncode}",
            operation=operation,
            command=cmd,
            exit_code=result.returncode,
            stdout=result.stdout,
            stderr=result.stderr,
            working_directory=working_directory,
            cli_path=cli_path,
            suggestions=suggestions,
        )


class TimeoutError(TaguchiError):
    """Specific error for command timeout failures."""
    
    def __init__(
        self,
        cmd: List[str],
        timeout: int,
        operation: Optional[str] = None,
        working_directory: Optional[str] = None,
        cli_path: Optional[str] = None,
    ):
        """Create a timeout error with specific suggestions."""
        suggestions = [
            f"Increase timeout (current: {timeout}s) using TAGUCHI_CLI_TIMEOUT",
            "For large arrays, consider using timeout of 60s or more",
            "Check system resources and load",
            "Verify the .tgu file is not malformed causing infinite loops",
        ]
        
        super().__init__(
            f"Command timed out after {timeout} seconds",
            operation=operation,
            command=cmd,
            working_directory=working_directory,
            cli_path=cli_path,
            suggestions=suggestions,
        )


class ValidationError(TaguchiError):
    """Error for validation failures."""
    
    def __init__(self, validation_errors: List[str], operation: Optional[str] = None):
        """Create a validation error from a list of validation issues."""
        if len(validation_errors) == 1:
            message = f"Validation failed: {validation_errors[0]}"
        else:
            formatted_errors = [f"  - {error}" for error in validation_errors]
            message = "Validation failed:\n" + "\n".join(formatted_errors)
        
        super().__init__(
            message,
            operation=operation,
            suggestions=["Review the documentation for proper configuration format"],
        )


# Backward compatibility - keep the original name
TaguchiError = TaguchiError