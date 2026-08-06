"""
Enhanced Analyzer class with dependency injection and improved error handling.

Provides Phase 2 improvements while maintaining backward compatibility.
"""

import tempfile
import os
import csv
import re
from typing import Dict, List, Optional, Any, Union

from .core_enhanced import Taguchi
from .config import TaguchiConfig  
from .errors import TaguchiError, ValidationError


class Analyzer:
    """
    Enhanced analyzer for Taguchi experimental results.
    
    Provides dependency injection, improved validation, and better error
    handling while maintaining full backward compatibility.

    Use as a context manager to ensure the temporary results CSV is cleaned up:

        with Analyzer(exp, metric_name="accuracy") as analyzer:
            analyzer.add_results_from_dict({1: 0.92, 2: 0.87, ...})
            print(analyzer.summary())
    
    Or with dependency injection:
    
        config = TaguchiConfig(debug_mode=True)
        taguchi = Taguchi(config=config)
        with Analyzer(exp, metric_name="accuracy", taguchi=taguchi) as analyzer:
            analyzer.add_results_from_dict({1: 0.92, 2: 0.87, ...})
            print(analyzer.summary())
    """

    def __init__(
        self, 
        experiment: Any, 
        metric_name: str = "response",
        taguchi: Optional[Taguchi] = None,
        config: Optional[TaguchiConfig] = None
    ):
        """
        Initialize analyzer.
        
        Args:
            experiment: Experiment instance  
            metric_name: Name of the response metric
            taguchi: Optional Taguchi instance for dependency injection
            config: Optional configuration (if taguchi not provided)
        """
        # Dependency injection with backward compatibility
        if taguchi is not None:
            self._taguchi = taguchi
        else:
            self._taguchi = Taguchi(config=config)
        
        self._experiment = experiment
        self._metric_name = metric_name
        self._results: Dict[int, float] = {}
        self._effects: Optional[List[Dict]] = None
        self._csv_path: Optional[str] = None
        self._validation_cache: Optional[List[str]] = None

    # ------------------------------------------------------------------
    # Validation Methods (Phase 2)
    # ------------------------------------------------------------------

    def validate(self) -> List[str]:
        """
        Return list of validation errors, empty if valid.
        
        Validates:
        - Results coverage (all runs have results)
        - Result value validity
        - Metric name validity
        - Experiment-analyzer consistency
        
        Returns:
            List of error messages. Empty list means valid.
        """
        if self._validation_cache is not None:
            return self._validation_cache
        
        errors = []
        
        # Validate metric name
        if not self._metric_name:
            errors.append("Metric name cannot be empty")
        elif not isinstance(self._metric_name, str):
            errors.append(f"Metric name must be a string, got {type(self._metric_name)}")
        elif not self._metric_name.replace('_', '').replace('-', '').isalnum():
            errors.append(f"Metric name '{self._metric_name}' contains invalid characters")
        
        # Validate experiment
        try:
            experiment_runs = self._experiment.generate()
            expected_run_ids = {run['run_id'] for run in experiment_runs}
        except Exception as e:
            errors.append(f"Cannot access experiment runs: {e}")
            self._validation_cache = errors
            return errors
        
        # Validate results if any have been added
        if self._results:
            result_run_ids = set(self._results.keys())
            
            # Check for results without corresponding runs
            extra_results = result_run_ids - expected_run_ids
            if extra_results:
                errors.append(
                    f"Results provided for non-existent runs: {sorted(extra_results)}"
                )
            
            # Check for missing results
            missing_results = expected_run_ids - result_run_ids
            if missing_results:
                errors.append(
                    f"Missing results for runs: {sorted(missing_results)}"
                )
            
            # Validate individual result values
            for run_id, value in self._results.items():
                try:
                    float_value = float(value)
                    if not (-1e10 < float_value < 1e10):  # Reasonable bounds
                        errors.append(
                            f"Result for run {run_id} is out of reasonable bounds: {value}"
                        )
                except (ValueError, TypeError):
                    errors.append(
                        f"Result for run {run_id} is not a valid number: {value}"
                    )
        
        self._validation_cache = errors
        return errors
    
    def is_valid(self) -> bool:
        """Quick check if analyzer state is valid."""
        return len(self.validate()) == 0
    
    def validate_and_raise(self) -> None:
        """Validate analyzer and raise ValidationError if invalid."""
        errors = self.validate()
        if errors:
            raise ValidationError(errors, "analyzer_validation")
    
    def check_completeness(self) -> Dict[str, Any]:
        """
        Check if all experimental runs have results.
        
        Returns:
            Dictionary with completeness information
        """
        try:
            experiment_runs = self._experiment.generate()
            expected_run_ids = {run['run_id'] for run in experiment_runs}
            result_run_ids = set(self._results.keys())
            
            missing = expected_run_ids - result_run_ids
            extra = result_run_ids - expected_run_ids
            
            return {
                "total_runs": len(expected_run_ids),
                "results_provided": len(result_run_ids),
                "missing_results": sorted(missing),
                "extra_results": sorted(extra),
                "is_complete": len(missing) == 0,
                "completion_percentage": len(result_run_ids) / len(expected_run_ids) * 100
                    if expected_run_ids else 0,
            }
        except Exception as e:
            return {
                "error": f"Cannot check completeness: {e}",
                "is_complete": False,
            }

    # ------------------------------------------------------------------
    # Result collection (enhanced)
    # ------------------------------------------------------------------

    def add_result(self, run_id: int, value: float) -> "Analyzer":
        """Record the measured response for a run. Returns self for chaining."""
        # Validate run_id exists in experiment
        try:
            experiment_runs = self._experiment.generate()
            valid_run_ids = {run['run_id'] for run in experiment_runs}
            
            if run_id not in valid_run_ids:
                raise ValidationError([
                    f"Run ID {run_id} not found in experiment. "
                    f"Valid run IDs: {sorted(valid_run_ids)}"
                ], "result_addition")
        
        except Exception as e:
            if not isinstance(e, ValidationError):
                raise TaguchiError(f"Cannot validate run ID: {e}")
            raise
        
        # Validate value
        try:
            float_value = float(value)
            if not (-1e10 < float_value < 1e10):
                raise ValidationError([
                    f"Result value {value} is out of reasonable bounds"
                ], "result_addition")
        except (ValueError, TypeError):
            raise ValidationError([
                f"Result value {value} is not a valid number"
            ], "result_addition")
        
        self._results[run_id] = float_value
        
        # Invalidate caches
        self._effects = None
        self._csv_path = None
        self._validation_cache = None
        
        return self

    def add_results_from_dict(self, results: Dict[int, float]) -> "Analyzer":
        """Record multiple results at once. Returns self for chaining."""
        for run_id, value in results.items():
            self.add_result(run_id, value)
        return self
    
    def add_results_from_list(self, results: List[float]) -> "Analyzer":
        """
        Add results from a list, matching by position to run order.
        
        Args:
            results: List of result values in run order
            
        Returns:
            Self for chaining
        """
        try:
            experiment_runs = self._experiment.generate()
            if len(results) != len(experiment_runs):
                raise ValidationError([
                    f"Results list length ({len(results)}) does not match "
                    f"number of experimental runs ({len(experiment_runs)})"
                ], "result_addition")
            
            for run, value in zip(experiment_runs, results):
                self.add_result(run['run_id'], value)
            
            return self
        
        except Exception as e:
            if isinstance(e, ValidationError):
                raise
            raise TaguchiError(f"Error adding results from list: {e}")
    
    def remove_result(self, run_id: int) -> "Analyzer":
        """Remove result for a specific run. Returns self for chaining."""
        if run_id not in self._results:
            raise TaguchiError(f"No result found for run {run_id}")
        
        del self._results[run_id]
        
        # Invalidate caches
        self._effects = None
        self._csv_path = None
        self._validation_cache = None
        
        return self
    
    def clear_results(self) -> "Analyzer":
        """Remove all results. Returns self for chaining."""
        self._results.clear()
        
        # Invalidate caches
        self._effects = None
        self._csv_path = None
        self._validation_cache = None
        
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
    # Analysis API (enhanced)
    # ------------------------------------------------------------------

    def main_effects(self) -> List[Dict[str, Any]]:
        """
        Calculate and cache main effects for all factors.

        Returns a list of dicts: [{'factor': str, 'range': float,
                                    'level_means': [float, ...]}, ...]
        """
        # Validate before analysis
        self.validate_and_raise()
        
        if self._effects is None:
            tgu_path, csv_path = self._ensure_files()
            output = self._taguchi.effects(
                tgu_path, csv_path, metric=self._metric_name
            )
            self._effects = self._parse_effects(output)
            if not self._effects:
                raise TaguchiError(
                    "effects command produced no parseable output. "
                    "Check that run IDs in results match the experiment.",
                    operation="effects_calculation",
                    suggestions=[
                        "Verify all run IDs in results match experiment run IDs",
                        "Check that CSV file was created correctly",
                        "Ensure CLI is functioning properly",
                    ],
                    diagnostic_info={
                        "raw_output": output,
                        "results_count": len(self._results),
                        "metric_name": self._metric_name,
                    }
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
    
    def get_factor_rankings(self, higher_is_better: bool = True) -> List[Dict[str, Any]]:
        """
        Get factors ranked by their effect size (range).
        
        Args:
            higher_is_better: Whether higher metric values are better
            
        Returns:
            List of factor info dicts sorted by effect range (descending)
        """
        effects = self.main_effects()
        optimal = self.recommend_optimal(higher_is_better)
        
        rankings = []
        for effect in effects:
            factor = effect["factor"]
            rankings.append({
                "factor": factor,
                "range": effect["range"],
                "level_means": effect["level_means"],
                "optimal_level": optimal.get(factor),
                "relative_importance": 0.0,  # Will be calculated below
            })
        
        # Sort by range (descending)
        rankings.sort(key=lambda x: x["range"], reverse=True)
        
        # Calculate relative importance
        max_range = rankings[0]["range"] if rankings else 0
        if max_range > 0:
            for ranking in rankings:
                ranking["relative_importance"] = ranking["range"] / max_range
        
        return rankings

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
    
    def predict_response(self, factor_settings: Dict[str, str]) -> Dict[str, Any]:
        """
        Predict response for given factor settings using main effects.
        
        Args:
            factor_settings: Dict mapping factor names to level values
            
        Returns:
            Dictionary with prediction information
        """
        effects = self.main_effects()
        
        # Calculate overall mean
        overall_mean = sum(self._results.values()) / len(self._results)
        
        predicted_response = overall_mean
        contributions = {"overall_mean": overall_mean}
        
        # Add contributions from each factor
        for effect in effects:
            factor = effect["factor"]
            if factor in factor_settings:
                level_value = factor_settings[factor]
                
                # Find level index
                factor_levels = self._experiment.factors[factor]
                try:
                    level_idx = factor_levels.index(level_value)
                    if level_idx < len(effect["level_means"]):
                        level_effect = effect["level_means"][level_idx] - overall_mean
                        predicted_response += level_effect
                        contributions[factor] = level_effect
                except ValueError:
                    contributions[factor] = f"Level '{level_value}' not found"
        
        return {
            "predicted_response": predicted_response,
            "contributions": contributions,
            "factor_settings": factor_settings.copy(),
        }

    def summary(self, higher_is_better: bool = True) -> str:
        """Return a formatted text summary of main effects and recommendations."""
        effects = self.main_effects()
        optimal = self.recommend_optimal(higher_is_better)
        completeness = self.check_completeness()

        lines = [
            "=" * 60,
            f"Taguchi Experiment Analysis: {self._metric_name}",
            "=" * 60,
            "",
            f"Data completeness: {completeness['completion_percentage']:.1f}% "
            f"({completeness['results_provided']}/{completeness['total_runs']} runs)",
        ]
        
        if not completeness['is_complete']:
            lines.extend([
                f"WARNING: Missing results for runs: {completeness['missing_results']}",
                "",
            ])
        
        lines.extend([
            "",
            "Main Effects (sorted by range, descending):",
            "-" * 40,
        ])

        for effect in sorted(effects, key=lambda e: e["range"], reverse=True):
            factor = effect["factor"]
            range_val = effect["range"]
            means_str = ", ".join(f"{m:.4f}" for m in effect["level_means"])
            lines.append(f"  {factor:20s} range={range_val:8.4f}  means=[{means_str}]")

        lines += [
            "",
            f"Recommended Optimal Settings ({'higher' if higher_is_better else 'lower'} is better):",
            "-" * 40,
        ]
        for factor, level in optimal.items():
            lines.append(f"  {factor:20s} -> {level}")

        lines += ["", "=" * 60]
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Export and import methods  
    # ------------------------------------------------------------------
    
    def export_results(self, path: str) -> None:
        """Export results to CSV file."""
        if not self._results:
            raise TaguchiError("No results to export")
        
        with open(path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['run_id', self._metric_name])
            for run_id, value in sorted(self._results.items()):
                writer.writerow([run_id, value])
    
    def import_results(self, path: str, run_id_col: str = "run_id") -> "Analyzer":
        """
        Import results from CSV file.
        
        Args:
            path: Path to CSV file
            run_id_col: Name of run ID column
            
        Returns:
            Self for chaining
        """
        with open(path, 'r') as f:
            reader = csv.DictReader(f)
            
            if run_id_col not in reader.fieldnames:
                raise ValidationError([
                    f"Run ID column '{run_id_col}' not found in CSV. "
                    f"Available columns: {list(reader.fieldnames)}"
                ], "results_import")
            
            if self._metric_name not in reader.fieldnames:
                raise ValidationError([
                    f"Metric column '{self._metric_name}' not found in CSV. "
                    f"Available columns: {list(reader.fieldnames)}"
                ], "results_import")
            
            for row in reader:
                try:
                    run_id = int(row[run_id_col])
                    value = float(row[self._metric_name])
                    self.add_result(run_id, value)
                except (ValueError, KeyError) as e:
                    raise ValidationError([
                        f"Error reading row {row}: {e}"
                    ], "results_import")
        
        return self

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
    
    # ------------------------------------------------------------------
    # String representation
    # ------------------------------------------------------------------
    
    def __str__(self) -> str:
        """Human-readable analyzer description."""
        completeness = self.check_completeness()
        
        return (
            f"Taguchi Analyzer for '{self._metric_name}'\n"
            f"Results: {completeness['results_provided']}/{completeness['total_runs']} "
            f"({completeness['completion_percentage']:.1f}%)"
        )
    
    def __repr__(self) -> str:
        """Developer-friendly representation."""
        return (
            f"Analyzer(metric='{self._metric_name}', "
            f"results={len(self._results)}, "
            f"analyzed={self._effects is not None})"
        )