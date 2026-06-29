"""
Configuration management for the Taguchi Python bindings.

Provides centralized configuration with environment variable support,
validation, and backward compatibility.
"""

import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Any, Union


@dataclass
class TaguchiConfig:
    """
    Configuration for Taguchi Python bindings.
    
    Supports environment variable overrides and validation.
    All settings have sensible defaults for backward compatibility.
    """
    
    # Binary location and execution
    cli_path: Optional[str] = None
    cli_timeout: int = 30
    
    # Error handling and reliability  
    max_retries: int = 0
    retry_delay: float = 1.0
    retry_backoff_multiplier: float = 2.0
    
    # Debugging and observability
    debug_mode: bool = False
    log_commands: bool = False
    log_command_output: bool = False
    
    # Environment and working directory
    working_directory: Optional[str] = None
    environment_variables: Dict[str, str] = field(default_factory=dict)
    
    # Compatibility and versioning
    min_cli_version: Optional[str] = None
    output_format_version: str = "1.0"
    
    def __post_init__(self):
        """Validate configuration after initialization."""
        errors = self.validate()
        if errors:
            raise ValueError(f"Invalid configuration: {'; '.join(errors)}")
    
    def validate(self) -> List[str]:
        """Return list of configuration errors, empty if valid."""
        errors = []
        
        if self.cli_timeout <= 0:
            errors.append("cli_timeout must be positive")
            
        if self.cli_path and not Path(self.cli_path).exists():
            errors.append(f"cli_path does not exist: {self.cli_path}")
            
        if self.max_retries < 0:
            errors.append("max_retries must be non-negative")
            
        if self.retry_delay < 0:
            errors.append("retry_delay must be non-negative")
            
        if self.retry_backoff_multiplier < 1.0:
            errors.append("retry_backoff_multiplier must be >= 1.0")
            
        if self.working_directory and not Path(self.working_directory).exists():
            errors.append(f"working_directory does not exist: {self.working_directory}")
            
        return errors
    
    @classmethod
    def from_environment(cls) -> "TaguchiConfig":
        """Create configuration from environment variables."""
        return cls(
            cli_path=os.getenv("TAGUCHI_CLI_PATH"),
            cli_timeout=int(os.getenv("TAGUCHI_CLI_TIMEOUT", "30")),
            max_retries=int(os.getenv("TAGUCHI_MAX_RETRIES", "0")),
            retry_delay=float(os.getenv("TAGUCHI_RETRY_DELAY", "1.0")),
            retry_backoff_multiplier=float(os.getenv("TAGUCHI_RETRY_BACKOFF", "2.0")),
            debug_mode=os.getenv("TAGUCHI_DEBUG", "false").lower() == "true",
            log_commands=os.getenv("TAGUCHI_LOG_COMMANDS", "false").lower() == "true",
            log_command_output=os.getenv("TAGUCHI_LOG_OUTPUT", "false").lower() == "true",
            working_directory=os.getenv("TAGUCHI_WORKING_DIR"),
            environment_variables=_parse_env_vars(os.getenv("TAGUCHI_ENV_VARS", "")),
            min_cli_version=os.getenv("TAGUCHI_MIN_CLI_VERSION"),
            output_format_version=os.getenv("TAGUCHI_OUTPUT_FORMAT", "1.0"),
        )
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert configuration to dictionary for serialization."""
        return {
            "cli_path": self.cli_path,
            "cli_timeout": self.cli_timeout,
            "max_retries": self.max_retries,
            "retry_delay": self.retry_delay,
            "retry_backoff_multiplier": self.retry_backoff_multiplier,
            "debug_mode": self.debug_mode,
            "log_commands": self.log_commands,
            "log_command_output": self.log_command_output,
            "working_directory": self.working_directory,
            "environment_variables": self.environment_variables.copy(),
            "min_cli_version": self.min_cli_version,
            "output_format_version": self.output_format_version,
        }
    
    def copy(self, **overrides) -> "TaguchiConfig":
        """Create a copy of the configuration with optional overrides."""
        config_dict = self.to_dict()
        config_dict.update(overrides)
        return TaguchiConfig(**config_dict)


def _parse_env_vars(env_vars_str: str) -> Dict[str, str]:
    """Parse environment variables from comma-separated KEY=VALUE pairs."""
    if not env_vars_str.strip():
        return {}
    
    env_vars = {}
    for pair in env_vars_str.split(","):
        pair = pair.strip()
        if "=" in pair:
            key, value = pair.split("=", 1)
            env_vars[key.strip()] = value.strip()
    
    return env_vars


class ConfigManager:
    """
    Manages configuration lifecycle and provides utilities for debugging
    configuration issues.
    """
    
    _default_config: Optional[TaguchiConfig] = None
    
    @classmethod
    def get_default_config(cls) -> TaguchiConfig:
        """Get the default configuration, creating it if necessary."""
        if cls._default_config is None:
            cls._default_config = TaguchiConfig.from_environment()
        return cls._default_config
    
    @classmethod
    def set_default_config(cls, config: TaguchiConfig) -> None:
        """Set the default configuration for all new instances."""
        cls._default_config = config
    
    @classmethod
    def reset_default_config(cls) -> None:
        """Reset to environment-based configuration."""
        cls._default_config = None
    
    @staticmethod
    def diagnose_environment() -> Dict[str, Any]:
        """
        Provide detailed diagnostic information about the current environment
        for troubleshooting configuration issues.
        """
        env_vars = {
            "TAGUCHI_CLI_PATH": os.getenv("TAGUCHI_CLI_PATH"),
            "TAGUCHI_CLI_TIMEOUT": os.getenv("TAGUCHI_CLI_TIMEOUT"),
            "TAGUCHI_MAX_RETRIES": os.getenv("TAGUCHI_MAX_RETRIES"),
            "TAGUCHI_DEBUG": os.getenv("TAGUCHI_DEBUG"),
            "PATH": os.getenv("PATH", "").split(os.pathsep),
        }
        
        # Check common CLI locations
        search_paths = [
            "/usr/local/bin/taguchi",
            "/usr/bin/taguchi",
            "taguchi",  # PATH lookup
        ]
        
        path_info = {}
        for path_str in search_paths:
            path = Path(path_str) if path_str != "taguchi" else None
            if path:
                path_info[str(path)] = {
                    "exists": path.exists(),
                    "executable": path.exists() and os.access(path, os.X_OK),
                    "absolute": str(path.absolute()) if path.exists() else None,
                }
            else:
                # PATH lookup
                import shutil
                found = shutil.which("taguchi")
                path_info["PATH lookup"] = {
                    "found": bool(found),
                    "path": found,
                    "executable": bool(found and os.access(found, os.X_OK)) if found else False,
                }
        
        return {
            "timestamp": time.time(),
            "working_directory": os.getcwd(),
            "environment_variables": env_vars,
            "path_search_results": path_info,
            "python_version": f"{os.sys.version_info.major}.{os.sys.version_info.minor}.{os.sys.version_info.micro}",
            "platform": os.name,
        }


# Default configuration instance for backward compatibility
default_config = ConfigManager.get_default_config