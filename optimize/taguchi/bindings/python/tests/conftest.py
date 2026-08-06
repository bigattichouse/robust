"""
Shared fixtures for Python bindings tests.
"""

import os
import pytest
from pathlib import Path

# Absolute path to the built CLI binary — tests don't rely on PATH
CLI_PATH = str(Path(__file__).parent.parent.parent.parent / "build" / "taguchi")


def pytest_configure(config):
    if not os.path.exists(CLI_PATH) or not os.access(CLI_PATH, os.X_OK):
        pytest.exit(
            f"taguchi binary not found or not executable at {CLI_PATH}. "
            "Run 'make cli' first.",
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
