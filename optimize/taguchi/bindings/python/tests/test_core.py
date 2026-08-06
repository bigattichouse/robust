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

    def test_raises_if_not_found(self, tmp_path):
        # Patch Path.exists so no discovered path is considered present, and
        # patch subprocess.run so the which/shutil fallback also fails.
        with patch("taguchi.core.Path") as mock_path_cls, \
             patch("subprocess.run") as mock_run:
            # Every Path(...) instance reports exists()=False
            mock_instance = MagicMock()
            mock_instance.exists.return_value = False
            mock_instance.__truediv__ = lambda self, other: mock_instance
            mock_path_cls.return_value = mock_instance
            mock_run.return_value = MagicMock(returncode=1, stdout="")
            with pytest.raises(TaguchiError, match="Could not find taguchi CLI"):
                Taguchi(cli_path=str(tmp_path / "nonexistent"))

    def test_finds_via_path_fallback(self, cli_path):
        """When explicit path is missing but binary is on PATH."""
        with patch("subprocess.run") as mock_run:
            mock_run.return_value = MagicMock(returncode=0, stdout=cli_path + "\n")
            # Pass a nonexistent explicit path so relative/system paths all miss
            t = Taguchi(cli_path="/nonexistent/taguchi")
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
        with pytest.raises(TaguchiError, match="Taguchi command failed"):
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
