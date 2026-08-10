#!/usr/bin/env bash
# test_morris_cli.sh — the morris binary: every subcommand, both screening
# modes, and the error paths. Coverage put this CLI at 20%: only `sample` was
# ever run, so `groups:` and `bifurcate` shipped unexercised end to end.
set -u
MORRIS="${MORRIS:-build/bin/morris}"
TMP="build/morris_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$MORRIS" ] || { echo "morris not found at $MORRIS" >&2; exit 2; }
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }
expect_stderr() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/e" && ok "$l" || bad "$l" "stderr lacks '$p'"; }

# ---- machine-readable output --------------------------------------------
#
# `analyze` is consumed by other tools, so "did it print something" is not the
# question -- "can a program read what it printed" is. These helpers ask that
# with a real JSON parser where one exists. Without python3 the strict checks
# are SKIPPED LOUDLY rather than silently passing: a check that reports success
# when it did not run is how the bug this file guards against got shipped.
HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
skip() { echo "  SKIP: $1  (no python3)"; }

# Run "$@", require stdout to be a JSON document a parser accepts.
json_ok() { local l="$1"; shift; "$@" >"$TMP/o" 2>"$TMP/e"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP/o" 2>"$TMP/pe"
    then ok "$l"; else bad "$l" "$(head -1 "$TMP/pe")"; fi; }

# Run "$@", then evaluate a python expression over the parsed document `d`
# and require it to print `want`. This is what a downstream consumer actually
# does: load, index, act -- not grep.
json_is() { local want="$1" expr="$2" l="$3"; shift 3; "$@" >"$TMP/o" 2>"$TMP/e"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    local got
    got=$(python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))' "$TMP/o" "$expr" 2>"$TMP/pe")
    [ "$got" = "$want" ] && ok "$l" || bad "$l" "got '${got:-$(head -1 "$TMP/pe")}', wanted '$want'"; }

# The whitespace contract of a fixed-width table, checked the way a positional
# parser reads it: split each data row on whitespace and demand the named field
# be a bare number. `cols` is the 1-based index; `ci` is an index that must hold
# a single bracketed token with no internal space.
table_fields_parse() { local num="$1" ci="$2" want_rows="$3" l="$4"; shift 4
    "$@" >"$TMP/o" 2>"$TMP/e"
    local r
    r=$(awk -v n="$num" -v c="$ci" '
        /^-----/  { rows = 1; next }
        rows && NF == 0 { rows = 0 }
        rows {
            total++
            if ($n !~ /^-?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?$/) badnum++
            if (c > 0 && $c !~ /^\[[^][ ]*,[^][ ]*\]$/) badci++
        }
        END { printf "%d %d %d", total+0, badnum+0, badci+0 }' "$TMP/o")
    [ "$r" = "$want_rows 0 0" ] && ok "$l" \
        || bad "$l" "rows/bad-number/bad-ci = $r, wanted '$want_rows 0 0'"; }

cat > "$TMP/f.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
seed: 5
trajectories: 6
EOF
cat > "$TMP/g.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
  d: 0,10
seed: 5
trajectories: 6
groups:
  first: a, b
  second: c, d
EOF
cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
awk -v a="$MORRIS_a" 'BEGIN{printf "%.6f\n", 10*a}'
EOF
chmod +x "$TMP/run.sh"

expect_exit 0 "validate accepts a good space" "$MORRIS" validate "$TMP/f.space"
expect_exit 1 "validate rejects a missing file" "$MORRIS" validate "$TMP/nope.space"
expect_exit 0 "sample runs" "$MORRIS" sample "$TMP/f.space"
expect_exit 0 "generate runs" "$MORRIS" generate "$TMP/f.space"
expect_match "Point 1" "generate lists points" "$MORRIS" generate "$TMP/f.space"
expect_exit 0 "run executes the script" "$MORRIS" run "$TMP/f.space" "$TMP/run.sh"
expect_exit 1 "run needs a script" "$MORRIS" run "$TMP/f.space"

# per-factor analyze
{ echo "run_id,response"; "$MORRIS" sample "$TMP/f.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2}'; } > "$TMP/r.csv"
expect_exit 0 "analyze runs" "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv"
expect_match "mu\*" "analyze reports mu*" "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv"
expect_exit 1 "analyze needs results" "$MORRIS" analyze "$TMP/f.space"
expect_match "95% CI" "analyze reports a confidence interval on mu*" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv"
expect_match "keep-share" "analyze: --keep-share reports the cut" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --keep-share 0.9
expect_exit 1 "analyze: a bad --keep-share exits 1" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --keep-share 2

# ---- the table stays positionally parseable -----------------------------
#
# The bug this guards: the 95% CI was added glued to mu* ("215.6[210,221]"),
# so a consumer splitting the row on whitespace read field 2 as a string that
# would not parse as a number. EVERY row failed identically, so the caller got
# an empty ranking rather than a partial one, took it for "nothing to rank",
# and skipped screening -- after paying for r*(k+1) real benchmark runs.
# Nothing errored and nothing warned.
table_fields_parse 2 3 3 "analyze: mu* is its own numeric column" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv"
table_fields_parse 4 3 3 "analyze: sigma survives the column before it" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv"
# A CI containing a space would split into two fields and shift everything
# after it -- the same failure, one column over.
"$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" > "$TMP/table.txt" 2>/dev/null
awk '/^-----/{r=1;next} r&&NF==0{r=0} r' "$TMP/table.txt" | grep -qE '\[[^]]* ' \
    && bad "analyze: no CI cell contains a space" "found '[nnn, nnn]' in a data row" \
    || ok "analyze: no CI cell contains a space"
# The header still advertises the interval, just as its own column.
grep -q '95% CI' "$TMP/table.txt" && ok "analyze: the CI column is still labelled" \
    || bad "analyze: the CI column is still labelled" "no '95% CI' header"

# ---- --json: the actual contract ----------------------------------------
json_ok "analyze --json parses" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "3" "len(d['factors'])" "analyze --json lists every factor" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "True" "all(isinstance(f['mu_star'], float) or isinstance(f['mu_star'], int) for f in d['factors'])" \
    "analyze --json: mu_star is a number, not a string" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "True" "all(k in f for f in d['factors'] for k in ('mu','mu_star','mu_star_lo','mu_star_hi','sigma','rank','share','interacting'))" \
    "analyze --json: the CI is separate fields, not glued into the value" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "True" "all(f['mu_star_lo'] <= f['mu_star'] <= f['mu_star_hi'] for f in d['factors'])" \
    "analyze --json: the interval brackets mu*" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "True" "[f['rank'] for f in d['factors']] == list(range(1, len(d['factors'])+1)) and all(a['mu_star'] >= b['mu_star'] for a,b in zip(d['factors'], d['factors'][1:]))" \
    "analyze --json: ranked by descending mu*" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "1" "d['schema']" "analyze --json carries a schema version" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "per-factor" "d['mode']" "analyze --json names the screening mode" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "24" "d['runs']" "analyze --json reports the runs it consumed" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
{ echo "run_id,latency"; "$MORRIS" sample "$TMP/f.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2}'; } > "$TMP/lat.csv"
json_is "latency" "d['metric']" "analyze --json echoes the metric" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/lat.csv" --metric latency --json

# Without a keep rule there is no cut, and the keys still exist so a consumer
# can read them unconditionally.
json_is "None" "d['keep']" "analyze --json: no --keep-share means keep is null" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "None" "d['gap_at_cut']" "analyze --json: gap is null with no cut" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "False" "d['cut_is_tie']" "analyze --json: cut_is_tie present with no cut" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "None" "d['cut_at']" "analyze --json: cut_at is null with no cut" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json

# r.csv moves one factor only, so every share keeps exactly one and the cut is
# never interesting. mix.csv moves two, at a 10:3 ratio, so 0.9 must keep both
# (0.77 of the mass is not enough) and 0.5 must keep one.
{ echo "run_id,response"; "$MORRIS" sample "$TMP/f.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2 + 3*$3}'; } > "$TMP/mix.csv"
json_is "2" "d['keep']['count']" "analyze --json: --keep-share 0.9 keeps two of three" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json
json_is "1" "d['keep']['count']" "analyze --json: a smaller share keeps fewer" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.5 --json
json_is "True" "d['keep']['factors'] == [f['factor'] for f in d['factors'][:d['keep']['count']]]" \
    "analyze --json: the kept list is the top of the ranking" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json
json_is "0.9" "d['keep']['share_requested']" \
    "analyze --json: the requested share is recorded" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json
json_is "keep-share" "d['cut_at']" "analyze --json: cut_at names where the gap was measured" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json
json_is "True" "d['keep']['share_achieved'] >= 0.9" \
    "analyze --json: the achieved share reaches what was asked" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json
# A 10:3 gap is wide, so the cut is resolvable and must NOT be flagged.
json_is "False" "d['cut_is_tie']" "analyze --json: a wide gap is not a tie" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.5 --json

# The table and the JSON are two renderings of one computation; if they ever
# disagree about where the cut fell, one of them is lying to somebody.
if [ "$HAVE_PY" -eq 1 ]; then
    jkeep=$("$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 --json 2>/dev/null \
            | python3 -c 'import json,sys; print(json.load(sys.stdin)["keep"]["count"])')
    tkeep=$("$MORRIS" analyze "$TMP/f.space" "$TMP/mix.csv" --keep-share 0.9 2>/dev/null \
            | sed -n 's/.*keeps \([0-9]*\) of.*/\1/p')
    [ "$jkeep" = "$tkeep" ] && ok "analyze: table and --json agree on the cut" \
        || bad "analyze: table and --json agree on the cut" "json $jkeep vs table $tkeep"
else
    skip "analyze: table and --json agree on the cut"
fi

# A near-tie at the cut: the ranking is not resolvable at any trajectory count,
# and a machine consumer must be told so -- on stderr AND in the document,
# because it will never read the stderr text.
cat > "$TMP/tie.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
seed: 5
trajectories: 6
EOF
{ echo "run_id,response"; "$MORRIS" sample "$TMP/tie.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2 + 10.02*$3 + 0.0001*$4}'; } > "$TMP/tie.csv"
expect_stderr "falls inside a" "analyze: a near-tie at the cut warns (table)" \
    "$MORRIS" analyze "$TMP/tie.space" "$TMP/tie.csv" --keep-share 0.5
expect_stderr "falls inside a" "analyze: a near-tie warns in --json mode too" \
    "$MORRIS" analyze "$TMP/tie.space" "$TMP/tie.csv" --keep-share 0.5 --json
json_ok "analyze --json stays parseable while warning" \
    "$MORRIS" analyze "$TMP/tie.space" "$TMP/tie.csv" --keep-share 0.5 --json
json_is "True" "d['cut_is_tie']" "analyze --json: cut_is_tie flags the near-tie" \
    "$MORRIS" analyze "$TMP/tie.space" "$TMP/tie.csv" --keep-share 0.5 --json
json_is "True" "1.0 < d['gap_at_cut'] < 1.05" "analyze --json: gap_at_cut is the ratio at the cut" \
    "$MORRIS" analyze "$TMP/tie.space" "$TMP/tie.csv" --keep-share 0.5 --json

# An unknown option used to be ignored, which is the same failure in miniature:
# a caller asking for a mode this build lacks would get the human table and
# exit 0 -- a silent wrong answer instead of an error.
expect_exit 1 "analyze rejects an unknown option instead of ignoring it" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --format json
expect_stderr "unknown option" "analyze names the option it rejected" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --format json

# Names are user-supplied and the parser allows anything but control
# characters, so the emitter has to escape rather than interpolate.
printf 'factors:\n  a"b: 0,10\n  c: 0,10\nseed: 5\ntrajectories: 4\n' > "$TMP/quote.space"
{ echo 'run_id,resp"onse'; "$MORRIS" sample "$TMP/quote.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2}'; } > "$TMP/quote.csv"
json_ok "analyze --json escapes a quote in a factor name" \
    "$MORRIS" analyze "$TMP/quote.space" "$TMP/quote.csv" --metric 'resp"onse' --json
json_is 'a"b' "d['factors'][0]['factor']" "analyze --json round-trips the quoted name" \
    "$MORRIS" analyze "$TMP/quote.space" "$TMP/quote.csv" --metric 'resp"onse' --json
json_is 'resp"onse' "d['metric']" "analyze --json round-trips a quoted metric" \
    "$MORRIS" analyze "$TMP/quote.space" "$TMP/quote.csv" --metric 'resp"onse' --json

# all-inert note
{ echo "run_id,response"; "$MORRIS" sample "$TMP/f.space" | tail -n +2 \
  | awk '{printf "%d,1\n", NR}'; } > "$TMP/flat.csv"
expect_match "every mu\* is exactly zero" "analyze notes an all-inert result" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/flat.csv"
# The note is the difference between "nothing matters" and "the harness is
# broken", so a machine consumer gets it as a field, not only as stderr prose.
expect_stderr "every mu\* is exactly zero" "analyze notes an all-inert result in --json mode" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/flat.csv" --json
json_is "True" "d['all_zero']" "analyze --json flags an all-inert result" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/flat.csv" --json
json_is "False" "d['all_zero']" "analyze --json: all_zero is false on a live result" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/r.csv" --json
json_is "0" "d['total_mu_star']" "analyze --json: an inert run totals zero" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/flat.csv" --json

# group mode is chosen by the file, not a flag
n=$("$MORRIS" sample "$TMP/g.space" | tail -n +2 | wc -l)
[ "$n" -eq 18 ] && ok "groups: sample emits r*(G+1) = 18 runs" \
                || bad "groups: sample emits r*(G+1)" "$n runs"
{ echo "run_id,response"; "$MORRIS" sample "$TMP/g.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2}'; } > "$TMP/gr.csv"
expect_match "GROUP effects" "groups: analyze switches mode from the file" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv"
expect_match "against .* for per-factor" "groups: analyze states the saving" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv"

# Group mode is the other half of the same interface: a consumer that switches
# a .space to `groups:` must not have to switch parsers too.
json_ok "groups: analyze --json parses" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "group" "d['mode']" "groups: --json names the mode" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "2" "len(d['groups'])" "groups: --json lists the groups" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "True" "all(k in g for g in d['groups'] for k in ('group','mu_star','sigma','members'))" \
    "groups: --json carries mu*, spread and membership" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "4" "sum(g['members'] for g in d['groups'])" \
    "groups: --json membership accounts for every factor" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "18" "d['runs']" "groups: --json reports r*(G+1) runs" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "30" "d['per_factor_runs']" "groups: --json states what per-factor would have cost" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
# spec/morris-groups.bp: both analyze paths must carry these.
json_is "True" "'gap_at_cut' in d and 'cut_is_tie' in d" \
    "groups: --json carries gap_at_cut and cut_is_tie" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "bottom" "d['cut_at']" "groups: --json says the gap is taken at the bottom" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json
json_is "1" "d['schema']" "groups: --json carries a schema version" \
    "$MORRIS" analyze "$TMP/g.space" "$TMP/gr.csv" --json

# bifurcate
expect_exit 0 "bifurcate runs" "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh"
expect_match "worst-case budget" "bifurcate prints the cost before running" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh"
expect_match "survivor" "bifurcate reports survivors" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh"
expect_match "below the 20" "bifurcate warns at low trajectories" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh"
expect_exit 1 "bifurcate needs a script" "$MORRIS" bifurcate "$TMP/f.space"
expect_exit 1 "bifurcate rejects a bad --keep-share" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --keep-share 5

# bifurcate's output IS a decision -- which factors survived -- reached by a
# different route than analyze, and it was prose only.
json_ok "bifurcate --json parses" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
json_is "True" "set(d['survivors']) | set(d['dropped']) == {'a','b','c'}" \
    "bifurcate --json accounts for every factor exactly once" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
json_is "True" "d['survivor_count'] == len(d['survivors'])" \
    "bifurcate --json: the count matches the list" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
json_is "True" "d['evaluations'] <= d['predicted_max']" \
    "bifurcate --json: spend never exceeds the budget predicted up front" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
# r=6 is below the measured threshold, where a factor can be dropped SILENTLY.
# A machine consumer never reads the stderr text, so it must be a field.
json_is "True" "d['low_trajectories']" \
    "bifurcate --json flags a trajectory count that can drop a real factor" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
json_is "20" "d['min_trajectories']" \
    "bifurcate --json states the threshold it judged against" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
expect_stderr "below the 20" "bifurcate warns on stderr in --json mode too" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
json_is "True" "all(k in t for t in d['trace'] for k in ('round','group','mu_star','members','kept'))" \
    "bifurcate --json: the trace carries each round's keep/drop" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --json
expect_exit 1 "bifurcate rejects an unknown option" \
    "$MORRIS" bifurcate "$TMP/f.space" "$TMP/run.sh" --format json

expect_exit 1 "unknown command exits 1" "$MORRIS" not-a-command "$TMP/f.space"
expect_exit 1 "no arguments exits 1" "$MORRIS"

echo
echo "morris CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
