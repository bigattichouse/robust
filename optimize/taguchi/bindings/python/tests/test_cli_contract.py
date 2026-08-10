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
import os
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
# Formerly broken: a factor named `kv-type` was dropped from the analysis
# because analyzer.py matched the name with \w+. These were
# xfail(strict=True) until the binding moved onto --json; they are plain
# tests now, which is the switch's acceptance criterion met.
# --------------------------------------------------------------------------

class TestHyphenatedFactorSurvives:

    RESP = {1: -8.0, 2: -18.0, 3: -28.0, 4: -38.0}

    def test_reaches_main_effects(self, tmp_path):
        _, an = build_analyzer(tmp_path, HYPHEN_TGU, self.RESP)
        assert [e["factor"] for e in an.main_effects()] == [
            "batch", "kv-type", "threads"]

    def test_reaches_the_recommendation(self, tmp_path):
        exp, an = build_analyzer(tmp_path, HYPHEN_TGU, self.RESP)
        assert set(an.recommend_optimal()) == set(exp.factors)

    def test_its_effect_is_measured_not_zeroed(self, tmp_path):
        """Dropping it and reporting it as inert are different failures, and
        only one of them is visible. Pin the real number."""
        _, an = build_analyzer(tmp_path, HYPHEN_TGU, self.RESP)
        kv = [e for e in an.main_effects() if e["factor"] == "kv-type"][0]
        assert kv["range"] > 0


# --------------------------------------------------------------------------
# The JSON reader refuses what the scraping parsers used to skip.
# --------------------------------------------------------------------------

class TestReaderIsStrict:

    def test_non_json_output_says_what_arrived(self):
        """The likely cause is a binary predating --json, which ignored the
        flag and printed the table at exit 0. The message has to name that,
        not surface a bare JSONDecodeError."""
        from taguchi._cli_json import runs_from_json
        with pytest.raises(Exception) as exc:
            runs_from_json("Generated 9 experiment runs:\nRun 1: temp=cold\n")
        assert "did not return JSON" in str(exc.value)

    def test_a_short_run_list_is_an_error_not_a_smaller_design(self):
        from taguchi._cli_json import runs_from_json
        doc = '{"schema": 1, "run_count": 9, "runs": ' \
              '[{"run_id": 1, "values": [], "settings": {}}]}'
        with pytest.raises(Exception) as exc:
            runs_from_json(doc)
        assert "declared 9 runs but listed 1" in str(exc.value)

    def test_a_future_schema_is_refused(self):
        from taguchi._cli_json import runs_from_json
        with pytest.raises(Exception) as exc:
            runs_from_json('{"schema": 99, "runs": []}')
        assert "schema" in str(exc.value)

    def test_the_error_is_a_taguchi_error(self):
        """The package has one TaguchiError; the reader must raise it."""
        from taguchi._cli_json import runs_from_json
        from taguchi.errors import TaguchiError
        import taguchi.core as core

        assert core.TaguchiError is TaguchiError
        with pytest.raises(TaguchiError):
            runs_from_json("not json")


class TestFromTguDoesNotEatYourFile:
    """`Experiment.from_tgu(path)` must not delete the file it was handed.

    cleanup() unlinked whatever _tgu_path named, and from_tgu pointed that at
    the CALLER's file, so

        with Experiment.from_tgu("my_experiment.tgu") as exp:
            ...

    destroyed the input on the way out of the with-block. Silent, immediate,
    and unrecoverable. Ownership is tracked now rather than assumed.
    """

    TGU = "factors:\n  a: 1, 2\n  b: 1, 2\narray: L4\n"

    def _write(self, tmp_path):
        p = tmp_path / "mine.tgu"
        p.write_text(self.TGU)
        return p

    def test_from_tgu_leaves_it_alone(self, tmp_path):
        from taguchi import Experiment
        p = self._write(tmp_path)
        with Experiment.from_tgu(str(p)):
            pass
        assert p.exists(), "from_tgu deleted the caller's file"

    def test_it_survives_a_failed_load(self, tmp_path):
        """__del__ had its own unlink that ignored ownership, so a from_tgu
        that RAISED left an object whose destructor deleted the file."""
        import gc
        from taguchi import Experiment

        p = tmp_path / "bad.tgu"
        p.write_text("\nfactors:\n  invalid name: low, high\n  temp: \n")
        try:
            Experiment.from_tgu(str(p))
        except Exception:
            pass
        gc.collect()
        assert p.exists(), "deleted the file after a failed load"

    def test_a_temp_file_it_created_is_still_cleaned_up(self, tmp_path):
        """The other half of the contract: don't fix the leak by leaking."""
        import os
        from taguchi import Experiment
        exp = Experiment()
        exp.add_factor("x", ["1", "2"])
        exp.add_factor("y", ["1", "2"])
        path = exp.get_tgu_path()
        assert os.path.exists(path)
        exp.cleanup()
        assert not os.path.exists(path), "its own temp file was left behind"


class TestArrayDiscovery:

    def test_mixed_level_arrays_are_visible(self):
        """L18 is mixed-level, so the old scraping regex -- which required a
        NUMBER before "levels" -- never matched it. The binding reported 19 of
        the tool's 20 arrays, and get_array_info("L18") raised "not found"."""
        from taguchi.core import Taguchi
        t = Taguchi()
        names = t.list_arrays()
        assert "L18" in names
        info = t.get_array_info("L18")
        assert info["rows"] == 18
        assert info["levels"] is None, "mixed-level must be null, not 0"

    def test_suggest_array_survives_a_mixed_level_entry(self):
        """Including L18 broke suggest_array, which compared levels
        numerically against None. Loud, but it had to be handled."""
        from taguchi.core import Taguchi
        assert Taguchi().suggest_array(num_factors=3, max_levels=3).startswith("L")


class TestExportedErrorActuallyCatches:
    """`from taguchi import TaguchiError` has to catch what the library raises.

    It did not. The package exported
    `class BackwardCompatibleTaguchiError(TaguchiError, _OriginalTaguchiError)`
    -- a SUBCLASS of both layers' error types. `except` matches a class or its
    ANCESTORS, so a subclass of both catches neither, and the documented name
    could not catch a single error this package produced.
    """

    def test_it_is_an_ancestor_of_both_layers(self):
        import taguchi
        import taguchi.core
        import taguchi.errors
        assert issubclass(taguchi.core.TaguchiError, taguchi.TaguchiError)
        assert issubclass(taguchi.errors.TaguchiError, taguchi.TaguchiError)

    def test_it_catches_the_enhanced_layer_in_practice(self):
        """The failure a user actually hits: the documented import, the
        documented call, and an `except` that has to fire."""
        import taguchi
        with pytest.raises(taguchi.TaguchiError):
            taguchi.Experiment().generate()      # no factors defined

    def test_it_catches_the_specific_subclasses(self):
        import taguchi
        for name in ("BinaryDiscoveryError", "CommandExecutionError",
                     "ValidationError"):
            assert issubclass(getattr(taguchi, name), taguchi.TaguchiError), name


class TestEnvironmentFlags:

    def test_common_spellings_of_true_are_honoured(self, monkeypatch):
        """TAGUCHI_DEBUG=1 and =yes did nothing: the parser tested
        `.lower() == "true"` only, so the two spellings a person reaches for
        first were silently ignored."""
        from taguchi.config import TaguchiConfig
        for value in ("1", "yes", "on", "true", "TRUE", "True"):
            monkeypatch.setenv("TAGUCHI_DEBUG", value)
            assert TaguchiConfig.from_environment().debug_mode is True, value
        for value in ("0", "no", "off", "false", "FALSE", ""):
            monkeypatch.setenv("TAGUCHI_DEBUG", value)
            assert TaguchiConfig.from_environment().debug_mode is False, value


class TestBinaryDiscovery:

    def test_does_not_pick_the_stale_legacy_sibling(self, cli):
        """The binding used to search optimize/taguchi/build/taguchi first --
        where taguchi put its binary when it built itself with a sub-make. That
        file survives in older trees, so the suite ran against a four-day-old
        binary and reported passes.

        Asserted as "not the legacy path", not as one literal location: the
        umbrella build is build/bin/taguchi normally and build/asan/bin/taguchi
        under `make test-asan`, and pinning one of those would fail the other
        for no reason. (It did.)"""
        path = cli._cli_path.replace("\\", "/")
        assert not path.endswith("optimize/taguchi/build/taguchi"), (
            "found the legacy sub-make path: %s" % path)
        assert path.endswith("/bin/taguchi"), (
            "expected an umbrella build layout (.../bin/taguchi), got %s" % path)

    def test_an_explicit_TAGUCHI_CLI_wins(self, cli):
        """CI pins the binary it just built. If the env var were ignored, the
        suite could silently test something else -- which is the whole reason
        this class exists."""
        want = os.environ.get("TAGUCHI_CLI")
        if not want:
            pytest.skip("TAGUCHI_CLI not set in this environment")
        assert os.path.realpath(Taguchi()._cli_path) == os.path.realpath(want)

    def test_the_binary_it_found_supports_json(self, cli, tmp_path):
        """Cheap guard against the same class of mistake: if discovery ever
        picks up a binary predating --json, say so here rather than let a
        confusing parse failure surface somewhere else."""
        out = subprocess.run(
            [cli._cli_path, "generate", write_tgu(tmp_path, PLAIN_TGU), "--json"],
            capture_output=True, text=True)
        assert out.returncode == 0
        json.loads(out.stdout)
