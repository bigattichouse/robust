"""
Integration tests for the enhanced Taguchi Python bindings.

These tests verify that all components work together correctly and that
the enhancements maintain backward compatibility.
"""

import asyncio
import os
import tempfile
import pytest
from unittest.mock import AsyncMock, patch, Mock

from taguchi.config import TaguchiConfig, ConfigManager
from taguchi.core_enhanced import Taguchi, AsyncTaguchi
from taguchi.experiment_enhanced import Experiment
from taguchi.analyzer_enhanced import Analyzer
from taguchi.errors import (
    TaguchiError, BinaryDiscoveryError, ValidationError,
)


class TestBackwardCompatibility:
    """Test that enhanced modules maintain backward compatibility."""
    
    @pytest.fixture
    def mock_cli_with_output(self):
        """Mock CLI that returns realistic output."""
        mock_cli = Mock()
        
        # Mock list-arrays output
        mock_cli.return_value.returncode = 0
        # The --json document list_arrays reads now. It used to be the human
        # table, whose regex required a NUMBER before "levels" and so dropped
        # the mixed-level array entirely -- L18 is in this fixture on purpose.
        mock_cli.return_value.stdout = """{
  "tool": "taguchi",
  "command": "list-arrays",
  "schema": 1,
  "arrays": [
    {
      "name": "L9",
      "runs": 9,
      "columns": 4,
      "levels": 3,
      "mixed_levels": false
    },
    {
      "name": "L16",
      "runs": 16,
      "columns": 5,
      "levels": 4,
      "mixed_levels": false
    },
    {
      "name": "L18",
      "runs": 18,
      "columns": 8,
      "levels": null,
      "mixed_levels": true
    },
    {
      "name": "L25",
      "runs": 25,
      "columns": 6,
      "levels": 5,
      "mixed_levels": false
    }
  ]
}"""
        mock_cli.return_value.stderr = ""
        
        return mock_cli
    
    def test_original_taguchi_workflow(self, mock_cli_with_output):
        """Test that the original Taguchi workflow still works."""
        with patch('subprocess.run', mock_cli_with_output):
            with patch('shutil.which', return_value='/usr/bin/taguchi'):
                # Original workflow should work unchanged
                taguchi = Taguchi()
                
                # Original methods should work
                arrays = taguchi.list_arrays()
                assert 'L9' in arrays
                assert 'L16' in arrays
                
                array_info = taguchi.get_array_info('L9')
                assert array_info['rows'] == 9
                assert array_info['cols'] == 4
                
                suggestion = taguchi.suggest_array(3, 3)
                assert suggestion in ['L9', 'L16', 'L25']
    
    def test_original_experiment_workflow(self, mock_cli_with_output):
        """Test that the original Experiment workflow still works."""
        with patch('subprocess.run', mock_cli_with_output):
            with patch('shutil.which', return_value='/usr/bin/taguchi'):
                # Mock generate command output
                generate_mock = Mock()
                generate_mock.returncode = 0
                # The CLI's --json document, which is what generate_runs
                # reads now. It used to scrape `Run 1: temp=low, ...`.
                generate_mock.stdout = """{
  "tool": "taguchi",
  "command": "generate",
  "schema": 1,
  "run_count": 3,
  "factor_count": 2,
  "factors": [
    "temp",
    "pressure"
  ],
  "runs": [
    {
      "run_id": 1,
      "values": [
        "low",
        "low"
      ],
      "settings": {
        "temp": "low",
        "pressure": "low"
      }
    },
    {
      "run_id": 2,
      "values": [
        "medium",
        "high"
      ],
      "settings": {
        "temp": "medium",
        "pressure": "high"
      }
    },
    {
      "run_id": 3,
      "values": [
        "high",
        "low"
      ],
      "settings": {
        "temp": "high",
        "pressure": "low"
      }
    }
  ]
}"""
                generate_mock.stderr = ""
                
                with patch('subprocess.run', side_effect=[mock_cli_with_output.return_value, generate_mock]):
                    # Original workflow should work unchanged
                    with Experiment() as exp:
                        exp.add_factor("temp", ["low", "medium", "high"])
                        exp.add_factor("pressure", ["low", "high"])
                        
                        runs = exp.generate()
                        
                        assert len(runs) == 3
                        assert runs[0]["run_id"] == 1
                        assert runs[0]["factors"]["temp"] == "low"
                        assert runs[0]["factors"]["pressure"] == "low"
    
    def test_original_analyzer_workflow(self, mock_cli_with_output):
        """Test that the original Analyzer workflow still works."""
        mock_experiment = Mock()
        mock_experiment.generate.return_value = [
            {"run_id": 1, "factors": {"temp": "low"}},
            {"run_id": 2, "factors": {"temp": "high"}},
        ]
        mock_experiment.factors = {"temp": ["low", "high"]}
        mock_experiment.get_tgu_path.return_value = "/tmp/test.tgu"
        
        # Mock effects output
        effects_mock = Mock()
        effects_mock.returncode = 0
        # The --json document main_effects() reads now.
        effects_mock.stdout = "{\"tool\": \"taguchi\", \"command\": \"effects\", \"schema\": 1, \"metric\": \"response\", \"factor_count\": 1, \"effects\": [{\"factor\": \"temp\", \"range\": 0.05, \"levels\": [{\"level\": 1, \"value\": \"low\", \"mean\": 1.02}, {\"level\": 2, \"value\": \"high\", \"mean\": 1.07}]}]}"
        effects_mock.stderr = ""
        
        # return_value=, not the mock itself: patching subprocess.run WITH the
        # mock makes every call return a FRESH Mock, so returncode was a Mock
        # (non-zero), the error path ran, and stderr -- also a Mock -- blew up
        # in the message formatter. The test never exercised the happy path.
        with patch('subprocess.run', return_value=effects_mock):
            with patch('shutil.which', return_value='/usr/bin/taguchi'):
                # Original workflow should work unchanged
                with Analyzer(mock_experiment, metric_name="response") as analyzer:
                    analyzer.add_result(1, 1.0)
                    analyzer.add_result(2, 1.1)
                    
                    effects = analyzer.main_effects()
                    assert len(effects) == 1
                    assert effects[0]["factor"] == "temp"
                    
                    optimal = analyzer.recommend_optimal()
                    assert "temp" in optimal
    
    def test_original_constructor_signatures(self, tmp_path):
        """Test that original constructor signatures still work."""
        with patch('shutil.which', return_value='/usr/bin/taguchi'):
            # Original Taguchi constructor
            taguchi1 = Taguchi()
            custom = tmp_path / "custom_taguchi"
            custom.write_text("#!/bin/sh\nexit 0\n")
            custom.chmod(0o755)
            taguchi2 = Taguchi(cli_path=str(custom))
            
            assert taguchi1._config.cli_path is None
            assert taguchi2._config.cli_path == str(custom)
            
            # Original Experiment constructor
            exp1 = Experiment()
            exp2 = Experiment(array_type="L9")
            
            assert exp1._array_type is None
            assert exp2._array_type == "L9"
            
            # Original Analyzer constructor
            mock_exp = Mock()
            mock_exp.generate.return_value = []
            mock_exp.factors = {}
            
            analyzer1 = Analyzer(mock_exp)
            analyzer2 = Analyzer(mock_exp, "custom_metric")
            
            assert analyzer1._metric_name == "response"
            assert analyzer2._metric_name == "custom_metric"


class TestEnvironmentVariableIntegration:
    """Test environment variable integration across the system."""
    
    def test_environment_configuration_propagation(self):
        """Test that environment variables propagate through the system."""
        env_vars = {
            "TAGUCHI_CLI_PATH": "/usr/local/bin/taguchi", 
            "TAGUCHI_CLI_TIMEOUT": "120",
            "TAGUCHI_DEBUG": "true",
            "TAGUCHI_MAX_RETRIES": "3",
        }
        
        with patch.dict(os.environ, env_vars, clear=False):
            with patch('pathlib.Path.exists', return_value=True):
                with patch('os.access', return_value=True):
                    # Configuration should be loaded from environment
                    config = TaguchiConfig.from_environment()
                    
                    assert config.cli_path == "/usr/local/bin/taguchi"
                    assert config.cli_timeout == 120
                    assert config.debug_mode is True
                    assert config.max_retries == 3
                    
                    # Should propagate to Taguchi instance
                    taguchi = Taguchi(config=config)
                    assert taguchi._config.cli_path == "/usr/local/bin/taguchi"
                    assert taguchi._config.cli_timeout == 120
                    
                    # Should propagate to Experiment
                    exp = Experiment(config=config)
                    assert exp._taguchi._config.cli_timeout == 120
                    
                    # Should propagate to Analyzer
                    mock_exp = Mock()
                    mock_exp.generate.return_value = []
                    mock_exp.factors = {}
                    
                    analyzer = Analyzer(mock_exp, config=config)
                    assert analyzer._taguchi._config.cli_timeout == 120
    
    def test_binary_discovery_environment_integration(self):
        """Test binary discovery with environment variables."""
        with tempfile.NamedTemporaryFile(delete=False) as f:
            cli_path = f.name
        
        # Make it executable
        os.chmod(cli_path, 0o755)
        
        try:
            with patch.dict(os.environ, {"TAGUCHI_CLI_PATH": cli_path}, clear=False):
                taguchi = Taguchi()
                assert taguchi._cli_path == str(os.path.abspath(cli_path))
        finally:
            os.unlink(cli_path)
    
    def test_environment_variable_error_reporting(self):
        """Test error reporting includes environment variable info."""
        with patch.dict(os.environ, {}, clear=True):
            with patch('shutil.which', return_value=None):
                with patch('pathlib.Path.exists', return_value=False):
                    with pytest.raises(BinaryDiscoveryError) as exc_info:
                        Taguchi()
                    
                    error = exc_info.value
                    assert "TAGUCHI_CLI_PATH" in str(error)
                    assert "environment variable" in str(error)


class TestConfigurationIntegration:
    """Test configuration management integration."""
    
    def test_config_manager_default_behavior(self):
        """Test ConfigManager default configuration behavior."""
        # Reset any existing default
        ConfigManager.reset_default_config()
        
        with patch.dict(os.environ, {"TAGUCHI_DEBUG": "true"}, clear=False):
            # Should create from environment
            config1 = ConfigManager.get_default_config()
            assert config1.debug_mode is True
            
            # Should return same instance
            config2 = ConfigManager.get_default_config()
            assert config1 is config2
    
    def test_config_manager_custom_default(self):
        """Test setting custom default configuration."""
        custom_config = TaguchiConfig(cli_timeout=240, debug_mode=True)
        ConfigManager.set_default_config(custom_config)
        
        try:
            # All new instances should use custom config
            with patch('pathlib.Path.exists', return_value=True):
                taguchi = Taguchi()
                assert taguchi._config.cli_timeout == 240
                assert taguchi._config.debug_mode is True
                
                exp = Experiment()
                assert exp._taguchi._config.cli_timeout == 240
                
                mock_exp = Mock()
                mock_exp.generate.return_value = []
                mock_exp.factors = {}
                
                analyzer = Analyzer(mock_exp)
                assert analyzer._taguchi._config.cli_timeout == 240
        finally:
            ConfigManager.reset_default_config()
    
    def test_configuration_validation_integration(self):
        """Test configuration validation across components."""
        # Invalid configuration should be rejected everywhere
        with pytest.raises(ValueError):
            TaguchiConfig(cli_timeout=-1)
        
        with pytest.raises(ValueError):
            Taguchi(config=TaguchiConfig(cli_timeout=-1))
        
        with pytest.raises(ValueError):
            Experiment(config=TaguchiConfig(cli_timeout=-1))


class TestDependencyInjectionIntegration:
    """Test dependency injection integration."""
    
    def test_taguchi_dependency_injection_chain(self):
        """Test Taguchi instance propagation through dependency injection."""
        config = TaguchiConfig(debug_mode=True, cli_timeout=300)
        
        with patch('pathlib.Path.exists', return_value=True):
            with patch('os.access', return_value=True):
                # Create Taguchi with custom config
                taguchi = Taguchi(config=config)
                
                # Inject into Experiment
                exp = Experiment(taguchi=taguchi)
                assert exp._taguchi is taguchi
                assert exp._taguchi._config.debug_mode is True
                assert exp._taguchi._config.cli_timeout == 300
                
                # Inject into Analyzer
                mock_exp = Mock()
                mock_exp.generate.return_value = []
                mock_exp.factors = {}
                
                analyzer = Analyzer(mock_exp, taguchi=taguchi)
                assert analyzer._taguchi is taguchi
                assert analyzer._taguchi._config.debug_mode is True
    
    def test_experiment_analyzer_integration(self):
        """Test Experiment and Analyzer working together."""
        mock_taguchi = Mock()
        mock_taguchi._get_arrays_info.return_value = [
            {'name': 'L9', 'rows': 9, 'cols': 4, 'levels': 3},
        ]
        mock_taguchi.suggest_array.return_value = 'L9'
        mock_taguchi.generate_runs.return_value = [
            {"run_id": 1, "factors": {"temp": "low", "pressure": "low"}},
            {"run_id": 2, "factors": {"temp": "high", "pressure": "high"}},
        ]
        mock_taguchi.effects_json.return_value = [
            {"factor": "temp", "range": 0.050, "level_means": [1.020, 1.070],
             "level_values": ["cold", "hot"]},
        ]
        
        # Create experiment with injected Taguchi
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        exp.add_factor("pressure", ["low", "high"])
        
        runs = exp.generate()
        assert len(runs) == 2
        
        # Create analyzer with same Taguchi instance
        analyzer = Analyzer(exp, taguchi=mock_taguchi)
        analyzer.add_result(1, 1.0)
        analyzer.add_result(2, 1.1)
        
        effects = analyzer.main_effects()
        assert len(effects) == 1
        assert effects[0]["factor"] == "temp"
        
        optimal = analyzer.recommend_optimal()
        assert optimal["temp"] == "high"  # L2 has higher value


class TestErrorHandlingIntegration:
    """Test error handling integration across components."""
    
    def test_error_context_propagation(self):
        """Test that error context propagates through the system."""
        config = TaguchiConfig(debug_mode=True)
        
        with patch('pathlib.Path.exists', return_value=True):
            with patch('os.access', return_value=True):
                taguchi = Taguchi(config=config)
                
                # Mock command failure
                mock_result = Mock()
                mock_result.returncode = 1
                mock_result.stdout = ""
                mock_result.stderr = "Test error message"
                
                with patch('subprocess.run', return_value=mock_result):
                    with pytest.raises(TaguchiError) as exc_info:
                        taguchi._run_command(['test-command'], operation='test_operation')
                    
                    error = exc_info.value
                    assert error.operation == 'test_operation'
                    assert 'test-command' in str(error)
                    assert 'Test error message' in str(error)
    
    def test_validation_error_consistency(self):
        """Test validation error consistency across components."""
        mock_taguchi = Mock()
        mock_taguchi._get_arrays_info.return_value = []  # No arrays available
        
        # Experiment validation should catch array issues
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        errors = exp.validate()
        assert len(errors) > 0
        
        # Should also raise when using validate_and_raise
        with pytest.raises(ValidationError):
            exp.validate_and_raise()


class TestAsyncIntegration:
    """Test async functionality integration."""
    
    @pytest.mark.asyncio
    async def test_async_taguchi_basic_workflow(self):
        """Test basic AsyncTaguchi workflow."""
        config = TaguchiConfig(cli_timeout=60)
        
        with patch('pathlib.Path.exists', return_value=True):
            with patch('os.access', return_value=True):
                async_taguchi = AsyncTaguchi(config=config)
                
                # Mock async subprocess
                mock_process = Mock()
                mock_process.returncode = 0
                mock_process.communicate = Mock()
                mock_process.communicate.return_value = (
                    # The --json document list_arrays_async reads now.
                    "{\"tool\": \"taguchi\", \"command\": \"list-arrays\", \"schema\": 1, \"arrays\": [{\"name\": \"L9\", \"runs\": 9, \"columns\": 4, \"levels\": 3, \"mixed_levels\": false}, {\"name\": \"L16\", \"runs\": 16, \"columns\": 5, \"levels\": 4, \"mixed_levels\": false}]}".encode(),
                    b""
                )
                
                with patch('asyncio.create_subprocess_exec', return_value=mock_process):
                    with patch('asyncio.wait_for', return_value=(
                        "{\"tool\": \"taguchi\", \"command\": \"list-arrays\", \"schema\": 1, \"arrays\": [{\"name\": \"L9\", \"runs\": 9, \"columns\": 4, \"levels\": 3, \"mixed_levels\": false}, {\"name\": \"L16\", \"runs\": 16, \"columns\": 5, \"levels\": 4, \"mixed_levels\": false}]}".encode(),
                        b""
                    )):
                        arrays = await async_taguchi.list_arrays_async()
                        assert 'L9' in arrays
                        assert 'L16' in arrays
    
    @pytest.mark.asyncio
    async def test_async_error_handling(self):
        """Test async error handling."""
        config = TaguchiConfig(cli_timeout=1)
        
        with patch('pathlib.Path.exists', return_value=True):
            with patch('os.access', return_value=True):
                async_taguchi = AsyncTaguchi(config=config)
                
                # Mock timeout
                mock_process = Mock()
                mock_process.kill = Mock()
                # AsyncMock: the timeout path awaits process.wait().
                mock_process.wait = AsyncMock(return_value=None)
                
                with patch('asyncio.create_subprocess_exec', return_value=mock_process):
                    with patch('asyncio.wait_for', side_effect=asyncio.TimeoutError):
                        with pytest.raises(TaguchiError) as exc_info:
                            await async_taguchi._run_command_async(['slow-command'])
                        
                        assert "timed out" in str(exc_info.value)
                        mock_process.kill.assert_called_once()


class TestFullWorkflowIntegration:
    """Test complete workflow integration."""
    
    def test_complete_enhanced_workflow(self):
        """Test a complete workflow using all enhanced features."""
        # Set up environment
        env_vars = {
            "TAGUCHI_DEBUG": "true",
            "TAGUCHI_CLI_TIMEOUT": "120",
        }
        
        with patch.dict(os.environ, env_vars, clear=False):
            with patch('pathlib.Path.exists', return_value=True):
                with patch('os.access', return_value=True):
                    # Create configuration from environment
                    config = TaguchiConfig.from_environment()
                    assert config.debug_mode is True
                    assert config.cli_timeout == 120
                    
                    # Create Taguchi with environment config
                    taguchi = Taguchi(config=config)
                    
                    # Mock CLI responses
                    # --json documents, which is what the binding reads now.
                    # These were the human tables: an arrays line the regex
                    # could parse, a "Run 1: temp=low" line, and an effects row.
                    mock_responses = [
                        # list-arrays response
                        Mock(returncode=0, stdout="{\"tool\": \"taguchi\", \"command\": \"list-arrays\", \"schema\": 1, \"arrays\": [{\"name\": \"L9\", \"runs\": 9, \"columns\": 4, \"levels\": 3, \"mixed_levels\": false}]}", stderr=""),
                        # generate response
                        Mock(returncode=0, stdout="{\"tool\": \"taguchi\", \"command\": \"generate\", \"schema\": 1, \"run_count\": 2, \"factor_count\": 1, \"factors\": [\"temp\"], \"runs\": [{\"run_id\": 1, \"values\": [\"low\"], \"settings\": {\"temp\": \"low\"}}, {\"run_id\": 2, \"values\": [\"high\"], \"settings\": {\"temp\": \"high\"}}]}", stderr=""),
                        # effects response
                        Mock(returncode=0, stdout="{\"tool\": \"taguchi\", \"command\": \"effects\", \"schema\": 1, \"metric\": \"response\", \"factor_count\": 1, \"effects\": [{\"factor\": \"temp\", \"range\": 0.05, \"levels\": [{\"level\": 1, \"value\": \"low\", \"mean\": 1.02}, {\"level\": 2, \"value\": \"high\", \"mean\": 1.07}]}]}", stderr=""),
                    ]
                    
                    with patch('subprocess.run', side_effect=mock_responses):
                        # Create experiment with dependency injection
                        with Experiment(taguchi=taguchi) as exp:
                            # Enhanced validation
                            assert exp.is_valid() is False  # No factors yet
                            
                            # Add factors with validation
                            exp.add_factor("temp", ["low", "high"])
                            assert exp.is_valid() is True
                            
                            # Enhanced analysis
                            comparison = exp.compare_with_full_factorial()
                            assert comparison["full_factorial_runs"] == 2
                            
                            # Generate runs
                            runs = exp.generate()
                            assert len(runs) == 2
                            
                            # Create analyzer with dependency injection
                            with Analyzer(exp, metric_name="accuracy", taguchi=taguchi) as analyzer:
                                # Enhanced result management
                                analyzer.add_results_from_dict({1: 0.95, 2: 0.87})
                                
                                # Enhanced validation
                                assert analyzer.is_valid() is True
                                completeness = analyzer.check_completeness()
                                assert completeness["is_complete"] is True
                                
                                # Enhanced analysis
                                rankings = analyzer.get_factor_rankings()
                                assert len(rankings) == 1
                                assert rankings[0]["factor"] == "temp"
                                
                                # Prediction
                                prediction = analyzer.predict_response({"temp": "high"})
                                assert "predicted_response" in prediction
                                
                                # Enhanced summary
                                summary = analyzer.summary()
                                assert "100.0%" in summary  # Complete data
                                assert "temp" in summary


class TestDiagnosticsIntegration:
    """Test diagnostic functionality integration."""
    
    def test_installation_verification_integration(self):
        """Test installation verification with real components."""
        config = TaguchiConfig(debug_mode=True)
        
        with patch('pathlib.Path.exists', return_value=True):
            with patch('os.access', return_value=True):
                taguchi = Taguchi(config=config)
                
                # Mock successful CLI interactions
                with patch.object(taguchi, 'get_version', return_value="1.5.0"):
                    with patch.object(taguchi, '_run_command', return_value="L9 (9 runs)"):
                        verification = taguchi.verify_installation()
                        
                        assert verification['cli_found'] is True
                        assert verification['cli_version'] == "1.5.0"
                        assert verification['test_command_successful'] is True
                        assert len(verification['errors']) == 0
                        assert 'configuration' in verification
                        assert 'environment_diagnostics' in verification
    
    def test_environment_diagnostics_integration(self):
        """Test environment diagnostics functionality."""
        with patch('shutil.which', return_value='/usr/bin/taguchi'):
            with patch('os.access', return_value=True):
                diagnostics = ConfigManager.diagnose_environment()
                
                assert 'timestamp' in diagnostics
                assert 'environment_variables' in diagnostics
                assert 'path_search_results' in diagnostics
                assert diagnostics['path_search_results']['PATH lookup']['found'] is True


class TestPerformanceIntegration:
    """Test performance-related integration."""
    
    def test_caching_behavior_integration(self):
        """Test that caching works correctly across components."""
        mock_taguchi = Mock()
        mock_taguchi._get_arrays_info.return_value = [
            {'name': 'L9', 'rows': 9, 'cols': 4, 'levels': 3},
        ]
        
        # First call should fetch arrays
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        errors1 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 1
        
        # Second call should use cache
        errors2 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 1  # No additional call
        
        # Cache should be invalidated when adding factors
        exp.add_factor("pressure", ["low", "high"])
        errors3 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 2  # New call made
    
    def test_configuration_overhead_minimal(self):
        """Test that configuration overhead is minimal."""
        # Creating multiple instances should be fast
        configs = []
        for i in range(100):
            config = TaguchiConfig(cli_timeout=30 + i)
            configs.append(config)
        
        # All configs should be properly initialized
        for i, config in enumerate(configs):
            assert config.cli_timeout == 30 + i
            assert len(config.validate()) == 0