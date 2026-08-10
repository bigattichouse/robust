"""
Reading and owning `.tgu` files.

`parse_tgu` turns `.tgu` text into (factors, array_type). `TguFileMixin` tracks
whether the Experiment CREATED the file it points at: `from_tgu()` adopts the
caller's file and must never delete it, while a generated temp file is ours to
clean up. Everything here checks `_owns_tgu` before unlinking.
"""

import os
import tempfile
from typing import Dict, List, Optional, Tuple


def parse_tgu(content: str) -> Tuple[Dict[str, List[str]], Optional[str]]:
    """Parse `.tgu` text into (factors, array_type).

    Handles inline and full-line `#` comments, blank lines, and an `array:`
    key. Returns the factors in declaration order -- level ORDER is meaningful,
    since `taguchi_recommend_optimal` reports a level by index.

    Raises nothing: an empty result is returned as an empty dict, and the
    callers decide what to say about it, because their exception types differ.
    """
    factors: Dict[str, List[str]] = {}
    array_type: Optional[str] = None
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
            array_type = line[len('array:'):].strip()
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

    return factors, array_type


class TguFileMixin:
    """Owns (or does not own) the `.tgu` file an Experiment works through.

    Two attributes, and the second is the one that matters:

        _tgu_path   where the file is
        _owns_tgu   whether THIS object created it and may delete it

    Everything here checks `_owns_tgu` before unlinking. Deleting on the
    strength of `_tgu_path` alone is what destroyed callers' files.
    """

    _tgu_path: Optional[str]
    _owns_tgu: bool

    def _init_tgu_state(self) -> None:
        self._tgu_path = None
        self._owns_tgu = False

    def _adopt_tgu(self, path: str) -> None:
        """Use an existing file that someone else owns. We must not delete it."""
        self._tgu_path = path
        self._owns_tgu = False

    def _write_tgu(self, content: str) -> str:
        """Write a temp `.tgu` we own, and return its path."""
        fd, path = tempfile.mkstemp(suffix='.tgu')
        with os.fdopen(fd, 'w') as f:
            f.write(content)
        self._tgu_path = path
        self._owns_tgu = True
        return path

    def cleanup(self) -> None:
        """Delete the temporary `.tgu` file this experiment created.

        Only the one it created. `from_tgu()` adopts the caller's file, and
        this used to unlink whatever `_tgu_path` named.
        """
        if self._owns_tgu and self._tgu_path and os.path.exists(self._tgu_path):
            try:
                os.unlink(self._tgu_path)
            except OSError:
                pass
        self._tgu_path = None
        self._owns_tgu = False

    def __del__(self) -> None:
        # Guard against partially-initialised objects and interpreter shutdown,
        # and check ownership: __del__ had its own unlink that did not, so a
        # from_tgu() which RAISED left an object whose destructor removed the
        # file the user was asking about. Read through __dict__ because this
        # can run on an object whose __init__ never finished.
        try:
            if not self.__dict__.get('_owns_tgu'):
                return
            path = self.__dict__.get('_tgu_path')
            if path and os.path.exists(path):
                os.unlink(path)
        except Exception:
            pass
