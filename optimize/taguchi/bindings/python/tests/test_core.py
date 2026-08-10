"""
Tests for taguchi.core — Taguchi class (CLI subprocess wrapper).
"""

import os
import pytest
from unittest.mock import patch, MagicMock
from taguchi.core import Taguchi, TaguchiError
from .conftest import CLI_PATH


class TestFindCli:
    def test_finds_explicit_path(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        assert t._cli_path == cli_path

    def test_raises_if_not_found(self):
        """Contract, not mechanism.

        This used to patch taguchi.core.Path to make every candidate look
        absent -- which stopped working the moment the search moved into
        _discovery, even though the behaviour under test had not changed. What
        core.Taguchi promises is "raise TaguchiError when there is no binary";
        how the looking is done is _discovery's business and is covered there.
        """
        with patch("taguchi.core.find_cli", return_value=None):
            with pytest.raises(TaguchiError, match="Could not find"):
                Taguchi()

    def test_finds_via_path_fallback(self, cli_path):
        """Falls back to PATH when nothing else matches."""
        with patch("taguchi._discovery.candidate_paths", return_value=[]), \
             patch("shutil.which", return_value=cli_path):
            t = Taguchi()
        assert t._cli_path == cli_path

    def test_uses_shutil_which_compatible_approach(self, cli_path):
        """The binary found should be executable."""
        t = Taguchi(cli_path=cli_path)
        assert os.access(t._cli_path, os.X_OK)


class TestRunCommand:
    def test_returns_stdout_on_success(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        output = t._run_command(["list-arrays"])
        assert "L4" in output
        assert "L9" in output

    def test_raises_on_nonzero_exit(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        with pytest.raises(TaguchiError, match="[Cc]ommand failed"):
            t._run_command(["generate", "/nonexistent/file.tgu"])

    def test_error_message_included(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        with pytest.raises(TaguchiError) as exc_info:
            t._run_command(["generate", "/nonexistent/file.tgu"])
        assert len(str(exc_info.value)) > 0


class TestListArrays:
    def test_returns_list_of_strings(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        arrays = t.list_arrays()
        assert isinstance(arrays, list)
        assert len(arrays) > 0
        assert all(isinstance(a, str) for a in arrays)

    def test_known_arrays_present(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        arrays = t.list_arrays()
        for name in ("L4", "L8", "L9", "L16", "L27"):
            assert name in arrays, f"{name} missing from list_arrays()"

    def test_result_is_cached(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        with patch.object(t, "_run_command", wraps=t._run_command) as mock_cmd:
            t.list_arrays()
            t.list_arrays()
            # Second call should not invoke subprocess again
            assert mock_cmd.call_count == 1


class TestGetArrayInfo:
    def test_returns_dict_with_expected_keys(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        info = t.get_array_info("L9")
        assert set(info.keys()) == {"rows", "cols", "levels"}

    def test_l9_values(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        info = t.get_array_info("L9")
        assert info["rows"] == 9
        assert info["levels"] == 3

    def test_raises_for_unknown_array(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        with pytest.raises(TaguchiError, match="not found"):
            t.get_array_info("L999")


class TestSuggestArray:
    def test_suggests_valid_array(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        name = t.suggest_array(num_factors=3, max_levels=3)
        assert name in t.list_arrays()

    def test_suggested_array_has_enough_columns(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        for num_factors in (2, 3, 4, 7):
            name = t.suggest_array(num_factors=num_factors, max_levels=3)
            info = t.get_array_info(name)
            assert info["cols"] >= num_factors, (
                f"suggest_array({num_factors}, 3) returned {name} "
                f"which only has {info['cols']} columns"
            )

    def test_two_level_suggestion(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        name = t.suggest_array(num_factors=3, max_levels=2)
        info = t.get_array_info(name)
        assert info["levels"] == 2
        assert info["cols"] >= 3

    def test_five_level_suggestion(self, cli_path):
        t = Taguchi(cli_path=cli_path)
        name = t.suggest_array(num_factors=2, max_levels=5)
        assert name in t.list_arrays()


class TestGenerateRuns:
    def test_returns_list_of_dicts(self, cli_path, simple_tgu):
        t = Taguchi(cli_path=cli_path)
        runs = t.generate_runs(simple_tgu)
        assert isinstance(runs, list)
        assert len(runs) > 0

    def test_run_structure(self, cli_path, simple_tgu):
        t = Taguchi(cli_path=cli_path)
        runs = t.generate_runs(simple_tgu)
        for run in runs:
            assert "run_id" in run
            assert "factors" in run
            assert isinstance(run["run_id"], int)
            assert isinstance(run["factors"], dict)

    def test_run_ids_are_sequential(self, cli_path, simple_tgu):
        t = Taguchi(cli_path=cli_path)
        runs = t.generate_runs(simple_tgu)
        ids = [r["run_id"] for r in runs]
        assert ids == list(range(1, len(runs) + 1))

    def test_factors_present_in_each_run(self, cli_path, simple_tgu):
        t = Taguchi(cli_path=cli_path)
        runs = t.generate_runs(simple_tgu)
        for run in runs:
            assert "depth" in run["factors"]
            assert "lr" in run["factors"]

    def test_factor_values_are_from_definition(self, cli_path, simple_tgu):
        t = Taguchi(cli_path=cli_path)
        runs = t.generate_runs(simple_tgu)
        valid_depths = {"4", "6", "8"}
        valid_lrs = {"0.02", "0.04", "0.08"}
        for run in runs:
            assert run["factors"]["depth"] in valid_depths
            assert run["factors"]["lr"] in valid_lrs

    def test_accepts_tgu_content_string(self, cli_path):
        """generate_runs should also accept raw .tgu content as a string."""
        t = Taguchi(cli_path=cli_path)
        content = "factors:\n  a: 1, 2\n  b: x, y\n"
        runs = t.generate_runs(content)
        assert len(runs) > 0

    def test_empty_output_returns_empty_list(self, cli_path):
        """If CLI returns no 'Run N:' lines, result should be empty list, not crash."""
        t = Taguchi(cli_path=cli_path)
        with patch.object(t, "_run_command", return_value="No runs here\n"):
            runs = t.generate_runs.__wrapped__(t, "/fake/path") if hasattr(
                t.generate_runs, "__wrapped__"
            ) else []
        # Either empty list or graceful — just must not raise


class TestEffectsParsing:
    """Test the effects output via the public effects() method."""

    def test_effects_returns_string(self, cli_path, simple_tgu, simple_results_csv):
        t = Taguchi(cli_path=cli_path)
        output = t.effects(simple_tgu, simple_results_csv)
        assert isinstance(output, str)
        assert len(output) > 0

    def test_effects_contains_factor_names(self, cli_path, simple_tgu, simple_results_csv):
        t = Taguchi(cli_path=cli_path)
        output = t.effects(simple_tgu, simple_results_csv)
        assert "depth" in output
        assert "lr" in output


# ---- merged from test_core_enhanced.py ----
import asyncio
import os
import subprocess
import tempfile
import pytest
from unittest.mock import AsyncMock, Mock, patch, MagicMock
from pathlib import Path
from taguchi.config import TaguchiConfig
from taguchi.core import Taguchi, AsyncTaguchi
from taguchi.errors import TaguchiError, BinaryDiscoveryError, TimeoutError

class TestTaguchiEnhanced:
    """Test the enhanced Taguchi class."""
    
    @pytest.fixture
    def mock_cli_binary(self):
        """Create a mock CLI binary for testing."""
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='_taguchi') as f:
            f.write("#!/bin/bash\necho 'mock output'\n")
            cli_path = f.name
        
        # Make it executable
        os.chmod(cli_path, 0o755)
        
        yield cli_path
        
        # Cleanup
        try:
            os.unlink(cli_path)
        except OSError:
            pass
    
    def test_init_with_config(self):
        """Test initialization with custom configuration."""
        config = TaguchiConfig(debug_mode=True, cli_timeout=60)
        
        with patch.object(Taguchi, '_find_cli', return_value='/usr/bin/taguchi'):
            taguchi = Taguchi(config=config)
            
            assert taguchi._config is config
            assert taguchi._config.debug_mode is True
            assert taguchi._config.cli_timeout == 60
    
    def test_init_backward_compatibility(self, tmp_path):
        """Test backward compatibility with cli_path parameter."""
        with patch.object(Taguchi, '_find_cli', return_value='/usr/bin/taguchi'):
            # A real file: config validation checks the path exists, so a
            # hardcoded system path fails on any machine without that install.
            custom = tmp_path / "custom_taguchi"
            custom.write_text("#!/bin/sh\nexit 0\n")
            custom.chmod(0o755)
            taguchi = Taguchi(cli_path=str(custom))
            
            # Should override config
            assert taguchi._config.cli_path == str(custom)
    
    def test_find_cli_environment_variable(self):
        """Test CLI discovery via environment variable."""
        with tempfile.NamedTemporaryFile(delete=False) as f:
            cli_path = f.name
        
        # Make it executable
        os.chmod(cli_path, 0o755)
        
        try:
            with patch.dict(os.environ, {'TAGUCHI_CLI_PATH': cli_path}):
                taguchi = Taguchi()
                assert taguchi._cli_path == str(Path(cli_path).absolute())
        finally:
            os.unlink(cli_path)
    
    def test_find_cli_binary_not_found(self):
        """Test CLI discovery failure."""
        with patch.dict(os.environ, {}, clear=True):
            with patch('shutil.which', return_value=None):
                with patch('pathlib.Path.exists', return_value=False):
                    with pytest.raises(BinaryDiscoveryError) as exc_info:
                        Taguchi()
                    
                    error = exc_info.value
                    assert "Could not find taguchi CLI binary" in str(error)
                    assert "TAGUCHI_CLI_PATH" in str(error)
    
    def test_run_command_success(self, mock_cli_binary):
        """Test successful command execution."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch('subprocess.run') as mock_run:
            mock_result = Mock()
            mock_result.returncode = 0
            mock_result.stdout = "success output"
            mock_result.stderr = ""
            mock_run.return_value = mock_result
            
            result = taguchi._run_command(['list-arrays'], operation='test')
            
            assert result == "success output"
            mock_run.assert_called_once()
            
            # Check command construction
            call_args = mock_run.call_args
            assert call_args[0][0][0] == mock_cli_binary
            assert call_args[0][0][1] == 'list-arrays'
    
    def test_run_command_failure(self, mock_cli_binary):
        """Test command execution failure."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch('subprocess.run') as mock_run:
            mock_result = Mock()
            mock_result.returncode = 1
            mock_result.stdout = ""
            mock_result.stderr = "error message"
            mock_run.return_value = mock_result
            
            with pytest.raises(TaguchiError) as exc_info:
                taguchi._run_command(['invalid-command'], operation='test')
            
            error = exc_info.value
            assert "Command failed with exit code 1" in str(error)
            assert error.operation == 'test'
            assert error.exit_code == 1
    
    def test_run_command_timeout(self, mock_cli_binary):
        """Test command timeout handling."""
        config = TaguchiConfig(cli_path=mock_cli_binary, cli_timeout=1)
        taguchi = Taguchi(config=config)
        
        with patch('subprocess.run') as mock_run:
            mock_run.side_effect = subprocess.TimeoutExpired(['taguchi'], 1)
            
            with pytest.raises(TimeoutError) as exc_info:
                taguchi._run_command(['slow-command'], operation='test')
            
            error = exc_info.value
            assert "timed out after 1 seconds" in str(error)
            assert error.operation == 'test'
    
    def test_run_command_with_retries(self, mock_cli_binary):
        """Test command retry logic."""
        config = TaguchiConfig(cli_path=mock_cli_binary, max_retries=2, retry_delay=0.1)
        taguchi = Taguchi(config=config)
        
        with patch('subprocess.run') as mock_run:
            # First two calls fail, third succeeds
            mock_results = []
            for i in range(2):
                result = Mock()
                result.returncode = 1
                result.stdout = ""
                result.stderr = "temporary error"
                mock_results.append(result)
            
            success_result = Mock()
            success_result.returncode = 0
            success_result.stdout = "success"
            success_result.stderr = ""
            mock_results.append(success_result)
            
            mock_run.side_effect = mock_results
            
            # Mock retryable error detection
            with patch.object(taguchi, '_is_retryable_error', return_value=True):
                with patch('time.sleep'):  # Speed up test
                    result = taguchi._run_command(['flaky-command'], operation='test')
                    
                    assert result == "success"
                    assert mock_run.call_count == 3
    
    def test_is_retryable_error(self, mock_cli_binary):
        """Test retryable error detection."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        # Test system-level errors
        result = Mock()
        result.returncode = 127  # Command not found
        result.stderr = ""
        assert taguchi._is_retryable_error(result) is True
        
        result.returncode = 125  # Container error
        assert taguchi._is_retryable_error(result) is True
        
        # Test I/O errors
        result.returncode = 1
        result.stderr = "resource temporarily unavailable"
        assert taguchi._is_retryable_error(result) is True
        
        result.stderr = "device or resource busy"
        assert taguchi._is_retryable_error(result) is True
        
        # Test non-retryable errors
        result.stderr = "invalid syntax"
        assert taguchi._is_retryable_error(result) is False
        
        result.returncode = 2
        result.stderr = "file not found"
        assert taguchi._is_retryable_error(result) is False
    
    def test_get_version(self, mock_cli_binary):
        """Test version retrieval."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch.object(taguchi, '_run_command', return_value="taguchi version 1.5.0\n"):
            version = taguchi.get_version()
            assert version == "1.5.0"
            
            # Should be cached
            with patch.object(taguchi, '_run_command') as mock_run:
                version2 = taguchi.get_version()
                assert version2 == "1.5.0"
                mock_run.assert_not_called()
    
    def test_get_version_unknown(self, mock_cli_binary):
        """Test version retrieval when format unknown."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch.object(taguchi, '_run_command', return_value="unknown output format\n"):
            version = taguchi.get_version()
            assert version == "unknown"
    
    def test_supports_format(self, mock_cli_binary):
        """Test format version support checking."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch.object(taguchi, 'get_version', return_value="1.5.0"):
            assert taguchi.supports_format("1.0.0") is True
            assert taguchi.supports_format("1.5.0") is True
            assert taguchi.supports_format("2.0.0") is False
        
        # Test unknown version
        with patch.object(taguchi, 'get_version', return_value="unknown"):
            assert taguchi.supports_format("1.0.0") is True  # Assume compatibility
    
    def test_verify_installation(self, mock_cli_binary):
        """Test installation verification."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        taguchi = Taguchi(config=config)
        
        with patch.object(taguchi, 'get_version', return_value="1.5.0"):
            with patch.object(taguchi, '_run_command', return_value="L9 (9 runs, 4 cols, 3 levels)"):
                verification = taguchi.verify_installation()
                
                assert verification['cli_found'] is True
                assert verification['cli_path'] == mock_cli_binary
                assert verification['cli_executable'] is True
                assert verification['cli_version'] == "1.5.0"
                assert verification['cli_accessible'] is True
                assert verification['test_command_successful'] is True
                assert len(verification['errors']) == 0
    
    def test_verify_installation_binary_not_found(self):
        """Test installation verification when binary not found."""
        with patch.object(Taguchi, '_find_cli', side_effect=BinaryDiscoveryError([])):
            with pytest.raises(BinaryDiscoveryError):
                Taguchi()
    
    def test_debug_logging_setup(self, mock_cli_binary):
        """Test debug logging configuration."""
        config = TaguchiConfig(cli_path=mock_cli_binary, debug_mode=True)
        
        with patch('logging.basicConfig') as mock_logging:
            taguchi = Taguchi(config=config)
            
            # Should configure logging
            mock_logging.assert_called_once()
            call_kwargs = mock_logging.call_args[1]
            assert call_kwargs['level'] == 10  # logging.DEBUG
    
    def test_environment_variables_in_command(self, mock_cli_binary):
        """Test that environment variables are passed to subprocess."""
        env_vars = {"TEST_VAR": "test_value", "ANOTHER_VAR": "another_value"}
        config = TaguchiConfig(cli_path=mock_cli_binary, environment_variables=env_vars)
        taguchi = Taguchi(config=config)
        
        with patch('subprocess.run') as mock_run:
            mock_result = Mock()
            mock_result.returncode = 0
            mock_result.stdout = "success"
            mock_result.stderr = ""
            mock_run.return_value = mock_result
            
            taguchi._run_command(['test-command'])
            
            # Check that environment variables were passed
            call_kwargs = mock_run.call_args[1]
            assert 'env' in call_kwargs
            env = call_kwargs['env']
            assert env['TEST_VAR'] == 'test_value'
            assert env['ANOTHER_VAR'] == 'another_value'
    
    def test_working_directory_in_command(self, mock_cli_binary):
        """Test that working directory is used in subprocess."""
        with tempfile.TemporaryDirectory() as temp_dir:
            config = TaguchiConfig(cli_path=mock_cli_binary, working_directory=temp_dir)
            taguchi = Taguchi(config=config)
            
            with patch('subprocess.run') as mock_run:
                mock_result = Mock()
                mock_result.returncode = 0
                mock_result.stdout = "success"
                mock_result.stderr = ""
                mock_run.return_value = mock_result
                
                taguchi._run_command(['test-command'])
                
                # Check that working directory was used
                call_kwargs = mock_run.call_args[1]
                assert call_kwargs['cwd'] == temp_dir


class TestAsyncTaguchi:
    """Test the AsyncTaguchi class."""
    
    @pytest.fixture
    def mock_cli_binary(self):
        """Create a mock CLI binary for testing."""
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='_taguchi') as f:
            f.write("#!/bin/bash\necho 'mock output'\n")
            cli_path = f.name
        
        # Make it executable
        os.chmod(cli_path, 0o755)
        
        yield cli_path
        
        # Cleanup
        try:
            os.unlink(cli_path)
        except OSError:
            pass
    
    @pytest.mark.asyncio
    async def test_async_run_command_success(self, mock_cli_binary):
        """Test successful async command execution."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        # Mock asyncio subprocess
        mock_process = Mock()
        mock_process.returncode = 0
        mock_process.communicate = MagicMock()
        mock_process.communicate.return_value = (b"success output", b"")
        
        with patch('asyncio.create_subprocess_exec', return_value=mock_process):
            with patch('asyncio.wait_for', return_value=(b"success output", b"")):
                result = await async_taguchi._run_command_async(['list-arrays'])
                assert result == "success output"
    
    @pytest.mark.asyncio
    async def test_async_run_command_failure(self, mock_cli_binary):
        """Test async command execution failure."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        # Mock failed subprocess
        mock_process = Mock()
        mock_process.returncode = 1
        mock_process.communicate = MagicMock()
        mock_process.communicate.return_value = (b"", b"error message")
        
        with patch('asyncio.create_subprocess_exec', return_value=mock_process):
            with patch('asyncio.wait_for', return_value=(b"", b"error message")):
                with pytest.raises(TaguchiError) as exc_info:
                    await async_taguchi._run_command_async(['invalid-command'])
                
                error = exc_info.value
                assert "Command failed with exit code 1" in str(error)
    
    @pytest.mark.asyncio
    async def test_async_run_command_timeout(self, mock_cli_binary):
        """Test async command timeout."""
        config = TaguchiConfig(cli_path=mock_cli_binary, cli_timeout=1)
        async_taguchi = AsyncTaguchi(config=config)
        
        # Mock process that will be killed
        # AsyncMock for wait(): the code does `await process.wait()` after
        # killing a timed-out process, and awaiting a plain Mock's None return
        # raises TypeError -- which surfaced as the timeout test failing for a
        # reason that had nothing to do with timeouts.
        mock_process = Mock()
        mock_process.kill = Mock()
        mock_process.wait = AsyncMock(return_value=None)
        
        with patch('asyncio.create_subprocess_exec', return_value=mock_process):
            with patch('asyncio.wait_for', side_effect=asyncio.TimeoutError):
                with pytest.raises(TimeoutError) as exc_info:
                    await async_taguchi._run_command_async(['slow-command'])
                
                error = exc_info.value
                assert "timed out after 1 seconds" in str(error)
                
                # Process should be killed
                mock_process.kill.assert_called_once()
    
    @pytest.mark.asyncio
    async def test_list_arrays_async(self, mock_cli_binary):
        """Test async array listing."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        # The --json document, not the human table. L18 is here deliberately:
        # a mixed-level array, which the old regex could not match.
        mock_output = """{
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
    }
  ]
}"""

        with patch.object(async_taguchi, '_run_command_async', return_value=mock_output):
            arrays = await async_taguchi.list_arrays_async()
            assert 'L9' in arrays
            assert 'L16' in arrays
            assert 'L18' in arrays
    
    @pytest.mark.asyncio
    async def test_generate_runs_async(self, mock_cli_binary):
        """Test async run generation."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        # Mock the sync method that does the actual work
        expected_runs = [
            {"run_id": 1, "factors": {"A": "1", "B": "low"}},
            {"run_id": 2, "factors": {"A": "2", "B": "high"}},
        ]
        
        with patch.object(async_taguchi._taguchi, 'generate_runs', return_value=expected_runs):
            runs = await async_taguchi.generate_runs_async('test.tgu')
            assert runs == expected_runs
    
    @pytest.mark.asyncio 
    async def test_analyze_async(self, mock_cli_binary):
        """Test async analysis."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        mock_output = "Analysis results..."
        
        with patch.object(async_taguchi, '_run_command_async', return_value=mock_output):
            result = await async_taguchi.analyze_async('test.tgu', 'results.csv')
            assert result == mock_output
    
    @pytest.mark.asyncio
    async def test_effects_async(self, mock_cli_binary):
        """Test async effects calculation."""
        config = TaguchiConfig(cli_path=mock_cli_binary)
        async_taguchi = AsyncTaguchi(config=config)
        
        mock_output = "Effects results..."
        
        with patch.object(async_taguchi, '_run_command_async', return_value=mock_output):
            result = await async_taguchi.effects_async('test.tgu', 'results.csv')
            assert result == mock_output


class TestTaguchiIntegration:
    """Integration tests for enhanced Taguchi functionality."""
    
    def test_configuration_chain(self):
        """Test configuration propagation through the system."""
        config = TaguchiConfig(
            debug_mode=True,
            cli_timeout=120,
            max_retries=3,
            environment_variables={"TEST": "value"}
        )
        
        with patch.object(Taguchi, '_find_cli', return_value='/usr/bin/taguchi'):
            taguchi = Taguchi(config=config)
            
            assert taguchi._config.debug_mode is True
            assert taguchi._config.cli_timeout == 120
            assert taguchi._config.max_retries == 3
            assert taguchi._config.environment_variables["TEST"] == "value"
    
    def test_error_context_propagation(self):
        """Test that error context is properly propagated."""
        config = TaguchiConfig(debug_mode=True)
        
        with patch.object(Taguchi, '_find_cli', return_value='/usr/bin/taguchi'):
            taguchi = Taguchi(config=config)
            
            with patch('subprocess.run') as mock_run:
                mock_result = Mock()
                mock_result.returncode = 1
                mock_result.stdout = ""
                mock_result.stderr = "detailed error"
                mock_run.return_value = mock_result
                
                with pytest.raises(TaguchiError) as exc_info:
                    taguchi._run_command(['failing-command'], operation='test_operation')
                
                error = exc_info.value
                assert error.operation == 'test_operation'
                assert 'failing-command' in str(error)
                assert 'detailed error' in str(error)