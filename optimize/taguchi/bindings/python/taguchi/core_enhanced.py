"""
Enhanced core Taguchi library interface with improved error handling,
configuration management, and robust binary discovery.

This module provides all the Phase 1, 2, and 3 improvements while maintaining
full backward compatibility with the existing API.
"""

import asyncio
import logging
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import List, Optional, Dict, Any, Union

from .config import TaguchiConfig, ConfigManager
from .errors import (
    TaguchiError, BinaryDiscoveryError, CommandExecutionError,
    TimeoutError, ValidationError
)


# Set up logger for debug output
logger = logging.getLogger(__name__)


class Taguchi:
    """
    Enhanced Python interface to the Taguchi orthogonal array CLI tool.
    
    Provides improved error handling, configuration management, retry logic,
    debug logging, and robust binary discovery while maintaining full
    backward compatibility.
    
    Example usage:
        # Basic usage (backward compatible)
        taguchi = Taguchi()
        
        # With custom configuration
        config = TaguchiConfig(cli_timeout=60, debug_mode=True)
        taguchi = Taguchi(config=config)
        
        # With environment variables
        os.environ['TAGUCHI_CLI_PATH'] = '/usr/local/bin/taguchi'
        taguchi = Taguchi()
    """

    def __init__(self, cli_path: Optional[str] = None, config: Optional[TaguchiConfig] = None):
        """
        Initialize Taguchi interface.
        
        Args:
            cli_path: Explicit path to CLI binary (backward compatibility)
            config: Configuration object (new feature)
        """
        # Handle configuration setup
        if config is None:
            config = ConfigManager.get_default_config()
        
        # Backward compatibility: cli_path parameter overrides config
        if cli_path is not None:
            config = config.copy(cli_path=cli_path)
        
        self._config = config
        self._cli_path = self._find_cli()
        self._array_cache: Optional[List[Dict]] = None
        self._version_cache: Optional[str] = None
        
        # Set up logging if debug mode is enabled
        if self._config.debug_mode:
            self._setup_debug_logging()
    
    def _setup_debug_logging(self):
        """Configure debug logging."""
        logging.basicConfig(
            level=logging.DEBUG,
            format='[%(asctime)s] %(name)s - %(levelname)s - %(message)s'
        )
        logger.info(f"Debug mode enabled. CLI path: {self._cli_path}")
        logger.info(f"Configuration: {self._config.to_dict()}")

    def _find_cli(self) -> str:
        """Find the taguchi CLI binary with enhanced error reporting."""
        possible_paths: List[str] = []
        
        # 1. Environment variable (highest priority)
        cli_path_env = os.getenv("TAGUCHI_CLI_PATH")
        if cli_path_env:
            possible_paths.append(cli_path_env)
        
        # 2. Explicit configuration
        if self._config.cli_path:
            possible_paths.append(self._config.cli_path)
        
        # 3. Relative paths from package location
        current_dir = Path(__file__).parent
        possible_paths.extend([
            str(current_dir.parent / "taguchi_cli"),  # For autoresearch integration
            str(current_dir.parent.parent.parent / "build" / "taguchi"),
            str(current_dir.parent.parent / "build" / "taguchi"),
            str(current_dir.parent / "build" / "taguchi"),
        ])
        
        # 4. Common system install locations
        possible_paths.extend([
            "/usr/local/bin/taguchi",
            "/usr/bin/taguchi",
            "/opt/taguchi/bin/taguchi",
        ])
        
        # Check each possible path
        for path_str in possible_paths:
            path = Path(path_str)
            if path.exists() and os.access(path, os.X_OK):
                resolved_path = str(path.absolute())
                if self._config.debug_mode:
                    logger.debug(f"Found CLI binary at: {resolved_path}")
                return resolved_path
        
        # 5. Fall back to PATH lookup
        found = shutil.which("taguchi")
        if found:
            if self._config.debug_mode:
                logger.debug(f"Found CLI binary via PATH: {found}")
            return found
        
        # Binary not found - provide detailed error
        raise BinaryDiscoveryError(
            searched_paths=possible_paths + ["PATH lookup"],
            cli_path_env=cli_path_env,
            path_env=os.getenv("PATH"),
        )

    def _run_command(self, args: List[str], operation: Optional[str] = None) -> str:
        """
        Run a taguchi command with enhanced error handling and retry logic.
        
        Args:
            args: Command arguments (without the binary name)
            operation: High-level operation description for error context
            
        Returns:
            Command stdout output
            
        Raises:
            TaguchiError: On command failure with detailed context
        """
        cmd = [self._cli_path] + args
        
        if self._config.log_commands or self._config.debug_mode:
            logger.info(f"Executing command: {' '.join(cmd)}")
        
        # Prepare environment
        env = dict(os.environ)
        env.update(self._config.environment_variables)
        
        # Retry logic
        last_exception = None
        for attempt in range(self._config.max_retries + 1):
            if attempt > 0:
                delay = self._config.retry_delay * (self._config.retry_backoff_multiplier ** (attempt - 1))
                if self._config.debug_mode:
                    logger.debug(f"Retrying command (attempt {attempt + 1}) after {delay:.1f}s delay")
                time.sleep(delay)
            
            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=self._config.cli_timeout,
                    cwd=self._config.working_directory,
                    env=env,
                )
                
                if self._config.log_command_output and result.stdout:
                    logger.debug(f"Command stdout: {result.stdout}")
                
                if result.returncode == 0:
                    return result.stdout
                
                # Command failed - create error
                error = CommandExecutionError.from_subprocess_error(
                    cmd=cmd,
                    result=result,
                    operation=operation,
                    working_directory=self._config.working_directory,
                    cli_path=self._cli_path,
                )
                
                # If this isn't the last attempt and error might be transient, continue
                if attempt < self._config.max_retries and self._is_retryable_error(result):
                    last_exception = error
                    if self._config.debug_mode:
                        logger.warning(f"Retryable error on attempt {attempt + 1}: {error}")
                    continue
                
                raise error
                
            except subprocess.TimeoutExpired:
                timeout_error = TimeoutError(
                    cmd=cmd,
                    timeout=self._config.cli_timeout,
                    operation=operation,
                    working_directory=self._config.working_directory,
                    cli_path=self._cli_path,
                )
                
                # Timeout might be retryable for transient issues
                if attempt < self._config.max_retries:
                    last_exception = timeout_error
                    if self._config.debug_mode:
                        logger.warning(f"Timeout on attempt {attempt + 1}: {timeout_error}")
                    continue
                
                raise timeout_error
            
            except Exception as e:
                # Unexpected error - wrap it
                error = TaguchiError(
                    f"Unexpected error executing command: {e}",
                    operation=operation,
                    command=cmd,
                    working_directory=self._config.working_directory,
                    cli_path=self._cli_path,
                )
                
                if attempt < self._config.max_retries:
                    last_exception = error
                    continue
                
                raise error
        
        # All retries exhausted
        if last_exception:
            raise last_exception
        else:
            raise TaguchiError("All retry attempts failed without specific error")

    def _is_retryable_error(self, result: subprocess.CompletedProcess) -> bool:
        """Determine if an error might be transient and worth retrying."""
        # System-level errors that might be transient
        if result.returncode in [125, 126, 127]:  # Container/execution errors
            return True
        
        # I/O errors that might be transient
        if result.stderr and any(keyword in result.stderr.lower() for keyword in [
            "resource temporarily unavailable",
            "device or resource busy",
            "no such device",
            "input/output error",
        ]):
            return True
        
        return False

    # Version and compatibility methods (Phase 2)
    def get_version(self) -> str:
        """Get CLI version for compatibility checks."""
        if self._version_cache is None:
            try:
                output = self._run_command(["--version"], operation="version_check")
                # Extract version from output (assuming format like "taguchi version 1.5.0")
                version_match = re.search(r'version\s+(\d+\.\d+\.\d+)', output)
                if version_match:
                    self._version_cache = version_match.group(1)
                else:
                    self._version_cache = "unknown"
            except TaguchiError:
                self._version_cache = "unknown"
        return self._version_cache
    
    def supports_format(self, format_version: str) -> bool:
        """Check if CLI supports specific output format."""
        # For now, assume backward compatibility
        cli_version = self.get_version()
        if cli_version == "unknown":
            return True  # Assume compatibility if we can't determine version
        
        # Simple version comparison (could be enhanced with proper version parsing)
        try:
            cli_parts = [int(x) for x in cli_version.split('.')]
            format_parts = [int(x) for x in format_version.split('.')]
            return cli_parts >= format_parts
        except ValueError:
            return True  # Assume compatibility on parse error

    # Installation verification (Phase 3)
    def verify_installation(self) -> Dict[str, Any]:
        """Comprehensive installation verification for troubleshooting."""
        verification_data = {
            "timestamp": time.time(),
            "cli_found": False,
            "cli_path": None,
            "cli_executable": False,
            "cli_version": None,
            "cli_accessible": False,
            "configuration": self._config.to_dict(),
            "environment_diagnostics": ConfigManager.diagnose_environment(),
            "test_command_successful": False,
            "errors": [],
        }
        
        try:
            # Check if CLI was found
            verification_data["cli_found"] = True
            verification_data["cli_path"] = self._cli_path
            verification_data["cli_executable"] = os.access(self._cli_path, os.X_OK)
            
            # Test basic command
            version = self.get_version()
            verification_data["cli_version"] = version
            verification_data["cli_accessible"] = True
            
            # Test list-arrays command
            self._run_command(["list-arrays"], operation="installation_verification")
            verification_data["test_command_successful"] = True
            
        except BinaryDiscoveryError as e:
            verification_data["errors"].append({
                "type": "binary_discovery",
                "message": str(e),
                "suggestions": e.suggestions,
            })
        except TaguchiError as e:
            verification_data["errors"].append({
                "type": "command_execution", 
                "message": str(e),
                "operation": e.operation,
                "suggestions": e.suggestions,
            })
        except Exception as e:
            verification_data["errors"].append({
                "type": "unexpected",
                "message": str(e),
            })
        
        return verification_data

    # Existing API methods with enhanced error handling
    def _get_arrays_info(self) -> List[Dict]:
        """Return cached array metadata with enhanced error handling."""
        if self._array_cache is not None:
            return self._array_cache

        try:
            output = self._run_command(["list-arrays"], operation="array_listing")
            arrays = []

            for line in output.strip().split('\n'):
                match = re.match(
                    r'\s+(L\d+)\s+\(\s*(\d+)\s+runs,\s*(\d+)\s+cols,\s*(\d+)\s+levels\)',
                    line,
                )
                if match:
                    arrays.append({
                        'name': match.group(1),
                        'rows': int(match.group(2)),
                        'cols': int(match.group(3)),
                        'levels': int(match.group(4)),
                    })

            if not arrays:
                raise TaguchiError(
                    "list-arrays returned no arrays — CLI output may have changed format",
                    operation="array_listing",
                    suggestions=[
                        "Verify CLI installation is complete",
                        "Check CLI version compatibility",
                        "Review CLI build configuration",
                    ],
                    diagnostic_info={"raw_output": output},
                )

            self._array_cache = arrays
            return arrays
        
        except TaguchiError:
            raise  # Re-raise enhanced errors as-is
        except Exception as e:
            raise TaguchiError(
                f"Unexpected error getting array information: {e}",
                operation="array_listing",
                suggestions=["Check CLI installation and permissions"],
            )

    def list_arrays(self) -> List[str]:
        """List all available orthogonal array names."""
        return [a['name'] for a in self._get_arrays_info()]

    def get_array_info(self, name: str) -> dict:
        """Get run/column/level counts for a named array."""
        for array in self._get_arrays_info():
            if array['name'] == name:
                return {
                    'rows': array['rows'],
                    'cols': array['cols'],
                    'levels': array['levels'],
                }
        raise TaguchiError(
            f"Array '{name}' not found",
            operation="array_info_lookup",
            suggestions=[
                f"Use list_arrays() to see available arrays",
                "Check array name spelling and case",
            ],
            diagnostic_info={"available_arrays": self.list_arrays()},
        )

    def suggest_array(self, num_factors: int, max_levels: int) -> str:
        """Suggest the smallest orthogonal array that fits the experiment."""
        if num_factors < 1:
            raise ValidationError(["num_factors must be at least 1"], "array_suggestion")
        if max_levels < 2:
            raise ValidationError(["max_levels must be at least 2"], "array_suggestion")

        try:
            arrays = self._get_arrays_info()

            # Prefer arrays whose native level count matches; fall back to any
            candidates = [a for a in arrays if a['levels'] >= max_levels] or arrays

            # Among candidates, keep those with enough columns
            sufficient = [a for a in candidates if a['cols'] >= num_factors]
            if not sufficient:
                # No perfect fit — return the largest available as best effort
                best_effort = max(candidates, key=lambda a: a['cols'])['name']
                logger.warning(
                    f"No array with {num_factors} columns found. "
                    f"Returning best effort: {best_effort}"
                )
                return best_effort

            # Return the smallest sufficient array (fewest runs)
            return min(sufficient, key=lambda a: a['rows'])['name']
        
        except Exception as e:
            if isinstance(e, TaguchiError):
                raise
            raise TaguchiError(
                f"Error suggesting array: {e}",
                operation="array_suggestion",
                diagnostic_info={
                    "num_factors": num_factors,
                    "max_levels": max_levels,
                },
            )

    def generate_runs(self, tgu_path: str) -> List[Dict[str, Any]]:
        """
        Generate experiment runs from a .tgu file path or raw .tgu content string.

        Returns a list of dicts: [{'run_id': int, 'factors': {name: value}}, ...]
        """
        temp_path = None
        try:
            if os.path.exists(tgu_path):
                file_path = tgu_path
                operation = "experiment_generation_from_file"
            else:
                # Treat the argument as raw .tgu content
                with tempfile.NamedTemporaryFile(
                    mode='w', suffix='.tgu', delete=False
                ) as f:
                    f.write(tgu_path)
                    temp_path = f.name
                file_path = temp_path
                operation = "experiment_generation_from_content"

            output = self._run_command(["generate", file_path], operation=operation)

            runs = []
            for line in output.strip().split("\n"):
                if not line.startswith("Run "):
                    continue
                parts = line.split(": ", 1)
                if len(parts) < 2:
                    continue
                try:
                    run_id = int(parts[0][4:].strip())
                except ValueError:
                    continue
                factors: Dict[str, str] = {}
                for pair in parts[1].split(", "):
                    if "=" in pair:
                        key, _, value = pair.partition("=")
                        factors[key.strip()] = value.strip()
                runs.append({"run_id": run_id, "factors": factors})

            if not runs:
                raise TaguchiError(
                    "No runs generated from .tgu file",
                    operation=operation,
                    suggestions=[
                        "Check .tgu file format and syntax",
                        "Verify factors are properly defined",
                        "Ensure the file contains valid factor definitions",
                    ],
                    diagnostic_info={"tgu_content_sample": tgu_path[:500] if len(tgu_path) < 1000 else "file_path"},
                )

            return runs
        
        except TaguchiError:
            raise  # Re-raise enhanced errors
        except Exception as e:
            raise TaguchiError(
                f"Unexpected error generating runs: {e}",
                operation="experiment_generation",
                suggestions=["Check .tgu file format and permissions"],
            )
        finally:
            if temp_path and os.path.exists(temp_path):
                try:
                    os.unlink(temp_path)
                except OSError:
                    pass

    def analyze(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Run full analysis with main effects and optimal recommendations."""
        return self._run_command(
            ["analyze", tgu_path, results_csv, "--metric", metric],
            operation="full_analysis"
        )

    def effects(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Calculate and return the main effects table.""" 
        return self._run_command(
            ["effects", tgu_path, results_csv, "--metric", metric],
            operation="effects_calculation"
        )


class AsyncTaguchi:
    """
    Asynchronous version of the Taguchi interface for non-blocking operations.
    
    Useful for long-running experiments or when integrating with async frameworks.
    All methods return coroutines that can be awaited.
    """
    
    def __init__(self, cli_path: Optional[str] = None, config: Optional[TaguchiConfig] = None):
        """Initialize async Taguchi interface."""
        self._taguchi = Taguchi(cli_path=cli_path, config=config)
    
    async def _run_command_async(self, args: List[str], operation: Optional[str] = None) -> str:
        """Run command asynchronously."""
        cmd = [self._taguchi._cli_path] + args
        
        if self._taguchi._config.log_commands or self._taguchi._config.debug_mode:
            logger.info(f"Executing async command: {' '.join(cmd)}")
        
        # Prepare environment
        env = dict(os.environ)
        env.update(self._taguchi._config.environment_variables)
        
        # Create subprocess
        process = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            cwd=self._taguchi._config.working_directory,
            env=env,
        )
        
        try:
            stdout, stderr = await asyncio.wait_for(
                process.communicate(),
                timeout=self._taguchi._config.cli_timeout
            )
            
            stdout = stdout.decode('utf-8') if stdout else ""
            stderr = stderr.decode('utf-8') if stderr else ""
            
            if process.returncode != 0:
                # Create mock result object for error handling
                class MockResult:
                    def __init__(self, returncode, stdout, stderr):
                        self.returncode = returncode
                        self.stdout = stdout
                        self.stderr = stderr
                
                result = MockResult(process.returncode, stdout, stderr)
                raise CommandExecutionError.from_subprocess_error(
                    cmd=cmd,
                    result=result,
                    operation=operation,
                    working_directory=self._taguchi._config.working_directory,
                    cli_path=self._taguchi._cli_path,
                )
            
            return stdout
            
        except asyncio.TimeoutError:
            # Kill the process
            process.kill()
            await process.wait()
            
            raise TimeoutError(
                cmd=cmd,
                timeout=self._taguchi._config.cli_timeout,
                operation=operation,
                working_directory=self._taguchi._config.working_directory,
                cli_path=self._taguchi._cli_path,
            )
    
    async def list_arrays_async(self) -> List[str]:
        """List arrays asynchronously."""
        output = await self._run_command_async(["list-arrays"], "array_listing")
        arrays = []
        
        for line in output.strip().split('\n'):
            match = re.match(
                r'\s+(L\d+)\s+\(\s*(\d+)\s+runs,\s*(\d+)\s+cols,\s*(\d+)\s+levels\)',
                line,
            )
            if match:
                arrays.append(match.group(1))
        
        return arrays
    
    async def generate_runs_async(self, tgu_path: str) -> List[Dict[str, Any]]:
        """Generate runs asynchronously."""
        # For simplicity, delegate to sync version in thread pool
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._taguchi.generate_runs, tgu_path)
    
    async def analyze_async(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Run analysis asynchronously."""
        return await self._run_command_async(
            ["analyze", tgu_path, results_csv, "--metric", metric],
            "full_analysis"
        )
    
    async def effects_async(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Calculate effects asynchronously."""
        return await self._run_command_async(
            ["effects", tgu_path, results_csv, "--metric", metric],
            "effects_calculation"
        )