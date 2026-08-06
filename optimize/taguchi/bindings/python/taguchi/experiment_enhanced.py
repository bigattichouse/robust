"""
Enhanced Experiment class with validation methods and dependency injection.

Provides Phase 2 improvements while maintaining backward compatibility.
"""

import tempfile
import os
import re
from typing import Dict, List, Optional, Any, Union

from .core_enhanced import Taguchi
from .config import TaguchiConfig
from .errors import TaguchiError, ValidationError


# Characters that are illegal in factor names because they break environment
# variable assignment (cmd_run uses setenv(name, value)) or the .tgu format.
_INVALID_NAME_RE = re.compile(r'[=\s#:,]')


def _validate_factor_name(name: str) -> List[str]:
    """Validate factor name and return list of errors."""
    errors = []
    
    if not name:
        errors.append("Factor name must not be empty")
        return errors
    
    if not isinstance(name, str):
        errors.append(f"Factor name must be a string, got {type(name)}")
        return errors
    
    m = _INVALID_NAME_RE.search(name)
    if m:
        errors.append(
            f"Factor name '{name}' contains invalid character '{m.group()}'. "
            "Names must not contain '=', whitespace, '#', ':', or ','."
        )
    
    # Additional validation rules
    if len(name) > 50:
        errors.append(f"Factor name '{name}' is too long (max 50 characters)")
    
    if name.startswith('_'):
        errors.append(f"Factor name '{name}' cannot start with underscore")
    
    return errors


def _validate_levels(name: str, levels: List[str]) -> List[str]:
    """Validate factor levels and return list of errors."""
    errors = []
    
    if not levels:
        errors.append(f"Factor '{name}' must have at least one level")
        return errors
    
    if len(levels) > 10:
        errors.append(f"Factor '{name}' has too many levels ({len(levels)} > 10)")
    
    seen_levels = set()
    for i, level in enumerate(levels):
        if not isinstance(level, str):
            errors.append(
                f"Factor '{name}': level {i+1} must be a string, got {type(level)}"
            )
            continue
        
        if not level.strip():
            errors.append(f"Factor '{name}': level {i+1} cannot be empty or whitespace")
            continue
        
        if level in seen_levels:
            errors.append(f"Factor '{name}': duplicate level '{level}'")
            continue
        
        seen_levels.add(level)
        
        # Check for problematic characters in levels
        if any(char in level for char in [',', '=', '\n', '\r', '\t']):
            errors.append(
                f"Factor '{name}': level '{level}' contains invalid characters "
                "(cannot contain comma, equals, or whitespace characters)"
            )
    
    return errors


class Experiment:
    """
    Enhanced high-level interface for designing and running Taguchi experiments.
    
    Provides validation, dependency injection, and improved error handling
    while maintaining full backward compatibility.

    Use as a context manager to ensure temporary files are always cleaned up:

        with Experiment() as exp:
            exp.add_factor("temp", ["350F", "375F", "400F"])
            runs = exp.generate()
    
    Or with custom configuration:
    
        config = TaguchiConfig(cli_timeout=60, debug_mode=True)
        taguchi = Taguchi(config=config)
        with Experiment(taguchi=taguchi) as exp:
            exp.add_factor("temp", ["350F", "375F", "400F"])
            runs = exp.generate()
    """

    def __init__(
        self,
        array_type: Optional[str] = None,
        taguchi: Optional[Taguchi] = None,
        config: Optional[TaguchiConfig] = None
    ):
        """
        Initialize experiment.
        
        Args:
            array_type: Explicit array type selection (backward compatibility)
            taguchi: Injected Taguchi instance (dependency injection)
            config: Configuration (if taguchi not provided)
        """
        # Dependency injection with backward compatibility
        if taguchi is not None:
            self._taguchi = taguchi
        else:
            self._taguchi = Taguchi(config=config)
        
        self._array_type = array_type
        self._factors: Dict[str, List[str]] = {}
        self._runs: Optional[List[Dict]] = None
        self._tgu_path: Optional[str] = None
        self._validation_cache: Optional[List[str]] = None

    # ------------------------------------------------------------------
    # Validation Methods (Phase 2)
    # ------------------------------------------------------------------

    def validate(self) -> List[str]:
        """
        Return list of validation errors, empty if valid.
        
        Validates:
        - Factor names and levels
        - Array compatibility
        - Overall experiment configuration
        
        Returns:
            List of error messages. Empty list means valid.
        """
        if self._validation_cache is not None:
            return self._validation_cache
        
        errors = []
        
        # Validate factors
        if not self._factors:
            errors.append("No factors defined")
            return errors
        
        for name, levels in self._factors.items():
            errors.extend(_validate_factor_name(name))
            errors.extend(_validate_levels(name, levels))
        
        # If basic validation failed, don't continue
        if errors:
            self._validation_cache = errors
            return errors
        
        # Validate array compatibility
        try:
            num_factors = len(self._factors)
            max_levels = max(len(lvls) for lvls in self._factors.values())
            
            # Check if any array can handle this configuration
            available_arrays = self._taguchi._get_arrays_info()
            compatible_arrays = [
                a for a in available_arrays 
                if a['cols'] >= num_factors and a['levels'] >= max_levels
            ]
            
            if not compatible_arrays:
                # Find best partial matches for better error message
                by_cols = [a for a in available_arrays if a['cols'] >= num_factors]
                by_levels = [a for a in available_arrays if a['levels'] >= max_levels]
                
                if not by_cols and not by_levels:
                    errors.append(
                        f"No available array supports {num_factors} factors with "
                        f"{max_levels} levels. Maximum available: "
                        f"{max(a['cols'] for a in available_arrays)} factors, "
                        f"{max(a['levels'] for a in available_arrays)} levels."
                    )
                elif not by_cols:
                    max_cols = max(a['cols'] for a in available_arrays)
                    errors.append(
                        f"No available array supports {num_factors} factors "
                        f"(maximum available: {max_cols})"
                    )
                elif not by_levels:
                    max_level_support = max(a['levels'] for a in available_arrays)
                    errors.append(
                        f"No available array supports {max_levels} levels "
                        f"(maximum available: {max_level_support})"
                    )
        
        except Exception as e:
            errors.append(f"Error validating array compatibility: {e}")
        
        # Validate explicit array type if specified
        if self._array_type:
            try:
                array_info = self._taguchi.get_array_info(self._array_type)
                num_factors = len(self._factors)
                max_levels = max(len(lvls) for lvls in self._factors.values())
                
                if array_info['cols'] < num_factors:
                    errors.append(
                        f"Array {self._array_type} has only {array_info['cols']} "
                        f"columns but {num_factors} factors were defined"
                    )
                
                if array_info['levels'] < max_levels:
                    errors.append(
                        f"Array {self._array_type} supports only {array_info['levels']} "
                        f"levels but {max_levels} levels were used"
                    )
            
            except TaguchiError:
                errors.append(f"Specified array '{self._array_type}' not found")
        
        self._validation_cache = errors
        return errors
    
    def is_valid(self) -> bool:
        """Quick check if experiment is valid."""
        return len(self.validate()) == 0
    
    def validate_and_raise(self) -> None:
        """Validate experiment and raise ValidationError if invalid."""
        errors = self.validate()
        if errors:
            raise ValidationError(errors, "experiment_validation")

    # ------------------------------------------------------------------
    # Public mutation API (enhanced)
    # ------------------------------------------------------------------

    def add_factor(self, name: str, levels: List[str]) -> "Experiment":
        """Add a factor with its levels. Returns self for chaining."""
        if self._runs is not None:
            raise TaguchiError("Cannot add factors after runs are generated")
        
        # Immediate validation for better error reporting
        name_errors = _validate_factor_name(name)
        if name_errors:
            raise ValidationError(name_errors, "factor_addition")
        
        level_errors = _validate_levels(name, list(levels))
        if level_errors:
            raise ValidationError(level_errors, "factor_addition")
        
        self._factors[name] = list(levels)
        
        # Invalidate caches
        self._tgu_path = None
        self._validation_cache = None
        
        return self
    
    def remove_factor(self, name: str) -> "Experiment":
        """Remove a factor. Returns self for chaining."""
        if self._runs is not None:
            raise TaguchiError("Cannot remove factors after runs are generated")
        
        if name not in self._factors:
            raise TaguchiError(f"Factor '{name}' not found")
        
        del self._factors[name]
        
        # Invalidate caches
        self._tgu_path = None
        self._validation_cache = None
        
        return self
    
    def clear_factors(self) -> "Experiment":
        """Remove all factors. Returns self for chaining."""
        if self._runs is not None:
            raise TaguchiError("Cannot clear factors after runs are generated")
        
        self._factors.clear()
        
        # Invalidate caches
        self._tgu_path = None
        self._validation_cache = None
        
        return self

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _generate_tgu(self) -> str:
        """Render the current factor definition as .tgu content."""
        if not self._factors:
            raise TaguchiError("No factors defined")
        lines = ["factors:"]
        for name, levels in self._factors.items():
            lines.append(f"  {name}: {', '.join(levels)}")
        if self._array_type:
            lines.append(f"array: {self._array_type}")
        return "\n".join(lines)

    def _initialize(self) -> None:
        """Resolve array type if not explicitly set."""
        if not self._factors:
            raise TaguchiError("No factors defined")
        
        # Validate before initializing
        self.validate_and_raise()
        
        if self._array_type is None:
            num_factors = len(self._factors)
            max_levels = max(len(lvls) for lvls in self._factors.values())
            self._array_type = self._taguchi.suggest_array(num_factors, max_levels)

    def get_tgu_path(self) -> str:
        """
        Return the path to a .tgu file for this experiment, creating a
        temporary one if necessary. The file persists until cleanup().
        """
        if self._tgu_path is None:
            self._initialize()
            fd, path = tempfile.mkstemp(suffix='.tgu')
            with os.fdopen(fd, 'w') as f:
                f.write(self._generate_tgu())
            self._tgu_path = path
        return self._tgu_path

    def cleanup(self) -> None:
        """Delete any temporary .tgu file created by this experiment."""
        if self._tgu_path and os.path.exists(self._tgu_path):
            try:
                os.unlink(self._tgu_path)
            except OSError:
                pass
        self._tgu_path = None

    # ------------------------------------------------------------------
    # Public generation API
    # ------------------------------------------------------------------

    def generate(self) -> List[Dict[str, Any]]:
        """Generate and cache experiment runs."""
        if self._runs is None:
            tgu_path = self.get_tgu_path()
            self._runs = self._taguchi.generate_runs(tgu_path)
        return self._runs

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def array_type(self) -> Optional[str]:
        """The selected orthogonal array (auto-selected if not set explicitly)."""
        if self._array_type is None:
            self._initialize()
        return self._array_type

    @property
    def num_runs(self) -> int:
        """Number of experimental runs (triggers generation if needed)."""
        return len(self.generate())

    @property
    def factors(self) -> Dict[str, List[str]]:
        """A copy of the defined factors and their levels."""
        return {name: list(levels) for name, levels in self._factors.items()}
    
    @property
    def factor_count(self) -> int:
        """Number of factors defined."""
        return len(self._factors)
    
    @property
    def max_levels(self) -> int:
        """Maximum number of levels across all factors."""
        if not self._factors:
            return 0
        return max(len(levels) for levels in self._factors.values())

    # ------------------------------------------------------------------
    # Enhanced analysis methods
    # ------------------------------------------------------------------
    
    def estimate_runtime(self, seconds_per_run: float = 1.0) -> Dict[str, float]:
        """
        Estimate total experiment runtime.
        
        Args:
            seconds_per_run: Expected time per experimental run
            
        Returns:
            Dictionary with runtime estimates in different units
        """
        num_runs = len(self.generate())
        total_seconds = num_runs * seconds_per_run
        
        return {
            "total_runs": num_runs,
            "seconds_per_run": seconds_per_run,
            "total_seconds": total_seconds,
            "total_minutes": total_seconds / 60,
            "total_hours": total_seconds / 3600,
        }
    
    def compare_with_full_factorial(self) -> Dict[str, Any]:
        """
        Compare this experiment with a full factorial design.
        
        Returns:
            Comparison metrics including efficiency gains
        """
        if not self._factors:
            raise TaguchiError("No factors defined")
        
        # Calculate full factorial runs
        full_factorial_runs = 1
        for levels in self._factors.values():
            full_factorial_runs *= len(levels)
        
        taguchi_runs = len(self.generate())
        
        return {
            "taguchi_runs": taguchi_runs,
            "full_factorial_runs": full_factorial_runs,
            "efficiency_ratio": taguchi_runs / full_factorial_runs,
            "runs_saved": full_factorial_runs - taguchi_runs,
            "percentage_reduction": (1 - taguchi_runs / full_factorial_runs) * 100,
        }

    # ------------------------------------------------------------------
    # Serialization
    # ------------------------------------------------------------------

    def to_tgu(self) -> str:
        """Export experiment definition as a .tgu format string."""
        self._initialize()
        return self._generate_tgu()

    def save(self, path: str) -> None:
        """Save experiment definition to a .tgu file."""
        self._initialize()
        with open(path, 'w') as f:
            f.write(self._generate_tgu())

    @classmethod
    def from_tgu(
        cls, 
        path: str, 
        taguchi: Optional[Taguchi] = None,
        config: Optional[TaguchiConfig] = None
    ) -> "Experiment":
        """
        Create an Experiment from an existing .tgu file.
        
        Args:
            path: Path to .tgu file
            taguchi: Optional Taguchi instance for dependency injection
            config: Optional configuration

        Handles:
        - Inline and full-line comments (# ...)
        - Blank lines
        - 'array:' key for explicit array override
        """
        with open(path, 'r') as f:
            content = f.read()

        exp = cls(taguchi=taguchi, config=config)
        # Point directly at the source file — no temp file needed
        exp._tgu_path = path

        factors: Dict[str, List[str]] = {}
        in_factors = False

        for raw_line in content.split('\n'):
            # Strip inline comments and surrounding whitespace
            line = raw_line.split('#')[0].strip()
            if not line:
                continue

            if line.startswith('factors:'):
                in_factors = True
                continue

            if line.startswith('array:'):
                exp._array_type = line[len('array:'):].strip()
                in_factors = False
                continue

            # Any non-indented, non-blank line outside factors: ends the block
            if in_factors and not raw_line.startswith(' ') and not raw_line.startswith('\t'):
                in_factors = False

            if in_factors and ':' in line:
                name, _, levels_str = line.partition(':')
                name = name.strip()
                levels = [lv.strip() for lv in levels_str.split(',') if lv.strip()]
                if name and levels:
                    factors[name] = levels

        if not factors:
            raise TaguchiError(f"No factors found in '{path}'")

        # Validate loaded factors
        for name, levels in factors.items():
            name_errors = _validate_factor_name(name)
            if name_errors:
                raise ValidationError(name_errors, "tgu_file_loading")
            
            level_errors = _validate_levels(name, levels)
            if level_errors:
                raise ValidationError(level_errors, "tgu_file_loading")

        exp._factors = factors
        return exp

    def get_array_info(self) -> Optional[Dict]:
        """Info dict for the selected array ({rows, cols, levels}), or None."""
        if self.array_type:
            return self._taguchi.get_array_info(self.array_type)
        return None

    # ------------------------------------------------------------------
    # Context manager and finalizer
    # ------------------------------------------------------------------

    def __enter__(self) -> "Experiment":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.cleanup()

    def __del__(self) -> None:
        # Guard against partially-initialised objects and interpreter shutdown
        try:
            path = self.__dict__.get('_tgu_path')
            if path and os.path.exists(path):
                os.unlink(path)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # String representation
    # ------------------------------------------------------------------
    
    def __str__(self) -> str:
        """Human-readable experiment description."""
        if not self._factors:
            return "Empty Experiment"
        
        lines = [
            f"Taguchi Experiment ({len(self._factors)} factors)",
            f"Array: {self.array_type or 'auto-select'}",
            f"Estimated runs: {len(self.generate())}",
            "",
            "Factors:",
        ]
        
        for name, levels in self._factors.items():
            lines.append(f"  {name}: {', '.join(levels)}")
        
        return "\n".join(lines)
    
    def __repr__(self) -> str:
        """Developer-friendly representation."""
        return (
            f"Experiment(factors={len(self._factors)}, "
            f"array_type={self._array_type!r}, "
            f"runs={len(self.generate()) if self._factors else 0})"
        )