"""
Core Taguchi library interface using shell commands.
"""

import shutil
import subprocess
import tempfile
import os
import re
from pathlib import Path
from typing import List, Optional, Dict, Any

from ._base_error import TaguchiErrorBase
from ._cli_json import effects_from_json, runs_from_json

# Subprocess timeout in seconds — prevents hangs if the binary stalls.
_CLI_TIMEOUT = 30


class TaguchiError(TaguchiErrorBase):
    """Exception raised for Taguchi library errors.

    Derives from the shared base so `from taguchi import TaguchiError` -- which
    is that base -- catches it. It did not, before: the exported name was a
    subclass of this one, and a subclass catches nothing.
    """
    pass


class Taguchi:
    """
    Python interface to the Taguchi orthogonal array CLI tool.
    Uses shell commands which is more robust than ctypes for complex structures.
    """

    def __init__(self, cli_path: Optional[str] = None):
        self._cli_path = self._find_cli(cli_path)
        self._array_cache: Optional[List[Dict]] = None

    def _find_cli(self, cli_path: Optional[str]) -> str:
        """Find the taguchi CLI binary."""
        possible_paths: List[Path] = []

        if cli_path:
            possible_paths.append(Path(cli_path))

        # $TAGUCHI_CLI wins after an explicit argument, so a test run or a CI
        # job can pin exactly the binary it just built.
        env_path = os.environ.get("TAGUCHI_CLI")
        if env_path:
            possible_paths.append(Path(env_path))

        # Search relative to this file.
        #
        # <repo>/build/bin/taguchi FIRST: that is where the umbrella Makefile
        # puts every tool. The optimize/taguchi/build/taguchi path below it is
        # where taguchi put its own binary back when it built itself with a
        # sub-make, and that file survives in older trees -- so this list used
        # to find a stale binary in preference to the one just built. Measured
        # on 2026-08-10: the binding's test suite was running against a binary
        # FOUR DAYS OLD and reporting passes. Keep the umbrella path first.
        pkg_dir = Path(__file__).resolve().parent          # .../python/taguchi
        repo = pkg_dir.parents[4]                          # <repo>/
        possible_paths.extend([
            repo / "build" / "bin" / "taguchi",            # umbrella build
            pkg_dir.parents[2] / "build" / "taguchi",      # legacy sub-make
            pkg_dir.parents[1] / "build" / "taguchi",
        ])

        # Common system install locations
        possible_paths.extend([
            Path("/usr/local/bin/taguchi"),
            Path("/usr/bin/taguchi"),
        ])

        for path in possible_paths:
            if path.exists() and os.access(path, os.X_OK):
                return str(path.absolute())

        # Fall back to PATH lookup — shutil.which is cross-platform
        found = shutil.which("taguchi")
        if found:
            return found

        raise TaguchiError("Could not find taguchi CLI. Build with 'make' first.")

    def _run_command(self, args: List[str]) -> str:
        """Run a taguchi command and return stdout."""
        cmd = [self._cli_path] + args
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=_CLI_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            raise TaguchiError(
                f"Taguchi command timed out after {_CLI_TIMEOUT}s: {' '.join(args)}"
            )
        if result.returncode != 0:
            error_msg = result.stderr.strip() or result.stdout.strip() or "Unknown error"
            raise TaguchiError(f"Taguchi command failed: {error_msg}")
        return result.stdout

    def _get_arrays_info(self) -> List[Dict]:
        """Return cached array metadata."""
        if self._array_cache is not None:
            return self._array_cache

        output = self._run_command(["list-arrays"])
        arrays = []

        for line in output.strip().split('\n'):
            match = re.match(
                r'\s+(L\d+)\s+\(\s*(\d+)\s+runs,\s*(\d+)\s+cols,\s*(\d+)\s+levels\)',
                line,
            )
            if match:
                arrays.append({
                    'name': match.group(1),
                    'rows': int(match.group(2)),
                    'cols': int(match.group(3)),
                    'levels': int(match.group(4)),
                })

        if not arrays:
            raise TaguchiError(
                "list-arrays returned no arrays — CLI output may have changed format"
            )

        self._array_cache = arrays
        return arrays

    def list_arrays(self) -> List[str]:
        """List all available orthogonal array names."""
        return [a['name'] for a in self._get_arrays_info()]

    def get_array_info(self, name: str) -> dict:
        """Get run/column/level counts for a named array."""
        for array in self._get_arrays_info():
            if array['name'] == name:
                return {
                    'rows': array['rows'],
                    'cols': array['cols'],
                    'levels': array['levels'],
                }
        raise TaguchiError(f"Array '{name}' not found")

    def suggest_array(self, num_factors: int, max_levels: int) -> str:
        """Suggest the smallest orthogonal array that fits the experiment."""
        if num_factors < 1:
            raise TaguchiError("num_factors must be at least 1")
        if max_levels < 2:
            raise TaguchiError("max_levels must be at least 2")

        arrays = self._get_arrays_info()

        # Prefer arrays whose native level count matches; fall back to any
        candidates = [a for a in arrays if a['levels'] >= max_levels] or arrays

        # Among candidates, keep those with enough columns
        sufficient = [a for a in candidates if a['cols'] >= num_factors]
        if not sufficient:
            # No perfect fit — return the largest available as best effort
            return max(candidates, key=lambda a: a['cols'])['name']

        # Return the smallest sufficient array (fewest runs)
        return min(sufficient, key=lambda a: a['rows'])['name']

    def generate_runs(self, tgu_path: str) -> List[Dict[str, Any]]:
        """
        Generate experiment runs from a .tgu file path or raw .tgu content string.

        Returns a list of dicts: [{'run_id': int, 'factors': {name: value}}, ...]
        """
        if os.path.exists(tgu_path):
            output = self._run_command(["generate", tgu_path, "--json"])
        else:
            # Treat the argument as raw .tgu content
            with tempfile.NamedTemporaryFile(
                mode='w', suffix='.tgu', delete=False
            ) as f:
                f.write(tgu_path)
                temp_path = f.name
            try:
                output = self._run_command(["generate", temp_path, "--json"])
            finally:
                os.unlink(temp_path)

        return runs_from_json(output)

    def analyze(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Run full analysis with main effects and optimal recommendations."""
        return self._run_command(
            ["analyze", tgu_path, results_csv, "--metric", metric]
        )

    def effects_json(self, tgu_path: str, results_csv: str,
                     metric: str = "response") -> List[Dict[str, Any]]:
        """Main effects as data: [{'factor', 'range', 'level_means',
        'level_values'}, ...].

        This is what `Analyzer.main_effects()` reads. `effects()` below still
        returns the human table, because that is what a caller printing a
        report wants -- but nothing should PARSE it.
        """
        return effects_from_json(self._run_command(
            ["effects", tgu_path, results_csv, "--metric", metric, "--json"]))

    def effects(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Calculate and return the main effects table."""
        return self._run_command(
            ["effects", tgu_path, results_csv, "--metric", metric]
        )
