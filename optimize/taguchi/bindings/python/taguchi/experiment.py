"""
High-level Experiment class for Taguchi orthogonal arrays.
"""

import tempfile
import os
import re
from typing import Dict, List, Optional, Any
from ._tgu import TguFileMixin, parse_tgu
from .core import Taguchi, TaguchiError

# Characters that are illegal in factor names because they break environment
# variable assignment (cmd_run uses setenv(name, value)) or the .tgu format.
_INVALID_NAME_RE = re.compile(r'[=\s#:,]')


def _validate_factor_name(name: str) -> None:
    if not name:
        raise TaguchiError("Factor name must not be empty")
    m = _INVALID_NAME_RE.search(name)
    if m:
        raise TaguchiError(
            f"Factor name '{name}' contains invalid character '{m.group()}'. "
            "Names must not contain '=', whitespace, '#', ':', or ','."
        )


def _validate_levels(name: str, levels: List[str]) -> None:
    if not levels:
        raise TaguchiError(f"Factor '{name}' must have at least one level")
    for level in levels:
        if not isinstance(level, str):
            raise TaguchiError(
                f"Factor '{name}': all levels must be strings, got {type(level)}"
            )


class Experiment(TguFileMixin):
    """
    High-level interface for designing and running Taguchi experiments.

    Use as a context manager to ensure temporary files are always cleaned up:

        with Experiment() as exp:
            exp.add_factor("temp", ["350F", "375F", "400F"])
            runs = exp.generate()
    """

    def __init__(self, array_type: Optional[str] = None):
        self._taguchi = Taguchi()
        self._array_type = array_type
        self._factors: Dict[str, List[str]] = {}
        self._runs: Optional[List[Dict]] = None
        self._tgu_path: Optional[str] = None
        # Whether THIS experiment created the .tgu and may therefore delete it.
        # from_tgu() points _tgu_path at a file the CALLER owns, and cleanup()
        # used to unlink whatever _tgu_path named -- so
        #     with Experiment.from_tgu("my_experiment.tgu"): ...
        # destroyed the user's input file on the way out. Ownership has to be
        # tracked, not assumed from "the path is set".
        self._owns_tgu: bool = False

    # ------------------------------------------------------------------
    # Public mutation API
    # ------------------------------------------------------------------

    def add_factor(self, name: str, levels: List[str]) -> "Experiment":
        """Add a factor with its levels. Returns self for chaining."""
        if self._runs is not None:
            raise TaguchiError("Cannot add factors after runs are generated")
        _validate_factor_name(name)
        _validate_levels(name, list(levels))
        self._factors[name] = list(levels)
        # Invalidate cached tgu file and array selection
        self._tgu_path = None
        self._array_type = self._array_type  # preserve explicit override
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
            self._write_tgu(self._generate_tgu())
        return self._tgu_path

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
    def from_tgu(cls, path: str) -> "Experiment":
        """
        Create an Experiment from an existing .tgu file.

        Handles:
        - Inline and full-line comments (# ...)
        - Blank lines
        - 'array:' key for explicit array override
        """
        with open(path, 'r') as f:
            content = f.read()

        exp = cls()
        # The CALLER owns this file; _adopt_tgu records that so nothing
        # deletes it. See _tgu.TguFileMixin.
        exp._adopt_tgu(path)

        # One parser, in _tgu. This block existed identically in both
        # Experiment classes.
        factors, array_type = parse_tgu(content)
        if array_type:
            exp._array_type = array_type

        if not factors:
            raise TaguchiError(f"No factors found in '{path}'")

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

