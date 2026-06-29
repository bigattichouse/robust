"""
Unit tests for configuration management.
"""

import os
import pytest
import tempfile
from unittest.mock import patch
from pathlib import Path

from taguchi.config import TaguchiConfig, ConfigManager, _parse_env_vars


class TestTaguchiConfig:
    """Test the TaguchiConfig class."""
    
    def test_default_config(self):
        """Test default configuration values."""
        config = TaguchiConfig()
        
        assert config.cli_path is None
        assert config.cli_timeout == 30
        assert config.max_retries == 0
        assert config.retry_delay == 1.0
        assert config.retry_backoff_multiplier == 2.0
        assert config.debug_mode is False
        assert config.log_commands is False
        assert config.log_command_output is False
        assert config.working_directory is None
        assert config.environment_variables == {}
        assert config.min_cli_version is None
        assert config.output_format_version == "1.0"
    
    def test_custom_config(self):
        """Test custom configuration values."""
        config = TaguchiConfig(
            cli_path="/usr/bin/taguchi",
            cli_timeout=60,
            max_retries=3,
            debug_mode=True,
            environment_variables={"TEST": "value"}
        )
        
        assert config.cli_path == "/usr/bin/taguchi"
        assert config.cli_timeout == 60
        assert config.max_retries == 3
        assert config.debug_mode is True
        assert config.environment_variables == {"TEST": "value"}
    
    def test_validation_success(self):
        """Test successful validation."""
        with tempfile.TemporaryDirectory() as temp_dir:
            config = TaguchiConfig(
                cli_timeout=30,
                max_retries=2,
                retry_delay=1.0,
                working_directory=temp_dir
            )
            
            errors = config.validate()
            assert errors == []
    
    def test_validation_failures(self):
        """Test validation error detection."""
        # Test invalid timeout
        with pytest.raises(ValueError, match="cli_timeout must be positive"):
            TaguchiConfig(cli_timeout=0)
        
        # Test invalid retries
        with pytest.raises(ValueError, match="max_retries must be non-negative"):
            TaguchiConfig(max_retries=-1)
        
        # Test invalid retry delay
        with pytest.raises(ValueError, match="retry_delay must be non-negative"):
            TaguchiConfig(retry_delay=-1.0)
        
        # Test invalid backoff multiplier
        with pytest.raises(ValueError, match="retry_backoff_multiplier must be >= 1.0"):
            TaguchiConfig(retry_backoff_multiplier=0.5)
        
        # Test non-existent CLI path
        with pytest.raises(ValueError, match="cli_path does not exist"):
            TaguchiConfig(cli_path="/non/existent/path")
        
        # Test non-existent working directory
        with pytest.raises(ValueError, match="working_directory does not exist"):
            TaguchiConfig(working_directory="/non/existent/dir")
    
    def test_from_environment_defaults(self):
        """Test loading from environment with defaults."""
        with patch.dict(os.environ, {}, clear=True):
            config = TaguchiConfig.from_environment()
            
            assert config.cli_path is None
            assert config.cli_timeout == 30
            assert config.max_retries == 0
            assert config.debug_mode is False
    
    def test_from_environment_custom(self):
        """Test loading from environment with custom values."""
        env_vars = {
            "TAGUCHI_CLI_PATH": "/usr/local/bin/taguchi",
            "TAGUCHI_CLI_TIMEOUT": "60",
            "TAGUCHI_MAX_RETRIES": "3",
            "TAGUCHI_RETRY_DELAY": "2.0",
            "TAGUCHI_RETRY_BACKOFF": "1.5",
            "TAGUCHI_DEBUG": "true",
            "TAGUCHI_LOG_COMMANDS": "true",
            "TAGUCHI_LOG_OUTPUT": "true",
            "TAGUCHI_WORKING_DIR": "/tmp",
            "TAGUCHI_ENV_VARS": "VAR1=value1,VAR2=value2",
            "TAGUCHI_MIN_CLI_VERSION": "1.5.0",
            "TAGUCHI_OUTPUT_FORMAT": "2.0",
        }
        
        with patch.dict(os.environ, env_vars, clear=True):
            # Mock path existence for validation
            with patch("pathlib.Path.exists", return_value=True):
                config = TaguchiConfig.from_environment()
                
                assert config.cli_path == "/usr/local/bin/taguchi"
                assert config.cli_timeout == 60
                assert config.max_retries == 3
                assert config.retry_delay == 2.0
                assert config.retry_backoff_multiplier == 1.5
                assert config.debug_mode is True
                assert config.log_commands is True
                assert config.log_command_output is True
                assert config.working_directory == "/tmp"
                assert config.environment_variables == {"VAR1": "value1", "VAR2": "value2"}
                assert config.min_cli_version == "1.5.0"
                assert config.output_format_version == "2.0"
    
    def test_to_dict(self):
        """Test configuration serialization."""
        config = TaguchiConfig(
            cli_path="/usr/bin/taguchi",
            cli_timeout=60,
            debug_mode=True,
            environment_variables={"TEST": "value"}
        )
        
        config_dict = config.to_dict()
        
        assert config_dict["cli_path"] == "/usr/bin/taguchi"
        assert config_dict["cli_timeout"] == 60
        assert config_dict["debug_mode"] is True
        assert config_dict["environment_variables"] == {"TEST": "value"}
        
        # Ensure it's a copy
        config_dict["environment_variables"]["NEW"] = "new_value"
        assert "NEW" not in config.environment_variables
    
    def test_copy(self):
        """Test configuration copying with overrides."""
        original = TaguchiConfig(
            cli_timeout=30,
            debug_mode=False,
            environment_variables={"TEST": "value"}
        )
        
        copy_config = original.copy(
            cli_timeout=60,
            debug_mode=True
        )
        
        # Original unchanged
        assert original.cli_timeout == 30
        assert original.debug_mode is False
        
        # Copy has overrides
        assert copy_config.cli_timeout == 60
        assert copy_config.debug_mode is True
        
        # Other values preserved
        assert copy_config.environment_variables == {"TEST": "value"}


class TestParseEnvVars:
    """Test environment variable parsing."""
    
    def test_empty_string(self):
        """Test parsing empty string."""
        assert _parse_env_vars("") == {}
        assert _parse_env_vars("   ") == {}
    
    def test_single_pair(self):
        """Test parsing single key-value pair."""
        assert _parse_env_vars("KEY=value") == {"KEY": "value"}
    
    def test_multiple_pairs(self):
        """Test parsing multiple key-value pairs."""
        result = _parse_env_vars("KEY1=value1,KEY2=value2,KEY3=value3")
        expected = {"KEY1": "value1", "KEY2": "value2", "KEY3": "value3"}
        assert result == expected
    
    def test_whitespace_handling(self):
        """Test handling of whitespace."""
        result = _parse_env_vars("  KEY1 = value1 ,  KEY2=value2  ")
        expected = {"KEY1": "value1", "KEY2": "value2"}
        assert result == expected
    
    def test_invalid_pairs(self):
        """Test handling of invalid pairs."""
        # No equals sign - should be skipped
        result = _parse_env_vars("KEY1=value1,INVALID,KEY2=value2")
        expected = {"KEY1": "value1", "KEY2": "value2"}
        assert result == expected
    
    def test_empty_values(self):
        """Test handling of empty values."""
        result = _parse_env_vars("KEY1=,KEY2=value2")
        expected = {"KEY1": "", "KEY2": "value2"}
        assert result == expected


class TestConfigManager:
    """Test the ConfigManager class."""
    
    def teardown_method(self):
        """Reset config manager state after each test."""
        ConfigManager.reset_default_config()
    
    def test_get_default_config(self):
        """Test getting default configuration."""
        config = ConfigManager.get_default_config()
        assert isinstance(config, TaguchiConfig)
        
        # Should return same instance on subsequent calls
        config2 = ConfigManager.get_default_config()
        assert config is config2
    
    def test_set_default_config(self):
        """Test setting custom default configuration."""
        custom_config = TaguchiConfig(cli_timeout=60, debug_mode=True)
        ConfigManager.set_default_config(custom_config)
        
        retrieved_config = ConfigManager.get_default_config()
        assert retrieved_config is custom_config
        assert retrieved_config.cli_timeout == 60
        assert retrieved_config.debug_mode is True
    
    def test_reset_default_config(self):
        """Test resetting to environment-based configuration."""
        # Set custom config
        custom_config = TaguchiConfig(cli_timeout=60)
        ConfigManager.set_default_config(custom_config)
        
        # Reset
        ConfigManager.reset_default_config()
        
        # Should create new config from environment
        new_config = ConfigManager.get_default_config()
        assert new_config is not custom_config
    
    def test_diagnose_environment(self):
        """Test environment diagnostics."""
        with patch("shutil.which", return_value="/usr/bin/taguchi"):
            with patch("os.access", return_value=True):
                diagnostics = ConfigManager.diagnose_environment()
                
                assert "timestamp" in diagnostics
                assert "working_directory" in diagnostics
                assert "environment_variables" in diagnostics
                assert "path_search_results" in diagnostics
                assert "python_version" in diagnostics
                assert "platform" in diagnostics
                
                # Check PATH lookup result
                assert diagnostics["path_search_results"]["PATH lookup"]["found"] is True
                assert diagnostics["path_search_results"]["PATH lookup"]["path"] == "/usr/bin/taguchi"
    
    def test_diagnose_environment_no_cli(self):
        """Test diagnostics when CLI not found."""
        with patch("shutil.which", return_value=None):
            diagnostics = ConfigManager.diagnose_environment()
            
            assert diagnostics["path_search_results"]["PATH lookup"]["found"] is False
            assert diagnostics["path_search_results"]["PATH lookup"]["path"] is None


class TestConfigIntegration:
    """Integration tests for configuration system."""
    
    def test_environment_override_precedence(self):
        """Test that environment variables override defaults."""
        with patch.dict(os.environ, {"TAGUCHI_CLI_TIMEOUT": "120"}, clear=False):
            config = TaguchiConfig.from_environment()
            assert config.cli_timeout == 120
    
    def test_boolean_environment_parsing(self):
        """Test boolean environment variable parsing."""
        # Test various true values
        for true_val in ["true", "TRUE", "True", "1", "yes", "YES"]:
            with patch.dict(os.environ, {"TAGUCHI_DEBUG": true_val}, clear=False):
                config = TaguchiConfig.from_environment()
                assert config.debug_mode is True
        
        # Test various false values
        for false_val in ["false", "FALSE", "False", "0", "no", "NO"]:
            with patch.dict(os.environ, {"TAGUCHI_DEBUG": false_val}, clear=False):
                config = TaguchiConfig.from_environment()
                assert config.debug_mode is False
    
    def test_invalid_environment_values(self):
        """Test handling of invalid environment values."""
        # Invalid timeout value should raise during validation
        with patch.dict(os.environ, {"TAGUCHI_CLI_TIMEOUT": "invalid"}, clear=False):
            with pytest.raises(ValueError):
                TaguchiConfig.from_environment()
        
        # Invalid retry count should raise during validation  
        with patch.dict(os.environ, {"TAGUCHI_MAX_RETRIES": "invalid"}, clear=False):
            with pytest.raises(ValueError):
                TaguchiConfig.from_environment()