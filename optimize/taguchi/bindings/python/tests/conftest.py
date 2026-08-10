"""
Shared fixtures for Python bindings tests.
"""

import os
import pytest
from pathlib import Path

# Absolute path to the built CLI binary — tests don't rely on PATH.
#
# The umbrella Makefile builds every tool into <repo>/build/bin/; the path
# below it (optimize/taguchi/build/taguchi) is where taguchi put its own binary
# back when it built itself with a sub-make. That stale path is still on disk
# in older trees, so this suite was running against a binary FOUR DAYS OLD
# while reporting passes -- the same silent-wrong-artifact failure as a `make`
# that builds nothing. Prefer the umbrella output, and let TAGUCHI_CLI override
# so CI can pin exactly what it just built.
def _find_cli() -> str:
    env = os.environ.get("TAGUCHI_CLI")
    if env:
        return env
    here = Path(__file__).resolve().parent
    # tests -> python -> bindings -> taguchi -> optimize -> <repo>
    repo = here.parents[4]
    for cand in (repo / "build" / "bin" / "taguchi",  # umbrella build (current)
                 here.parent.parent.parent / "build" / "taguchi"):  # legacy
        if cand.exists() and os.access(cand, os.X_OK):
            return str(cand)
    return str(repo / "build" / "bin" / "taguchi")   # report the one we want


CLI_PATH = _find_cli()


def pytest_configure(config):
    if not os.path.exists(CLI_PATH) or not os.access(CLI_PATH, os.X_OK):
        pytest.exit(
            f"taguchi binary not found or not executable at {CLI_PATH}. "
            "Run 'make all' from the repo root first, or set TAGUCHI_CLI.",
            returncode=1,
        )


@pytest.fixture(scope="session")
def cli_path():
    return CLI_PATH


@pytest.fixture
def simple_tgu(tmp_path):
    """A minimal 2-factor, 3-level .tgu file."""
    p = tmp_path / "simple.tgu"
    p.write_text("factors:\n  depth: 4, 6, 8\n  lr: 0.02, 0.04, 0.08\n")
    return str(p)


@pytest.fixture
def three_factor_tgu(tmp_path):
    """A 3-factor, 3-level .tgu file (uses L9)."""
    p = tmp_path / "three.tgu"
    p.write_text(
        "factors:\n"
        "  learning_rate: 0.001, 0.01, 0.1\n"
        "  batch_size: 32, 64, 128\n"
        "  weight_decay: 0.0, 0.1, 0.2\n"
    )
    return str(p)


@pytest.fixture
def simple_results_csv(tmp_path):
    """CSV results for a 9-run L9 experiment."""
    p = tmp_path / "results.csv"
    p.write_text(
        "run_id,response\n"
        "1,1.05\n2,1.02\n3,1.08\n"
        "4,1.03\n5,0.998\n6,1.045\n"
        "7,1.04\n8,1.015\n9,1.055\n"
    )
    return str(p)
