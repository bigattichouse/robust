"""
Readers for the CLI's `--json` output.

WHY THIS MODULE EXISTS
----------------------
The binding used to scrape the CLI's human tables, in FOUR places: a design
parser in `core.py` and again in `core.py`, an effects parser in
`analyzer.py` and again in `analyzer.py`. Each split on punctuation
and `continue`d past anything that did not match, so upstream formatting
changes removed data silently instead of raising.

That was not theoretical. `\\w+` in the effects regex does not match a factor
named `kv-type`, so that factor was dropped from the analysis with no error and
no gap in the returned list — two of three factors parsed, and the one lost was
second by effect size.

The CLI now emits `--json` on generate, analyze and effects, with a `schema`
key. These readers are the single place that understands it, so the next change
to that contract has one site to update rather than four to find.

They are deliberately strict. Where the old parsers skipped what they could not
read, these raise: a partial design or a missing factor is not a smaller
answer, it is a wrong one, and the whole point of the exercise is that it stops
being silent.
"""

import json
from typing import Any, Dict, List

from .errors import TaguchiError


def _error(message):
    return TaguchiError(message)


# The schema this reader understands. The CLI bumps its `schema` only when a
# key is renamed or removed, never for an addition, so a HIGHER number means
# the document may have lost something we depend on.
SUPPORTED_SCHEMA = 1


def _load(stdout: str, command: str) -> Dict[str, Any]:
    """Parse CLI stdout as JSON, or explain what actually arrived.

    The common cause of failure here is a taguchi binary predating `--json`:
    older builds IGNORED unknown options and printed the human table at exit 0,
    so the caller gets prose where it asked for a document. Say that, rather
    than let a bare JSONDecodeError travel up.
    """
    text = (stdout or "").strip()
    if not text:
        raise _error(
            "taguchi %s --json produced no output. "
            "Check that the binary exists and is executable." % command
        )
    try:
        doc = json.loads(text)
    except ValueError as exc:
        head = text.splitlines()[0][:80] if text.splitlines() else ""
        raise _error(
            "taguchi %s --json did not return JSON (%s). First line: %r. "
            "A binary older than --json ignores the flag and prints the human "
            "table instead; rebuild with 'make all' from the repo root."
            % (command, exc, head)
        )
    if not isinstance(doc, dict):
        raise _error(
            "taguchi %s --json returned %s, expected an object"
            % (command, type(doc).__name__)
        )

    schema = doc.get("schema")
    if schema is not None and schema > SUPPORTED_SCHEMA:
        raise _error(
            "taguchi %s --json reports schema %s; this binding understands %d. "
            "A higher schema means a key was renamed or removed, so refusing "
            "rather than reading it wrongly." % (command, schema, SUPPORTED_SCHEMA)
        )
    return doc


def runs_from_json(stdout: str) -> List[Dict[str, Any]]:
    """`taguchi generate --json` -> [{'run_id': int, 'factors': {name: value}}].

    The shape is unchanged from the scraping implementation, so callers and
    their tests do not have to change with it.
    """
    doc = _load(stdout, "generate")
    runs = doc.get("runs")
    if not isinstance(runs, list):
        raise _error("taguchi generate --json has no 'runs' array")

    out: List[Dict[str, Any]] = []
    for r in runs:
        settings = r.get("settings")
        if not isinstance(settings, dict):
            raise _error(
                "run %r has no 'settings' object" % (r.get("run_id"),))
        out.append({"run_id": int(r["run_id"]), "factors": dict(settings)})

    # A design is only a design if it is complete. The old parser could return
    # a short list and no one would know; this is where that stops.
    declared = doc.get("run_count")
    if declared is not None and declared != len(out):
        raise _error(
            "taguchi generate --json declared %s runs but listed %d"
            % (declared, len(out)))
    return out


def effects_from_json(stdout: str) -> List[Dict[str, Any]]:
    """`taguchi effects --json` -> [{'factor', 'range', 'level_means', ...}].

    Carries the three keys the scraping parser produced, plus `level_values`,
    which the table never printed at all — it showed `L1=1.05` and left the
    reader to work out what L1 was from the .tgu.
    """
    doc = _load(stdout, "effects")
    effects = doc.get("effects")
    if not isinstance(effects, list):
        raise _error("taguchi effects --json has no 'effects' array")

    out: List[Dict[str, Any]] = []
    for e in effects:
        levels = e.get("levels") or []
        out.append({
            "factor": e["factor"],
            "range": float(e["range"]),
            "level_means": [float(lv["mean"]) for lv in levels],
            "level_values": [lv.get("value") for lv in levels],
        })

    declared = doc.get("factor_count")
    if declared is not None and declared != len(out):
        raise _error(
            "taguchi effects --json declared %s factors but listed %d"
            % (declared, len(out)))
    return out


def arrays_from_json(stdout: str) -> List[Dict[str, Any]]:
    """`taguchi list-arrays --json` -> [{'name','rows','cols','levels',...}].

    The scraping version matched

        \\s+(L\\d+)\\s+\\(\\s*(\\d+)\\s+runs,\\s*(\\d+)\\s+cols,\\s*(\\d+)\\s+levels\\)

    which requires a NUMBER before "levels". A mixed-level array prints
    "mixed" there, so L18 never matched and the binding reported 19 of the 20
    arrays the tool has -- and `get_array_info("L18")` raised "not found". A
    user with mixed-level factors could not reach the one array designed for
    them. Nothing errored; the list was simply shorter.

    `levels` is None for a mixed-level array, and `mixed_levels` says so, so
    the two cases stay distinguishable instead of collapsing onto 0.
    """
    doc = _load(stdout, "list-arrays")
    arrays = doc.get("arrays")
    if not isinstance(arrays, list):
        raise _error("taguchi list-arrays --json has no 'arrays' array")

    out: List[Dict[str, Any]] = []
    for a in arrays:
        out.append({
            "name": a["name"],
            "rows": a.get("runs"),
            "cols": a.get("columns"),
            "levels": a.get("levels"),
            "mixed_levels": bool(a.get("mixed_levels")),
        })
    if not out:
        raise _error("taguchi list-arrays --json listed no arrays")
    return out
