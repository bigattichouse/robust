"""
The contract between the Python binding and the taguchi CLI.

WHY THIS FILE EXISTS
--------------------
The binding drives the CLI as a subprocess and parses its HUMAN output:

    core.py:168      `Run 1: a=1, b=2` split on ", " and then on "="
    analyzer.py:86   the effects table matched with
                     \\s*(\\w+)\\s+([\\d.]+)\\s+(.+)

Both `continue` past anything that does not match, so a formatting change
upstream removes data silently instead of raising. That is not hypothetical:
`\\w+` does not match a factor named `kv-type`, so that factor is dropped from
the analysis today -- no error, no warning, no gap in the returned list.

The CLI now has `--json` on generate, analyze and effects, and the binding
should move onto it. These tests exist to make that switch SAFE: they are
written against the binding's PUBLIC API, never against its parsing internals,
so every one of them stays meaningful before and after the change. If they pass
before the switch and pass after it, the switch preserved behaviour.

Cases that are broken TODAY are marked `xfail(strict=True)` -- so they fail the
build if they ever start passing, which forces them to be flipped to plain
tests as part of the switch rather than quietly left behind.

Hermetic: no system install, no network, no fixture outside tmp_path. The rest
of this directory assumes things like /usr/bin/taguchi existing, which is why
it is not the suite wired into CI.
"""

import json
import subprocess
from collections import Counter

import pytest

from taguchi.core import Taguchi
from taguchi.experiment import Experiment
from taguchi.analyzer import Analyzer


# --------------------------------------------------------------------------
# fixtures
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def cli():
    return Taguchi()


def write_tgu(tmp_path, body, name="e.tgu"):
    p = tmp_path / name
    p.write_text(body)
    return str(p)


PLAIN_TGU = """factors:
  temp: cold, warm, hot
  ph: low, mid, high
array: L9
"""

# A factor name the binding's \\w+ regex cannot match. This is the live bug.
HYPHEN_TGU = """factors:
  batch: 1, 8
  kv-type: q4_0, q8_0
  threads: 4, 8
array: L4
"""

# Level values with spaces and punctuation, taken from the shipped cookie
# example. The design is only useful if these survive the round trip intact.
AWKWARD_TGU = """factors:
  butter: 1/2 cup, 3/4 cup
  ratio: 1:1 brown:white, 2:1 brown:white
array: L4
"""


# --------------------------------------------------------------------------
# generate_runs — the DESIGN
#
# A design with a missing run is not a design, so these check completeness and
# the orthogonality property the whole method rests on.
# --------------------------------------------------------------------------

class TestGenerateRuns:

    def test_returns_every_run_in_the_array(self, cli, tmp_path):
        runs = cli.generate_runs(write_tgu(tmp_path, PLAIN_TGU))
        assert len(runs) == 9, "L9 has nine runs"

    def test_run_ids_are_contiguous_from_one(self, cli, tmp_path):
        runs = cli.generate_runs(write_tgu(tmp_path, PLAIN_TGU))
        assert [r["run_id"] for r in runs] == list(range(1, 10))

    def test_every_run_sets_every_factor(self, cli, tmp_path):
        runs = cli.generate_runs(write_tgu(tmp_path, PLAIN_TGU))
        for r in runs:
            assert set(r["factors"]) == {"temp", "ph"}

    def test_values_come_from_the_declared_levels(self, cli, tmp_path):
        runs = cli.generate_runs(write_tgu(tmp_path, PLAIN_TGU))
        for r in runs:
            assert r["factors"]["temp"] in {"cold", "warm", "hot"}
            assert r["factors"]["ph"] in {"low", "mid", "high"}

    def test_design_is_balanced(self, cli, tmp_path):
        """Orthogonality: each level appears equally often. This is the
        property the array exists for, and a dropped or duplicated run breaks
        it -- so it catches a partial parse that a length check would not."""
        runs = cli.generate_runs(write_tgu(tmp_path, PLAIN_TGU))
        for factor in ("temp", "ph"):
            counts = Counter(r["factors"][factor] for r in runs)
            assert len(set(counts.values())) == 1, f"{factor} unbalanced: {counts}"
            assert len(counts) == 3

    def test_survives_a_factor_named_with_a_hyphen(self, cli, tmp_path):
        """`kv-type` is a legal factor name. The design must contain it."""
        runs = cli.generate_runs(write_tgu(tmp_path, HYPHEN_TGU))
        assert len(runs) == 4
        for r in runs:
            assert "kv-type" in r["factors"]

    def test_preserves_values_containing_spaces_and_punctuation(self, cli, tmp_path):
        runs = cli.generate_runs(write_tgu(tmp_path, AWKWARD_TGU))
        assert {r["factors"]["butter"] for r in runs} == {"1/2 cup", "3/4 cup"}
        assert {r["factors"]["ratio"] for r in runs} == {
            "1:1 brown:white", "2:1 brown:white"
        }


# --------------------------------------------------------------------------
# main_effects / recommend_optimal — the ANSWER
# --------------------------------------------------------------------------

def build_analyzer(tmp_path, body, responses, metric="response"):
    """Drive the public Experiment/Analyzer path end to end."""
    exp = Experiment()
    for line in body.strip().splitlines():
        line = line.strip()
        if not line or line.startswith(("factors:", "array:")):
            continue
        name, _, vals = line.partition(":")
        exp.add_factor(name.strip(), [v.strip() for v in vals.split(",")])
    an = Analyzer(exp, metric_name=metric)
    an.add_results_from_dict(responses)
    return exp, an


class TestMainEffects:

    def test_one_entry_per_factor(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU,
                               {i: float(i) for i in range(1, 10)})
        assert len(an.main_effects()) == 2

    def test_range_equals_spread_of_the_level_means_it_reports(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU,
                               {i: float(i) for i in range(1, 10)})
        for e in an.main_effects():
            expected = max(e["level_means"]) - min(e["level_means"])
            assert e["range"] == pytest.approx(expected, abs=1e-6)

    def test_one_mean_per_declared_level(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU,
                               {i: float(i) for i in range(1, 10)})
        for e in an.main_effects():
            assert len(e["level_means"]) == 3

    def test_negative_responses_survive(self, tmp_path):
        """Level means are routinely negative -- a latency delta, a signed
        score. A digits-only parse loses the sign or the row."""
        _, an = build_analyzer(tmp_path, PLAIN_TGU,
                               {i: -float(i) for i in range(1, 10)})
        effects = an.main_effects()
        assert any(m < 0 for e in effects for m in e["level_means"])
        assert all(e["range"] >= 0 for e in effects)


class TestRecommendation:
    """The recommendation is the deliverable. These use a MONOTONE response so
    maximising and minimising must disagree."""

    RESP = {1: 11.0, 2: 12.0, 3: 13.0,
            4: 21.0, 5: 22.0, 6: 23.0,
            7: 31.0, 8: 32.0, 9: 33.0}

    def test_maximising_picks_the_top_level(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU, self.RESP)
        assert an.recommend_optimal(higher_is_better=True) == {
            "temp": "hot", "ph": "high"}

    def test_minimising_picks_the_bottom_level(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU, self.RESP)
        assert an.recommend_optimal(higher_is_better=False) == {
            "temp": "cold", "ph": "low"}

    def test_the_two_objectives_disagree(self, tmp_path):
        """Guards the fixture as much as the code: a balanced response makes
        both objectives return the same answer, and then neither test above
        means anything."""
        _, an = build_analyzer(tmp_path, PLAIN_TGU, self.RESP)
        assert an.recommend_optimal(True) != an.recommend_optimal(False)

    def test_names_a_level_VALUE_not_an_index(self, tmp_path):
        """The CLI's text form says `temp=level_3`. Every value returned here
        must be one the user actually declared."""
        exp, an = build_analyzer(tmp_path, PLAIN_TGU, self.RESP)
        for factor, value in an.recommend_optimal().items():
            assert value in exp.factors[factor]

    def test_covers_every_factor(self, tmp_path):
        exp, an = build_analyzer(tmp_path, PLAIN_TGU, self.RESP)
        assert set(an.recommend_optimal()) == set(exp.factors)


class TestSignificantFactors:

    def test_ranks_by_effect_size(self, tmp_path):
        """temp moves the response by 20, ph by 2, so a 50% threshold keeps
        only temp."""
        _, an = build_analyzer(tmp_path, PLAIN_TGU, TestRecommendation.RESP)
        assert an.get_significant_factors(threshold=0.5) == ["temp"]

    def test_a_low_threshold_keeps_both(self, tmp_path):
        _, an = build_analyzer(tmp_path, PLAIN_TGU, TestRecommendation.RESP)
        assert set(an.get_significant_factors(threshold=0.01)) == {"temp", "ph"}


# --------------------------------------------------------------------------
# Known-broken TODAY. These are the switch's acceptance criteria.
#
# strict=True: if one starts passing, the suite FAILS until the marker is
# removed. A fixed bug that nobody notices is how a test suite drifts out of
# describing the code.
# --------------------------------------------------------------------------

class TestKnownGapsInTheTextParser:

    @pytest.mark.xfail(strict=True, reason="analyzer.py:86 matches the factor "
                                           "name with \\w+, which does not match "
                                           "'kv-type', so the factor is dropped "
                                           "silently. Fixed by moving to --json.")
    def test_hyphenated_factor_reaches_main_effects(self, tmp_path):
        _, an = build_analyzer(tmp_path, HYPHEN_TGU,
                               {1: -8.0, 2: -18.0, 3: -28.0, 4: -38.0})
        assert [e["factor"] for e in an.main_effects()] == [
            "batch", "kv-type", "threads"]

    @pytest.mark.xfail(strict=True, reason="same \\w+ drop: the factor never "
                                           "reaches the recommendation, so the "
                                           "answer silently omits a setting.")
    def test_hyphenated_factor_reaches_the_recommendation(self, tmp_path):
        exp, an = build_analyzer(tmp_path, HYPHEN_TGU,
                                 {1: -8.0, 2: -18.0, 3: -28.0, 4: -38.0})
        assert set(an.recommend_optimal()) == set(exp.factors)


# --------------------------------------------------------------------------
# The CLI's own --json contract, checked directly.
#
# This is what the binding should be reading. Pinning it here means the switch
# has a target that is already known to hold.
# --------------------------------------------------------------------------

class TestCliJsonContract:

    def run_json(self, cli, *args):
        out = subprocess.run([cli._cli_path, *args],
                             capture_output=True, text=True, check=True)
        return json.loads(out.stdout)

    def test_generate_json_is_loadable_and_complete(self, cli, tmp_path):
        d = self.run_json(cli, "generate", write_tgu(tmp_path, PLAIN_TGU), "--json")
        assert d["schema"] == 1
        assert d["run_count"] == len(d["runs"]) == 9
        assert d["factors"] == ["temp", "ph"]

    def test_generate_json_keeps_a_hyphenated_factor(self, cli, tmp_path):
        d = self.run_json(cli, "generate", write_tgu(tmp_path, HYPHEN_TGU), "--json")
        assert "kv-type" in d["factors"]
        assert all("kv-type" in r["settings"] for r in d["runs"])

    def test_analyze_json_carries_values_not_only_indices(self, cli, tmp_path):
        tgu = write_tgu(tmp_path, PLAIN_TGU)
        csv = tmp_path / "r.csv"
        csv.write_text("run_id,response\n" + "".join(
            "%d,%g\n" % (k, v) for k, v in sorted(TestRecommendation.RESP.items())))
        d = self.run_json(cli, "analyze", tgu, str(csv), "--json")
        assert d["objective"] == "maximize"
        for s in d["recommendation"]["settings"]:
            assert s["value"] is not None
        assert {s["factor"]: s["value"] for s in d["recommendation"]["settings"]} == {
            "temp": "hot", "ph": "high"}

    def test_analyze_json_respects_minimize(self, cli, tmp_path):
        tgu = write_tgu(tmp_path, PLAIN_TGU)
        csv = tmp_path / "r.csv"
        csv.write_text("run_id,response\n" + "".join(
            "%d,%g\n" % (k, v) for k, v in sorted(TestRecommendation.RESP.items())))
        d = self.run_json(cli, "analyze", tgu, str(csv), "--minimize", "--json")
        assert {s["factor"]: s["value"] for s in d["recommendation"]["settings"]} == {
            "temp": "cold", "ph": "low"}

    def test_unknown_option_is_rejected_not_ignored(self, cli, tmp_path):
        """The failure this whole exercise is about: a caller asking for a mode
        the binary lacks must get an error, not a human table and exit 0."""
        out = subprocess.run(
            [cli._cli_path, "generate", write_tgu(tmp_path, PLAIN_TGU), "--format", "json"],
            capture_output=True, text=True)
        assert out.returncode != 0
        assert "unknown option" in out.stderr.lower()


class TestBinaryDiscovery:

    def test_finds_the_umbrella_build_not_a_stale_sibling(self, cli):
        """The binding used to search optimize/taguchi/build/taguchi first --
        where taguchi put its binary when it built itself. That file survives
        in older trees, so the suite ran against a four-day-old binary and
        reported passes."""
        assert cli._cli_path.replace("\\", "/").endswith("build/bin/taguchi")

    def test_the_binary_it_found_supports_json(self, cli, tmp_path):
        """Cheap guard against the same class of mistake: if discovery ever
        picks up a binary predating --json, say so here rather than let a
        confusing parse failure surface somewhere else."""
        out = subprocess.run(
            [cli._cli_path, "generate", write_tgu(tmp_path, PLAIN_TGU), "--json"],
            capture_output=True, text=True)
        assert out.returncode == 0
        json.loads(out.stdout)
