"""
Tests for taguchi.experiment — Experiment class.
"""

import os
import tempfile
import pytest
from taguchi.experiment import Experiment
from taguchi.core import TaguchiError
from .conftest import CLI_PATH


@pytest.fixture(autouse=True)
def use_cli(monkeypatch):
    """Ensure all Taguchi instances in this module use the known CLI path."""
    from taguchi import core
    original_find = core.Taguchi._find_cli

    def patched_find(self, cli_path):
        return CLI_PATH

    monkeypatch.setattr(core.Taguchi, "_find_cli", patched_find)


class TestInputValidation:
    def test_rejects_name_with_equals(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="invalid character"):
            exp.add_factor("bad=name", ["a", "b"])

    def test_rejects_name_with_hash(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="invalid character"):
            exp.add_factor("bad#name", ["a", "b"])

    def test_rejects_name_with_colon(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="invalid character"):
            exp.add_factor("bad:name", ["a", "b"])

    def test_rejects_empty_name(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="empty"):
            exp.add_factor("", ["a", "b"])

    def test_rejects_empty_levels(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="at least one level"):
            exp.add_factor("x", [])

    def test_rejects_non_string_levels(self):
        exp = Experiment()
        with pytest.raises(TaguchiError, match="must be strings"):
            exp.add_factor("x", [1, 2, 3])  # type: ignore

    def test_valid_name_with_underscore(self):
        exp = Experiment()
        exp.add_factor("layer_count", ["4", "8"])
        assert "layer_count" in exp.factors


class TestAddFactor:
    def test_add_single_factor(self):
        exp = Experiment()
        result = exp.add_factor("temp", ["low", "med", "high"])
        assert "temp" in exp.factors
        assert exp.factors["temp"] == ["low", "med", "high"]

    def test_add_factor_returns_self(self):
        exp = Experiment()
        result = exp.add_factor("x", ["a", "b"])
        assert result is exp

    def test_chaining(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2"]).add_factor("b", ["x", "y"])
        assert "a" in exp.factors
        assert "b" in exp.factors

    def test_factors_are_copied(self):
        levels = ["a", "b", "c"]
        exp = Experiment()
        exp.add_factor("f", levels)
        levels.append("d")
        assert "d" not in exp.factors["f"]

    def test_cannot_add_factor_after_generate(self):
        exp = Experiment()
        exp.add_factor("depth", ["4", "6", "8"])
        exp.add_factor("lr", ["0.01", "0.1"])
        exp.generate()
        with pytest.raises(TaguchiError, match="Cannot add factors after runs are generated"):
            exp.add_factor("new_factor", ["a", "b"])

    def test_factors_property_returns_copy(self):
        exp = Experiment()
        exp.add_factor("x", ["1", "2"])
        f = exp.factors
        f["injected"] = ["evil"]
        assert "injected" not in exp.factors


class TestGenerate:
    def test_returns_list(self):
        exp = Experiment()
        exp.add_factor("depth", ["4", "6", "8"])
        exp.add_factor("lr", ["0.02", "0.04", "0.08"])
        runs = exp.generate()
        assert isinstance(runs, list)
        assert len(runs) > 0

    def test_generate_is_idempotent(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.add_factor("b", ["x", "y", "z"])
        runs1 = exp.generate()
        runs2 = exp.generate()
        assert runs1 == runs2

    def test_no_factors_raises(self):
        exp = Experiment()
        with pytest.raises(TaguchiError):
            exp.generate()

    def test_num_runs_matches_generate(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.add_factor("b", ["x", "y", "z"])
        runs = exp.generate()
        assert exp.num_runs == len(runs)

    def test_explicit_array_type(self):
        exp = Experiment(array_type="L9")
        exp.add_factor("a", ["1", "2", "3"])
        exp.add_factor("b", ["x", "y", "z"])
        runs = exp.generate()
        assert exp.array_type == "L9"
        assert len(runs) == 9


class TestArrayType:
    def test_auto_select_3level(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.add_factor("b", ["x", "y", "z"])
        assert exp.array_type is not None
        assert exp.array_type.startswith("L")

    def test_array_type_not_none_after_generate(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.generate()
        assert exp.array_type is not None

    def test_get_array_info(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.add_factor("b", ["x", "y", "z"])
        info = exp.get_array_info()
        assert info is not None
        assert "rows" in info
        assert "cols" in info
        assert "levels" in info


class TestToTguAndSave:
    def test_to_tgu_contains_factors(self):
        exp = Experiment()
        exp.add_factor("depth", ["4", "6", "8"])
        exp.add_factor("lr", ["0.01", "0.1"])
        tgu = exp.to_tgu()
        assert "depth" in tgu
        assert "lr" in tgu
        assert "factors:" in tgu

    def test_save_creates_file(self, tmp_path):
        exp = Experiment()
        exp.add_factor("x", ["a", "b", "c"])
        path = str(tmp_path / "test.tgu")
        exp.save(path)
        assert os.path.exists(path)
        with open(path) as f:
            content = f.read()
        assert "x" in content


class TestFromTgu:
    def test_round_trip(self, tmp_path):
        exp = Experiment()
        exp.add_factor("depth", ["4", "6", "8"])
        exp.add_factor("lr", ["0.02", "0.04", "0.08"])
        path = str(tmp_path / "round_trip.tgu")
        exp.save(path)

        exp2 = Experiment.from_tgu(path)
        assert set(exp2.factors.keys()) == {"depth", "lr"}
        assert exp2.factors["depth"] == ["4", "6", "8"]
        assert exp2.factors["lr"] == ["0.02", "0.04", "0.08"]

    def test_from_tgu_with_array_type(self, tmp_path):
        p = tmp_path / "with_array.tgu"
        p.write_text("factors:\n  x: 1, 2, 3\n  y: a, b, c\narray: L9\n")
        exp = Experiment.from_tgu(str(p))
        assert exp._array_type == "L9"

    def test_from_tgu_ignores_full_line_comments(self, tmp_path):
        p = tmp_path / "comments.tgu"
        p.write_text(
            "# This is an experiment\n"
            "factors:\n"
            "  depth: 4, 6, 8\n"
            "# another comment\n"
            "  lr: 0.01, 0.1\n"
        )
        exp = Experiment.from_tgu(str(p))
        assert "depth" in exp.factors
        assert "lr" in exp.factors

    def test_from_tgu_strips_inline_comments(self, tmp_path):
        p = tmp_path / "inline_comments.tgu"
        p.write_text(
            "factors:\n"
            "  depth: 4, 6, 8  # layer count\n"
            "  lr: 0.01, 0.1   # learning rate\n"
        )
        exp = Experiment.from_tgu(str(p))
        assert exp.factors["depth"] == ["4", "6", "8"]
        assert exp.factors["lr"] == ["0.01", "0.1"]

    def test_from_tgu_handles_blank_lines(self, tmp_path):
        p = tmp_path / "blanks.tgu"
        p.write_text(
            "\nfactors:\n\n  depth: 4, 6, 8\n\n  lr: 0.01, 0.1\n\n"
        )
        exp = Experiment.from_tgu(str(p))
        assert "depth" in exp.factors
        assert "lr" in exp.factors

    def test_from_tgu_raises_on_empty_file(self, tmp_path):
        p = tmp_path / "empty.tgu"
        p.write_text("# just a comment\n")
        with pytest.raises(TaguchiError, match="No factors found"):
            Experiment.from_tgu(str(p))

    def test_from_tgu_can_generate(self, tmp_path):
        p = tmp_path / "gen.tgu"
        p.write_text("factors:\n  a: 1, 2, 3\n  b: x, y, z\n")
        exp = Experiment.from_tgu(str(p))
        runs = exp.generate()
        assert len(runs) > 0


class TestGetTguPath:
    def test_returns_existing_file_path(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        path = exp.get_tgu_path()
        assert os.path.exists(path)
        exp.cleanup()

    def test_same_path_on_repeated_calls(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        p1 = exp.get_tgu_path()
        p2 = exp.get_tgu_path()
        assert p1 == p2
        exp.cleanup()

    def test_cleanup_removes_file(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        path = exp.get_tgu_path()
        assert os.path.exists(path)
        exp.cleanup()
        assert not os.path.exists(path)

    def test_cleanup_is_idempotent(self):
        exp = Experiment()
        exp.add_factor("a", ["1", "2", "3"])
        exp.get_tgu_path()
        exp.cleanup()
        exp.cleanup()  # must not raise


class TestContextManager:
    def test_context_manager_cleans_up_tgu_file(self):
        saved_path = None
        with Experiment() as exp:
            exp.add_factor("a", ["1", "2", "3"])
            exp.add_factor("b", ["x", "y", "z"])
            exp.generate()
            saved_path = exp._tgu_path
            assert saved_path is not None
            assert os.path.exists(saved_path)
        # After __exit__, file should be gone
        assert not os.path.exists(saved_path)

    def test_context_manager_cleans_up_on_exception(self):
        saved_path = None
        try:
            with Experiment() as exp:
                exp.add_factor("a", ["1", "2", "3"])
                exp.generate()
                saved_path = exp._tgu_path
                raise RuntimeError("simulated error")
        except RuntimeError:
            pass
        if saved_path:
            assert not os.path.exists(saved_path)

    def test_context_manager_returns_experiment(self):
        with Experiment() as exp:
            assert isinstance(exp, Experiment)

    def test_no_tgu_file_before_generate(self):
        with Experiment() as exp:
            exp.add_factor("a", ["1", "2"])
            assert exp._tgu_path is None
        # No file was created, nothing to clean up
