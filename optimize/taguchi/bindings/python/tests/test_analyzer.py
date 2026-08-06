"""
Tests for taguchi.analyzer — Analyzer class.
"""

import os
import pytest
from taguchi.experiment import Experiment
from taguchi.analyzer import Analyzer
from taguchi.core import TaguchiError
from .conftest import CLI_PATH


@pytest.fixture(autouse=True)
def use_cli(monkeypatch):
    from taguchi import core
    monkeypatch.setattr(core.Taguchi, "_find_cli", lambda self, _: CLI_PATH)


@pytest.fixture
def two_factor_exp():
    exp = Experiment()
    exp.add_factor("depth", ["4", "6", "8"])
    exp.add_factor("lr", ["0.02", "0.04", "0.08"])
    exp.generate()
    return exp


@pytest.fixture
def analyzer_with_results(two_factor_exp):
    analyzer = Analyzer(two_factor_exp, metric_name="val_loss")
    results = {1: 1.05, 2: 1.02, 3: 1.08,
               4: 1.03, 5: 0.998, 6: 1.045,
               7: 1.04, 8: 1.015, 9: 1.055}
    analyzer.add_results_from_dict(results)
    return analyzer


class TestAddResult:
    def test_add_single_result(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        result = a.add_result(1, 0.95)
        assert a._results[1] == 0.95

    def test_add_result_returns_self(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        assert a.add_result(1, 0.5) is a

    def test_add_results_from_dict(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.add_results_from_dict({1: 1.0, 2: 2.0, 3: 3.0})
        assert a._results == {1: 1.0, 2: 2.0, 3: 3.0}

    def test_add_result_invalidates_cache(self, analyzer_with_results):
        analyzer_with_results.main_effects()  # populate cache
        assert analyzer_with_results._effects is not None
        analyzer_with_results.add_result(1, 99.0)
        assert analyzer_with_results._effects is None

    def test_overwrite_result(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.add_result(1, 1.0)
        a.add_result(1, 2.0)
        assert a._results[1] == 2.0

    def test_chaining(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.add_result(1, 0.5).add_result(2, 0.6).add_result(3, 0.7)
        assert len(a._results) == 3


class TestEmptyResultsGuard:
    def test_main_effects_raises_with_no_results(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        with pytest.raises(TaguchiError, match="No results added"):
            a.main_effects()

    def test_recommend_optimal_raises_with_no_results(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        with pytest.raises(TaguchiError, match="No results added"):
            a.recommend_optimal()


class TestMetricPassthrough:
    def test_metric_name_in_csv(self, two_factor_exp, tmp_path):
        """The metric name must appear as the CSV header column."""
        a = Analyzer(two_factor_exp, metric_name="my_score")
        a.add_result(1, 1.0)
        _, csv_path = a._ensure_files()
        with open(csv_path) as f:
            header = f.readline()
        assert "my_score" in header
        a.cleanup()


class TestParseEffects:
    def test_handles_negative_level_means(self, two_factor_exp):
        """_parse_effects must not drop entries with negative mean values."""
        a = Analyzer(two_factor_exp)
        fake_output = (
            "depth    0.500   L1=-0.200, L2=0.100, L3=0.300\n"
            "lr       0.200   L1=0.050, L2=-0.150, L3=0.200\n"
        )
        effects = a._parse_effects(fake_output)
        assert len(effects) == 2
        assert effects[0]["level_means"][0] == -0.200
        assert effects[1]["level_means"][1] == -0.150

    def test_skips_non_matching_lines(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        # Build explicitly to avoid the adjacent-literal-concatenation gotcha
        fake_output = "\n".join([
            "Factor           Range   Level Means",
            "depth    0.026   L1=1.050, L2=1.024, L3=1.037",
            "",
            "=" * 40,
        ])
        effects = a._parse_effects(fake_output)
        assert len(effects) == 1
        assert effects[0]["factor"] == "depth"


class TestCleanup:
    def test_cleanup_removes_csv(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.add_result(1, 1.0)
        a._ensure_files()
        path = a._csv_path
        assert os.path.exists(path)
        a.cleanup()
        assert not os.path.exists(path)
        assert a._csv_path is None

    def test_cleanup_idempotent(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.cleanup()  # no CSV created — must not raise
        a.cleanup()


class TestMainEffects:
    def test_returns_list(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        assert isinstance(effects, list)

    def test_effects_have_expected_keys(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        assert len(effects) > 0
        for e in effects:
            assert "factor" in e
            assert "range" in e
            assert "level_means" in e

    def test_effects_contain_factor_names(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        factor_names = {e["factor"] for e in effects}
        assert "depth" in factor_names
        assert "lr" in factor_names

    def test_level_means_are_floats(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        for e in effects:
            assert all(isinstance(m, float) for m in e["level_means"])

    def test_range_is_non_negative(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        for e in effects:
            assert e["range"] >= 0.0

    def test_effects_are_cached(self, analyzer_with_results):
        e1 = analyzer_with_results.main_effects()
        e2 = analyzer_with_results.main_effects()
        assert e1 is e2  # same object — from cache

    def test_no_results_raises_clearly(self, two_factor_exp):
        """With no results added, main_effects must raise TaguchiError."""
        a = Analyzer(two_factor_exp)
        with pytest.raises(TaguchiError, match="No results added"):
            a.main_effects()


class TestRecommendOptimal:
    def test_returns_dict(self, analyzer_with_results):
        optimal = analyzer_with_results.recommend_optimal()
        assert isinstance(optimal, dict)

    def test_optimal_keys_are_factor_names(self, analyzer_with_results):
        optimal = analyzer_with_results.recommend_optimal()
        for key in optimal.keys():
            assert key in {"depth", "lr"}

    def test_optimal_values_are_valid_levels(self, analyzer_with_results, two_factor_exp):
        optimal = analyzer_with_results.recommend_optimal()
        valid = {
            "depth": {"4", "6", "8"},
            "lr": {"0.02", "0.04", "0.08"},
        }
        for factor, level in optimal.items():
            assert level in valid[factor], (
                f"Optimal level '{level}' for '{factor}' not in valid levels"
            )

    def test_higher_is_better(self, two_factor_exp):
        """With a clearly dominant level, higher_is_better should pick it."""
        # depth=8 always produces highest response
        a = Analyzer(two_factor_exp)
        runs = two_factor_exp.generate()
        for run in runs:
            base = 1.0
            if run["factors"]["depth"] == "8":
                base += 0.5
            a.add_result(run["run_id"], base)
        optimal = a.recommend_optimal(higher_is_better=True)
        assert optimal.get("depth") == "8"

    def test_lower_is_better(self, two_factor_exp):
        """With a clearly dominant level, lower_is_better should pick it."""
        a = Analyzer(two_factor_exp)
        runs = two_factor_exp.generate()
        for run in runs:
            base = 1.0
            if run["factors"]["depth"] == "4":
                base -= 0.5
            a.add_result(run["run_id"], base)
        optimal = a.recommend_optimal(higher_is_better=False)
        assert optimal.get("depth") == "4"

    def test_no_runs_raises(self, two_factor_exp):
        a = Analyzer(two_factor_exp)
        a.add_result(1, 1.0)
        # Experiment has runs but Analyzer doesn't know yet — still should not crash
        # (full error path requires no runs at all)


class TestGetSignificantFactors:
    def test_returns_list(self, analyzer_with_results):
        sig = analyzer_with_results.get_significant_factors()
        assert isinstance(sig, list)

    def test_threshold_zero_returns_all(self, analyzer_with_results):
        all_factors = {e["factor"] for e in analyzer_with_results.main_effects()}
        sig = analyzer_with_results.get_significant_factors(threshold=0.0)
        assert set(sig) == all_factors

    def test_threshold_one_returns_max_only(self, analyzer_with_results):
        effects = analyzer_with_results.main_effects()
        if not effects:
            pytest.skip("No effects parsed")
        max_factor = max(effects, key=lambda e: e["range"])["factor"]
        sig = analyzer_with_results.get_significant_factors(threshold=1.0)
        assert sig == [max_factor]

    def test_zero_range_returns_empty(self, two_factor_exp):
        """All identical results → zero range → no significant factors."""
        a = Analyzer(two_factor_exp)
        runs = two_factor_exp.generate()
        for run in runs:
            a.add_result(run["run_id"], 1.0)
        sig = a.get_significant_factors()
        assert sig == []


class TestSummary:
    def test_returns_string(self, analyzer_with_results):
        s = analyzer_with_results.summary()
        assert isinstance(s, str)
        assert len(s) > 0

    def test_summary_contains_metric_name(self, analyzer_with_results):
        s = analyzer_with_results.summary()
        assert "val_loss" in s

    def test_summary_contains_factor_names(self, analyzer_with_results):
        s = analyzer_with_results.summary()
        assert "depth" in s
        assert "lr" in s


class TestContextManager:
    def test_cleans_up_csv_file(self, two_factor_exp):
        saved_path = None
        with Analyzer(two_factor_exp, metric_name="score") as a:
            results = {1: 1.0, 2: 1.1, 3: 1.2,
                       4: 1.0, 5: 1.1, 6: 1.2,
                       7: 1.0, 8: 1.1, 9: 1.2}
            a.add_results_from_dict(results)
            a.main_effects()  # forces CSV creation
            saved_path = a._csv_path
            assert saved_path is not None
            assert os.path.exists(saved_path)
        assert not os.path.exists(saved_path)

    def test_cleans_up_on_exception(self, two_factor_exp):
        saved_path = None
        try:
            with Analyzer(two_factor_exp) as a:
                a.add_result(1, 1.0)
                a.main_effects()
                saved_path = a._csv_path
                raise RuntimeError("simulated")
        except RuntimeError:
            pass
        if saved_path:
            assert not os.path.exists(saved_path)
