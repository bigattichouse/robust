"""
Where the taguchi CLI binary lives — one search order for the package.

Order: an explicit `cli_path` argument, then $TAGUCHI_CLI_PATH / $TAGUCHI_CLI,
then the umbrella build at <repo>/build/bin/taguchi, then legacy and system
locations, then PATH. An argument is a decision; an environment variable is a
default.

`find_cli` returns None rather than raising, so callers can report a missing
binary in whatever terms suit them.
"""

import os
import shutil
from pathlib import Path
from typing import List, Optional

# Read in order. An explicit argument is a decision, so it wins; an environment
# variable is a default; the tree layout is a fallback.
ENV_VARS = ("TAGUCHI_CLI_PATH", "TAGUCHI_CLI")

SYSTEM_PATHS = (
    "/usr/local/bin/taguchi",
    "/usr/bin/taguchi",
    "/opt/taguchi/bin/taguchi",
)


def candidate_paths(cli_path: Optional[str] = None) -> List[str]:
    """Every location that will be tried, in order. Exposed for diagnostics."""
    out: List[str] = []

    if cli_path:
        out.append(str(cli_path))

    for var in ENV_VARS:
        value = os.getenv(var)
        if value:
            out.append(value)

    # taguchi/ -> python/ -> bindings/ -> optimize/taguchi/ -> optimize/ -> repo
    pkg_dir = Path(__file__).resolve().parent
    out.extend([
        # The umbrella Makefile's output. FIRST, deliberately: see the note at
        # the top of this file.
        str(pkg_dir.parents[4] / "build" / "bin" / "taguchi"),
        str(pkg_dir.parent / "taguchi_cli"),          # autoresearch integration
        str(pkg_dir.parents[2] / "build" / "taguchi"),  # legacy sub-make output
        str(pkg_dir.parents[1] / "build" / "taguchi"),
    ])

    out.extend(SYSTEM_PATHS)
    return out


def find_cli(cli_path: Optional[str] = None) -> Optional[str]:
    """Return an absolute path to an executable taguchi, or None.

    None rather than an exception on purpose: the two layers report a missing
    binary differently (a plain TaguchiError versus a BinaryDiscoveryError
    carrying every path it tried), and that difference is part of their
    respective interfaces.
    """
    for path_str in candidate_paths(cli_path):
        p = Path(path_str)
        if p.exists() and os.access(str(p), os.X_OK):
            return str(p.absolute())

    return shutil.which("taguchi")
