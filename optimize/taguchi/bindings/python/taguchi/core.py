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

# Subprocess timeout in seconds — prevents hangs if the binary stalls.
_CLI_TIMEOUT = 30


class TaguchiError(Exception):
    """Exception raised for Taguchi library errors."""
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

        # Search relative to this file (package root → ../../.. → build/taguchi)
        current_dir = Path(__file__).parent
        possible_paths.extend([
            current_dir.parent.parent.parent / "build" / "taguchi",
            current_dir.parent.parent / "build" / "taguchi",
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
            output = self._run_command(["generate", tgu_path])
        else:
            # Treat the argument as raw .tgu content
            with tempfile.NamedTemporaryFile(
                mode='w', suffix='.tgu', delete=False
            ) as f:
                f.write(tgu_path)
                temp_path = f.name
            try:
                output = self._run_command(["generate", temp_path])
            finally:
                os.unlink(temp_path)

        runs = []
        for line in output.strip().split("\n"):
            if not line.startswith("Run "):
                continue
            parts = line.split(": ", 1)
            if len(parts) < 2:
                continue
            try:
                run_id = int(parts[0][4:].strip())
            except ValueError:
                continue
            factors: Dict[str, str] = {}
            for pair in parts[1].split(", "):
                if "=" in pair:
                    key, _, value = pair.partition("=")
                    factors[key.strip()] = value.strip()
            runs.append({"run_id": run_id, "factors": factors})

        return runs

    def analyze(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Run full analysis with main effects and optimal recommendations."""
        return self._run_command(
            ["analyze", tgu_path, results_csv, "--metric", metric]
        )

    def effects(self, tgu_path: str, results_csv: str, metric: str = "response") -> str:
        """Calculate and return the main effects table."""
        return self._run_command(
            ["effects", tgu_path, results_csv, "--metric", metric]
        )
