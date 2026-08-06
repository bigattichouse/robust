"""
Analyzer class for Taguchi experimental results.
"""

import tempfile
import os
import csv
import re
from typing import Dict, List, Optional, Any
from .core import Taguchi, TaguchiError


class Analyzer:
    """
    Collect results from a Taguchi experiment and compute main effects.

    Use as a context manager to ensure the temporary results CSV is cleaned up:

        with Analyzer(exp, metric_name="accuracy") as analyzer:
            analyzer.add_results_from_dict({1: 0.92, 2: 0.87, ...})
            print(analyzer.summary())
    """

    def __init__(self, experiment: Any, metric_name: str = "response"):
        self._taguchi = Taguchi()
        self._experiment = experiment
        self._metric_name = metric_name
        self._results: Dict[int, float] = {}
        self._effects: Optional[List[Dict]] = None
        self._csv_path: Optional[str] = None

    # ------------------------------------------------------------------
    # Result collection
    # ------------------------------------------------------------------

    def add_result(self, run_id: int, value: float) -> "Analyzer":
        """Record the measured response for a run. Returns self for chaining."""
        self._results[run_id] = float(value)
        self._effects = None   # invalidate cached analysis
        self._csv_path = None  # invalidate cached CSV
        return self

    def add_results_from_dict(self, results: Dict[int, float]) -> "Analyzer":
        """Record multiple results at once. Returns self for chaining."""
        for run_id, value in results.items():
            self.add_result(run_id, value)
        return self

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _ensure_files(self) -> tuple:
        """Write the results CSV if needed; return (tgu_path, csv_path)."""
        if not self._results:
            raise TaguchiError(
                "No results added. Call add_result() before analyzing."
            )

        if self._csv_path is None:
            fd, path = tempfile.mkstemp(suffix='.csv')
            with os.fdopen(fd, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(['run_id', self._metric_name])
                for run_id, value in sorted(self._results.items()):
                    writer.writerow([run_id, value])
            self._csv_path = path

        # Use the public API — no private method access
        tgu_path = self._experiment.get_tgu_path()
        return tgu_path, self._csv_path

    def _parse_effects(self, output: str) -> List[Dict]:
        """
        Parse the main-effects table from CLI output.

        Expected line format:
            depth    0.026   L1=1.050, L2=1.024, L3=1.037

        Handles:
        - Negative level means (e.g. L1=-0.5)
        - Extra whitespace / header/footer lines (silently skipped)
        """
        effects = []
        for line in output.strip().split('\n'):
            # Factor name (word chars), range (number), then level means
            match = re.match(r'\s*(\w+)\s+([\d.]+)\s+(.+)', line)
            if not match:
                continue

            factor = match.group(1)
            try:
                range_val = float(match.group(2))
            except ValueError:
                continue

            means_str = match.group(3)
            # Match L<n>=<signed float> — handles negatives and scientific notation
            level_matches = re.findall(r'L\d+=(-?[\d.]+(?:[eE][+-]?\d+)?)', means_str)
            means = []
            for m in level_matches:
                try:
                    means.append(float(m))
                except ValueError:
                    pass

            if means:
                effects.append({
                    'factor': factor,
                    'range': range_val,
                    'level_means': means,
                })

        return effects

    def cleanup(self) -> None:
        """Delete the temporary results CSV if one was created."""
        if self._csv_path and os.path.exists(self._csv_path):
            try:
                os.unlink(self._csv_path)
            except OSError:
                pass
        self._csv_path = None

    # ------------------------------------------------------------------
    # Analysis API
    # ------------------------------------------------------------------

    def main_effects(self) -> List[Dict[str, Any]]:
        """
        Calculate and cache main effects for all factors.

        Returns a list of dicts: [{'factor': str, 'range': float,
                                    'level_means': [float, ...]}, ...]
        """
        if self._effects is None:
            tgu_path, csv_path = self._ensure_files()
            output = self._taguchi.effects(
                tgu_path, csv_path, metric=self._metric_name
            )
            self._effects = self._parse_effects(output)
            if not self._effects:
                raise TaguchiError(
                    "effects command produced no parseable output. "
                    "Check that run IDs in results match the experiment."
                )
        return self._effects

    def recommend_optimal(self, higher_is_better: bool = True) -> Dict[str, str]:
        """
        Return the optimal level for each factor.

        Uses definition order for level indexing (L1 = first level defined,
        L2 = second, etc.) — not alphabetical order.
        """
        effects = self.main_effects()

        # factor_levels preserves insertion order (Python 3.7+)
        factor_levels: Dict[str, List[str]] = self._experiment.factors

        optimal: Dict[str, str] = {}
        for effect in effects:
            factor = effect["factor"]
            level_means = effect["level_means"]
            if not level_means or factor not in factor_levels:
                continue

            if higher_is_better:
                best_idx = level_means.index(max(level_means))
            else:
                best_idx = level_means.index(min(level_means))

            levels = factor_levels[factor]
            if best_idx < len(levels):
                optimal[factor] = levels[best_idx]

        return optimal

    def get_significant_factors(self, threshold: float = 0.1) -> List[str]:
        """
        Return factor names whose effect range is >= threshold * max_range.

        threshold=0.1 means factors with at least 10% of the largest effect.
        """
        effects = self.main_effects()
        if not effects:
            return []
        max_range = max(e["range"] for e in effects)
        if max_range == 0:
            return []
        return [e["factor"] for e in effects if e["range"] >= threshold * max_range]

    def summary(self) -> str:
        """Return a formatted text summary of main effects and recommendations."""
        effects = self.main_effects()
        optimal = self.recommend_optimal()

        lines = [
            "=" * 60,
            f"Taguchi Experiment Analysis: {self._metric_name}",
            "=" * 60,
            "",
            "Main Effects (sorted by range, descending):",
            "-" * 40,
        ]

        for effect in sorted(effects, key=lambda e: e["range"], reverse=True):
            factor = effect["factor"]
            range_val = effect["range"]
            means_str = ", ".join(f"{m:.4f}" for m in effect["level_means"])
            lines.append(f"  {factor:20s} range={range_val:8.4f}  means=[{means_str}]")

        lines += [
            "",
            "Recommended Optimal Settings:",
            "-" * 40,
        ]
        for factor, level in optimal.items():
            lines.append(f"  {factor:20s} -> {level}")

        lines += ["", "=" * 60]
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Context manager and finalizer
    # ------------------------------------------------------------------

    def __enter__(self) -> "Analyzer":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.cleanup()

    def __del__(self) -> None:
        try:
            path = self.__dict__.get('_csv_path')
            if path and os.path.exists(path):
                os.unlink(path)
        except Exception:
            pass
