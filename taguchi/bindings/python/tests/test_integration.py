"""
Integration tests — full end-to-end workflows through the Python bindings.
"""

import os
import pytest
from taguchi import Experiment, Analyzer, TaguchiError
from .conftest import CLI_PATH


@pytest.fixture(autouse=True)
def use_cli(monkeypatch):
    from taguchi import core
    monkeypatch.setattr(core.Taguchi, "_find_cli", lambda self, _: CLI_PATH)


class TestFullWorkflow:
    def test_define_generate_analyze_recommend(self):
        """Full pipeline: define → generate → add results → analyze → recommend."""
        with Experiment() as exp:
            exp.add_factor("learning_rate", ["0.001", "0.01", "0.1"])
            exp.add_factor("batch_size", ["32", "64", "128"])
            exp.add_factor("weight_decay", ["0.0", "0.1", "0.2"])

            runs = exp.generate()
            assert len(runs) == 9  # L9 for 3 factors at 3 levels

            # Simulate: lr=0.01 and batch_size=64 are best
            with Analyzer(exp, metric_name="accuracy") as analyzer:
                for run in runs:
                    score = 0.8
                    if run["factors"]["learning_rate"] == "0.01":
                        score += 0.05
                    if run["factors"]["batch_size"] == "64":
                        score += 0.03
                    analyzer.add_result(run["run_id"], score)

                optimal = analyzer.recommend_optimal(higher_is_better=True)
                assert optimal.get("learning_rate") == "0.01"
                assert optimal.get("batch_size") == "64"

    def test_minimize_metric(self):
        """recommend_optimal with higher_is_better=False picks lowest-scoring level."""
        with Experiment() as exp:
            exp.add_factor("depth", ["4", "6", "8"])
            exp.add_factor("lr", ["0.02", "0.04", "0.08"])
            runs = exp.generate()

            with Analyzer(exp, metric_name="loss") as analyzer:
                for run in runs:
                    loss = 1.0
                    if run["factors"]["depth"] == "6":
                        loss -= 0.2
                    if run["factors"]["lr"] == "0.04":
                        loss -= 0.1
                    analyzer.add_result(run["run_id"], loss)

                optimal = analyzer.recommend_optimal(higher_is_better=False)
                assert optimal.get("depth") == "6"
                assert optimal.get("lr") == "0.04"

    def test_significant_factors_detected(self):
        """A factor with a large effect should appear in get_significant_factors."""
        with Experiment() as exp:
            exp.add_factor("important", ["low", "med", "high"])
            exp.add_factor("irrelevant", ["a", "b", "c"])
            runs = exp.generate()

            with Analyzer(exp) as analyzer:
                level_scores = {"low": 1.0, "med": 1.5, "high": 2.0}
                for run in runs:
                    score = level_scores[run["factors"]["important"]]
                    # irrelevant factor contributes noise ±0.01
                    analyzer.add_result(run["run_id"], score)

                sig = analyzer.get_significant_factors(threshold=0.5)
                assert "important" in sig

    def test_save_and_reload_produces_same_runs(self, tmp_path):
        """Saving a .tgu file and reloading it should produce identical runs."""
        exp1 = Experiment()
        exp1.add_factor("depth", ["4", "6", "8"])
        exp1.add_factor("lr", ["0.02", "0.04", "0.08"])
        runs1 = exp1.generate()
        path = str(tmp_path / "saved.tgu")
        exp1.save(path)

        exp2 = Experiment.from_tgu(path)
        runs2 = exp2.generate()

        assert len(runs1) == len(runs2)
        for r1, r2 in zip(runs1, runs2):
            assert r1["run_id"] == r2["run_id"]
            assert r1["factors"] == r2["factors"]

    def test_two_level_experiment(self):
        """2-level factors should use an L4 or L8 array."""
        with Experiment() as exp:
            exp.add_factor("a", ["off", "on"])
            exp.add_factor("b", ["low", "high"])
            exp.add_factor("c", ["slow", "fast"])
            runs = exp.generate()
            assert len(runs) in (4, 8)  # L4 has 3 cols, L8 has 7
            for run in runs:
                assert run["factors"]["a"] in {"off", "on"}
                assert run["factors"]["b"] in {"low", "high"}
                assert run["factors"]["c"] in {"slow", "fast"}

    def test_summary_output_is_readable(self):
        with Experiment() as exp:
            exp.add_factor("x", ["1", "2", "3"])
            exp.add_factor("y", ["a", "b", "c"])
            runs = exp.generate()

            with Analyzer(exp, metric_name="score") as analyzer:
                for i, run in enumerate(runs):
                    analyzer.add_result(run["run_id"], float(i + 1))
                summary = analyzer.summary()
                assert "score" in summary
                assert "=" * 10 in summary

    def test_multiple_experiments_independent(self):
        """Two concurrent Experiment objects should not interfere."""
        exp1 = Experiment()
        exp1.add_factor("a", ["1", "2", "3"])
        exp1.add_factor("b", ["x", "y", "z"])

        exp2 = Experiment()
        exp2.add_factor("p", ["lo", "med", "hi"])
        exp2.add_factor("q", ["A", "B", "C"])

        runs1 = exp1.generate()
        runs2 = exp2.generate()

        # Both should have 9 runs (L9) and their factors should not mix
        assert len(runs1) == 9
        assert len(runs2) == 9
        for run in runs1:
            assert "a" in run["factors"]
            assert "p" not in run["factors"]
        for run in runs2:
            assert "p" in run["factors"]
            assert "a" not in run["factors"]


class TestErrorHandling:
    def test_invalid_tgu_path_raises(self):
        exp = Experiment()
        exp._tgu_path = "/nonexistent/file.tgu"
        from taguchi.core import Taguchi
        t = Taguchi(cli_path=CLI_PATH)
        with pytest.raises(TaguchiError):
            t.generate_runs("/nonexistent/file.tgu")

    def test_no_factors_raises_on_generate(self):
        exp = Experiment()
        with pytest.raises(TaguchiError):
            exp.generate()

    def test_taguchi_error_is_exception(self):
        assert issubclass(TaguchiError, Exception)
