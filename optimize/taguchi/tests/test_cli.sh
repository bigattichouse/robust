#!/usr/bin/env bash
#
# test_cli.sh — the taguchi command-line interface.
#
# src/cli/main.c is 407 lines and sat at 47%: the existing shell test drives
# only `analyze` and `effects`, so `generate`, `run`, `validate`,
# `suggest-array`, `list-arrays`, the global flags, and every error path went
# unexercised. Those are the paths a person actually types.
#
# Driven by $TAGUCHI so it runs against any build tree (build/bin,
# build/asan/bin). Run from the repo root:
#   TAGUCHI=build/bin/taguchi bash <this>

set -u

TAGUCHI="${TAGUCHI:-build/bin/taguchi}"
TMP="build/taguchi_cli_$$"
pass=0; fail=0

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

# Fail loudly rather than let "absence" assertions pass against no output.
if [ ! -x "$TAGUCHI" ]; then
    echo "taguchi binary not found at '$TAGUCHI' — run 'make taguchi' first" >&2
    exit 2
fi

mkdir -p "$TMP" || { echo "cannot create $TMP"; exit 2; }

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }

# expect_exit <code> <label> <cmd...>
expect_exit() {
    local want="$1" label="$2"; shift 2
    "$@" >"$TMP/out" 2>"$TMP/err"
    local got=$?
    if [ "$got" -eq "$want" ]; then ok "$label"
    else bad "$label" "exit $got, wanted $want"; fi
}

# expect_match <pattern> <label> <cmd...>   (searches stdout+stderr)
expect_match() {
    local pat="$1" label="$2"; shift 2
    "$@" >"$TMP/out" 2>"$TMP/err"
    if grep -qi -- "$pat" "$TMP/out" "$TMP/err"; then ok "$label"
    else bad "$label" "pattern '$pat' not found"; fi
}

# ---------------------------------------------------------------- fixtures
# ---- machine-readable output ---------------------------------------------
#
# generate and analyze were prose, and were being scraped. The Python binding
# split `Run 1: a=1, b=2` on ", " and "=", and matched the effects table with
# \s*(\w+)\s+([\d.]+)\s+(.+) -- both skipping any line that did not match.
# \w+ does not match a factor named `kv-type`, so that factor was silently
# dropped from the analysis. These check the documents with a real parser, and
# check the tables still hold the shape a positional reader needs.
#
# Without python3 the strict checks SKIP LOUDLY. A check that reports success
# without running is how the morris break shipped.
HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
skip() { echo "  SKIP: $1  (no python3)"; }

json_ok() { local l="$1"; shift; "$@" >"$TMP/j.out" 2>"$TMP/j.err"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP/j.out" 2>"$TMP/j.pe"
    then ok "$l"; else bad "$l" "$(head -1 "$TMP/j.pe")"; fi; }

json_is() { local want="$1" expr="$2" l="$3"; shift 3; "$@" >"$TMP/j.out" 2>"$TMP/j.err"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    local got
    got=$(python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))' "$TMP/j.out" "$expr" 2>"$TMP/j.pe")
    [ "$got" = "$want" ] && ok "$l" || bad "$l" "got '${got:-$(head -1 "$TMP/j.pe")}', wanted '$want'"; }

cat > "$TMP/exp.tgu" <<'EOF'
factors:
  temp: cold, warm, hot
  ph: low, mid, high
array: L9
EOF

# No array named — exercises the auto-selection path through the CLI.
cat > "$TMP/auto.tgu" <<'EOF'
factors:
  a: a1, a2, a3
  b: b1, b2, b3
EOF

cat > "$TMP/bad.tgu" <<'EOF'
this is not a valid definition
EOF

# ---------------------------------------------------------------- global flags
expect_exit 0 "--version exits 0" $TAGUCHI --version
# Output is "Taguchi Array Tool v1.5.0" -- assert a version number, not the word.
expect_match "v[0-9][0-9]*\.[0-9]" "--version reports a version number" $TAGUCHI --version
expect_exit 0 "--help exits 0" $TAGUCHI --help
expect_match "Commands:" "--help lists the commands" $TAGUCHI --help

# ---------------------------------------------------------------- list-arrays
expect_exit 0 "list-arrays exits 0" $TAGUCHI list-arrays
expect_match "L9" "list-arrays includes L9" $TAGUCHI list-arrays
expect_match "L27" "list-arrays includes L27" $TAGUCHI list-arrays

# ---------------------------------------------------------------- validate
expect_exit 0 "validate accepts a good definition" $TAGUCHI validate "$TMP/exp.tgu"
expect_exit 1 "validate rejects a malformed definition" $TAGUCHI validate "$TMP/bad.tgu"
expect_exit 1 "validate on a missing file exits 1" $TAGUCHI validate "$TMP/nope.tgu"

# ---------------------------------------------------------------- suggest-array
expect_exit 0 "suggest-array exits 0" $TAGUCHI suggest-array "$TMP/exp.tgu"
expect_match "L" "suggest-array names an array" $TAGUCHI suggest-array "$TMP/exp.tgu"
expect_exit 1 "suggest-array on a missing file exits 1" \
    $TAGUCHI suggest-array "$TMP/nope.tgu"

# ---------------------------------------------------------------- generate
$TAGUCHI generate "$TMP/exp.tgu" > "$TMP/gen.out" 2>"$TMP/gen.err"
if [ $? -eq 0 ] && [ -s "$TMP/gen.out" ]; then
    ok "generate produces output"
else
    bad "generate produces output" "exit non-zero or empty"
fi
# L9 means nine runs; every factor value must come from the definition.
if [ "$(grep -c 'cold\|warm\|hot' "$TMP/gen.out")" -ge 9 ]; then
    ok "generate emits at least the 9 L9 runs"
else
    bad "generate emits at least the 9 L9 runs" "$(grep -c 'cold\|warm\|hot' "$TMP/gen.out") matching lines"
fi
expect_exit 1 "generate on a missing file exits 1" $TAGUCHI generate "$TMP/nope.tgu"
expect_exit 1 "generate on a malformed file exits 1" $TAGUCHI generate "$TMP/bad.tgu"

# auto-selection through the CLI (no `array:` line)
expect_exit 0 "generate works without an array named" $TAGUCHI generate "$TMP/auto.tgu"

# ---------------------------------------------------------------- run
cat > "$TMP/model.sh" <<'EOF'
#!/bin/sh
# Echo a number that depends on the factor, proving env vars arrive.
case "$TAGUCHI_temp" in
  cold) echo 10 ;;
  warm) echo 20 ;;
  *)    echo 30 ;;
esac
EOF
chmod +x "$TMP/model.sh"

$TAGUCHI run "$TMP/exp.tgu" "$TMP/model.sh" > "$TMP/run.out" 2>"$TMP/run.err"
if [ $? -eq 0 ]; then ok "run executes a script"
else bad "run executes a script" "exit non-zero"; fi

expect_exit 1 "run with no script argument exits 1" $TAGUCHI run "$TMP/exp.tgu"
expect_exit 1 "run on a missing definition exits 1" \
    $TAGUCHI run "$TMP/nope.tgu" "$TMP/model.sh"

# ---------------------------------------------------------------- analyze/effects
cat > "$TMP/results.csv" <<'EOF'
run_id,response
1,10
2,20
3,30
4,20
5,30
6,10
7,30
8,10
9,20
EOF

expect_exit 0 "effects accepts results" $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv"
expect_match "temp" "effects names the factors" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv"
expect_exit 0 "analyze accepts results" $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv"
expect_match "optimal" "analyze reports an optimal configuration" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv"
expect_exit 0 "analyze --minimize is accepted" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv" --minimize

expect_exit 1 "analyze on a missing results file exits 1" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/nope.csv"
expect_exit 1 "analyze with no results argument exits 1" \
    $TAGUCHI analyze "$TMP/exp.tgu"
expect_exit 1 "effects on a missing results file exits 1" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/nope.csv"

# --minimize must actually change the answer: this response set is monotone in
# temp, so the best level under maximise and minimise cannot be the same.
$TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv"            > "$TMP/max.out" 2>&1
$TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv" --minimize > "$TMP/min.out" 2>&1
if diff -q "$TMP/max.out" "$TMP/min.out" >/dev/null; then
    bad "--minimize changes the recommendation" "output identical to maximise"
else
    ok "--minimize changes the recommendation"
fi

# ---------------------------------------------------------------- bad usage
expect_exit 1 "no arguments exits 1" $TAGUCHI
expect_exit 1 "unknown command exits 1" $TAGUCHI not-a-command
expect_match "unknown\|usage" "unknown command explains itself" $TAGUCHI not-a-command
expect_exit 1 "generate with no file exits 1" $TAGUCHI generate
expect_exit 1 "validate with no file exits 1" $TAGUCHI validate

# --metric naming a column that does not exist
expect_exit 1 "unknown --metric exits 1" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --metric nonexistent
expect_match "not found" "unknown --metric says so" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --metric nonexistent

# ---------------------------------------------------------------- --json
#
# A factor name the binding's \w+ regex cannot match. This is the live case:
# `kv-type` was dropped from the effects table with no error and no gap.
cat > "$TMP/hyphen.tgu" <<'EOF'
factors:
  batch: 1, 8
  kv-type: q4_0, q8_0
  threads: 4, 8
array: L4
EOF
{ echo "run_id,response"; for i in 1 2 3 4; do echo "$i,-$((10*i - 2)).0"; done; } > "$TMP/hyphen.csv"

# A response with a real GRADIENT.
#
# results.csv above is perfectly balanced -- every level mean comes out at
# exactly 20.000 and every range at 0 -- so maximize and minimize pick the same
# level and any test of the recommendation passes no matter what the code does.
# Verified: flipping max to min in the recommendation left that suite green.
# This one is monotone in both factors, so the two objectives must disagree.
cat > "$TMP/grad.csv" <<'EOF'
run_id,response
1,11
2,12
3,13
4,21
5,22
6,23
7,31
8,32
9,33
EOF

# generate --json: the DESIGN. A missing run is not a design.
json_ok "generate --json parses" $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "9" "d['run_count']" "generate --json reports the L9 run count" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "9" "len(d['runs'])" "generate --json emits every run" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "True" "all(len(r['values']) == d['factor_count'] for r in d['runs'])" \
    "generate --json: every run has a value for every factor" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "True" "[r['run_id'] for r in d['runs']] == list(range(1, 10))" \
    "generate --json: run ids are 1..N with no gaps" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "True" "all(list(r['settings'].values()) == r['values'] for r in d['runs'])" \
    "generate --json: settings and values agree" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "True" "all(set(r['settings']) == set(d['factors']) for r in d['runs'])" \
    "generate --json: settings key on the declared factors" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
# Orthogonality, the property the design exists for: every factor takes every
# level an equal number of times.
json_is "True" "all(len(set(len([r for r in d['runs'] if r['settings'][f] == v]) for v in set(r['settings'][f] for r in d['runs']))) == 1 for f in d['factors'])" \
    "generate --json: every factor is balanced across its levels" \
    $TAGUCHI generate "$TMP/exp.tgu" --json
json_is "1" "d['schema']" "generate --json carries a schema version" \
    $TAGUCHI generate "$TMP/exp.tgu" --json

# effects --json
json_ok "effects --json parses" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --json
json_is "True" "all(isinstance(e['range'], (int, float)) for e in d['effects'])" \
    "effects --json: range is a number, not a string" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --json
json_is "True" "all(isinstance(l['mean'], (int, float)) for e in d['effects'] for l in e['levels'])" \
    "effects --json: level means are separate numbers, not one glued cell" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --json
json_is "True" "all(l['value'] is not None for e in d['effects'] for l in e['levels'])" \
    "effects --json: each level carries its VALUE, not just its index" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --json
json_is "True" "all(abs(e['range'] - (max(l['mean'] for l in e['levels']) - min(l['mean'] for l in e['levels']))) < 1e-9 for e in d['effects'])" \
    "effects --json: range equals max-min of the level means it reports" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --json

# The live silent-drop: a hyphenated factor must survive into the document.
json_is "3" "len(d['effects'])" "effects --json keeps a factor named with a hyphen" \
    $TAGUCHI effects "$TMP/hyphen.tgu" "$TMP/hyphen.csv" --json
json_is "True" "'kv-type' in [e['factor'] for e in d['effects']]" \
    "effects --json names the hyphenated factor" \
    $TAGUCHI effects "$TMP/hyphen.tgu" "$TMP/hyphen.csv" --json
# Negative means are ordinary and must not be lost to a digits-only parse.
json_is "True" "any(l['mean'] < 0 for e in d['effects'] for l in e['levels'])" \
    "effects --json carries negative level means" \
    $TAGUCHI effects "$TMP/hyphen.tgu" "$TMP/hyphen.csv" --json

# analyze --json: the recommendation is the deliverable.
json_ok "analyze --json parses" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv" --json
json_is "maximize" "d['objective']" "analyze --json states the objective" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "minimize" "d['objective']" "analyze --json: --minimize is recorded" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json
json_is "True" "all(s['value'] is not None for s in d['recommendation']['settings'])" \
    "analyze --json: the recommendation carries values, not just level indices" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "True" "len(d['recommendation']['settings']) == d['factor_count']" \
    "analyze --json: every factor gets a recommended setting" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
# Maximizing must pick each factor's best level mean; minimizing its worst.
json_is "True" "all(s['mean'] == max(l['mean'] for e in d['effects'] if e['factor']==s['factor'] for l in e['levels']) for s in d['recommendation']['settings'])" \
    "analyze --json: maximizing picks the highest level mean" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "True" "all(s['mean'] == min(l['mean'] for e in d['effects'] if e['factor']==s['factor'] for l in e['levels']) for s in d['recommendation']['settings'])" \
    "analyze --json: minimizing picks the lowest level mean" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json

# The structured recommendation and the library's own string are two renderings
# of one decision. If they ever disagree, one of them is lying to somebody.
json_is "True" "d['recommendation']['text'] == ', '.join('%s=level_%d' % (s['factor'], s['level']) for s in d['recommendation']['settings'])" \
    "analyze --json: structured settings agree with the recommendation text" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "True" "d['recommendation']['text'] == ', '.join('%s=level_%d' % (s['factor'], s['level']) for s in d['recommendation']['settings'])" \
    "analyze --json: they agree when minimizing too" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json

json_is "['hot', 'high']" "[s['value'] for s in d['recommendation']['settings']]" \
    "analyze --json: maximizing a monotone response picks the top level" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "['cold', 'low']" "[s['value'] for s in d['recommendation']['settings']]" \
    "analyze --json: minimizing the same response picks the bottom level" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json

# A value carrying a quote must be escaped, not interpolated.
cat > "$TMP/quote.tgu" <<'EOF'
factors:
  a: pl"ain, other
  b: x, y
array: L4
EOF
json_ok "generate --json escapes a quote in a level value" \
    $TAGUCHI generate "$TMP/quote.tgu" --json
json_is "True" "any('pl\"ain' in r['values'] for r in d['runs'])" \
    "generate --json round-trips a quoted level value" \
    $TAGUCHI generate "$TMP/quote.tgu" --json

# Unknown options must not be ignored into a human table at exit 0.
expect_exit 1 "generate rejects an unknown option" \
    $TAGUCHI generate "$TMP/exp.tgu" --format json
expect_exit 1 "effects rejects an unknown option" \
    $TAGUCHI effects "$TMP/exp.tgu" "$TMP/results.csv" --format json
expect_exit 1 "analyze rejects an unknown option" \
    $TAGUCHI analyze "$TMP/exp.tgu" "$TMP/results.csv" --format json

# `help` and `version` as SUBCOMMANDS, distinct from --help/--version. Both
# were uncovered: two entry points nobody had ever run.
expect_exit 0 "help subcommand exits 0" $TAGUCHI help
expect_match "Commands:" "help subcommand lists the commands" $TAGUCHI help
expect_exit 0 "version subcommand exits 0" $TAGUCHI version
expect_match "v[0-9]" "version subcommand reports a version" $TAGUCHI version

# ---------------------------------------------------------------- list-arrays --json
json_ok "list-arrays --json parses" $TAGUCHI list-arrays --json
json_is "True" "len(d['arrays']) > 10" "list-arrays --json lists the arrays" \
    $TAGUCHI list-arrays --json
json_is "True" "all(k in a for a in d['arrays'] for k in ('name','runs','columns','levels','mixed_levels'))" \
    "list-arrays --json describes each array as fields, not a sentence" \
    $TAGUCHI list-arrays --json
json_is "True" "[a for a in d['arrays'] if a['name']=='L9'][0]['runs'] == 9" \
    "list-arrays --json: L9 has nine runs" $TAGUCHI list-arrays --json
# A mixed-level array reports null levels, not 0 -- "no levels" and "levels
# vary by column" are different statements and the table said "mixed" for one.
json_is "True" "all(a['levels'] is None for a in d['arrays'] if a['mixed_levels'])" \
    "list-arrays --json: mixed-level arrays report null, not zero" \
    $TAGUCHI list-arrays --json
expect_exit 1 "list-arrays rejects an unknown option" $TAGUCHI list-arrays --format json

# ---------------------------------------------------------------- name validation
# Control characters in a factor name reach the JSON emitters and
# setenv("TAGUCHI_<name>"), neither of which can represent them. The .space
# parser has always rejected them; this one did not.
printf 'factors:\n  a\001b: 1, 2\n  c: 1, 2\narray: L4\n' > "$TMP/ctl.tgu"
expect_exit 1 "validate rejects a control character in a factor name" \
    $TAGUCHI validate "$TMP/ctl.tgu"
expect_match "control character" "validate says why it rejected the name" \
    $TAGUCHI validate "$TMP/ctl.tgu"
# UTF-8 names must keep working: only bytes below 0x20 are refused.
printf 'factors:\n  caf\303\251: 1, 2\n  c: 1, 2\narray: L4\n' > "$TMP/utf.tgu"
expect_exit 0 "validate accepts a UTF-8 factor name" $TAGUCHI validate "$TMP/utf.tgu"
json_ok "generate --json handles a UTF-8 factor name" \
    $TAGUCHI generate "$TMP/utf.tgu" --json

# ---------------------------------------------------------------- confirm
#
# The confirmation run: M6's missing piece. An orthogonal array never runs the
# combination it recommends, so the additive prediction is a hypothesis and
# nothing in the analysis tests it.
#
# grad.csv is exactly additive (response = 10*temp_level + ph_level), so the
# prediction must be EXACT -- and run 9 of that design really did measure 33,
# which is what the model predicts for the recommended settings.
expect_exit 0 "confirm runs" $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv"
expect_match "Predicted response" "confirm predicts a response" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv"
expect_match "HYPOTHESIS" "confirm says the prediction is untested without a run" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv"
expect_match "HELD" "confirm: an additive response confirms" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured 33
expect_match "did NOT hold" "confirm: a measurement far from prediction fails" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured 5
expect_exit 1 "confirm rejects a non-numeric --measured" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured abc
expect_exit 1 "confirm rejects an unknown option" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --format json

json_ok "confirm --json parses" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "33" "d['predicted']" "confirm --json: an additive model predicts exactly" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "22" "d['grand_mean']" "confirm --json reports the grand mean" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --json
# prediction = grand + sum of the contributions it lists
json_is "True" "abs(d['predicted'] - (d['grand_mean'] + sum(s['contribution'] for s in d['settings']))) < 1e-9" \
    "confirm --json: prediction equals grand mean plus its own contributions" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "None" "d['additive_model_held']" \
    "confirm --json: no verdict without a measurement" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --json
json_is "True" "d['additive_model_held']" "confirm --json: exact measurement holds" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured 33 --json
json_is "False" "d['additive_model_held']" "confirm --json: a far measurement fails" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured 5 --json
json_is "0" "d['error']" "confirm --json: error is measured minus predicted" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --measured 33 --json
# minimizing must pick the other end and predict a different value
json_is "11" "d['predicted']" "confirm --json: --minimize predicts the low corner" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json
json_is "True" "[s['value'] for s in d['settings']] == ['cold', 'low']" \
    "confirm --json: --minimize recommends the low settings" \
    $TAGUCHI confirm "$TMP/exp.tgu" "$TMP/grad.csv" --minimize --json

# ---------------------------------------------------------------- robust (E5)
#
# The classic Taguchi "robust", and the thing this repo is named for: control
# factors inner, noise factors outer, crossed, and the recommendation is the
# setting least sensitive to what you cannot control.
#
# EXPANSION.md E5 states the validation exactly: "on a model with a known
# control x noise interaction, the S/N-optimal setting differs from the
# mean-optimal one exactly as constructed." So construct one.
cat > "$TMP/rb.tgu" <<'EOF'
factors:
  setting: low, high
  other: p, q
noise:
  temp: cold, hot
array: L4
EOF

expect_exit 0 "validate accepts a noise: section" $TAGUCHI validate "$TMP/rb.tgu"
expect_match "Crossed design" "generate crosses control against noise" \
    $TAGUCHI generate "$TMP/rb.tgu"
expect_match "temp" "the crossed design carries the noise columns" \
    $TAGUCHI generate "$TMP/rb.tgu"
# 4 control runs x 2 noise points, past the CSV header. The banner is on
# stderr, so stdout is the design and nothing else.
n=$($TAGUCHI generate "$TMP/rb.tgu" 2>/dev/null | tail -n +2 | wc -l)
[ "$n" -eq 8 ] && ok "generate emits inner x outer = 8 rows" \
                || bad "generate emits inner x outer = 8 rows" "$n rows"

# setting=high has the better MEAN but swings +/-8 with temp; setting=low is
# slightly lower on average and almost immune. Mean says high, S/N says low.
# the crossed banner goes to stderr, so stdout is already clean CSV
$TAGUCHI generate "$TMP/rb.tgu" 2>/dev/null > "$TMP/rb_design.csv"
python3 -c "
import csv
rows=list(csv.DictReader(open('$TMP/rb_design.csv')))
print('run_id,response')
for r in rows:
    base = 12 if r['setting']=='high' else 10
    swing = 8 if r['setting']=='high' else 0.2
    d = swing if r['temp']=='hot' else -swing
    print('%s,%.4f' % (r['run_id'], base + d))
" > "$TMP/rb.csv"

expect_exit 0 "robust runs on a crossed design" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv"
expect_match "differ" "robust flags the control x noise interaction" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv"

json_ok "robust --json parses" $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "True" "d['recommendations_differ']" \
    "robust --json: S/N and mean disagree, as constructed" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "low" "[f['robust_value'] for f in d['factors'] if f['factor']=='setting'][0]" \
    "robust --json: S/N picks the insensitive setting" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "high" "[f['mean_value'] for f in d['factors'] if f['factor']=='setting'][0]" \
    "robust --json: the mean picks the higher-average setting" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "8" "d['total_runs']" "robust --json reports the crossed run count" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "2" "d['outer_points']" "robust --json reports the outer array size" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
# The row with the wider spread must score LOWER on S/N -- that is the ratio
# doing its job, and it is what the whole crossed design buys.
json_is "True" "min(r['sn_db'] for r in d['rows']) < max(r['sn_db'] for r in d['rows'])" \
    "robust --json: a wider spread scores worse on S/N" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json
json_is "True" "sorted(d['rows'], key=lambda r: r['sd'])[0]['sn_db'] == max(r['sn_db'] for r in d['rows'])" \
    "robust --json: the tightest row has the best S/N" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --json

# A response with no control x noise interaction must NOT be flagged.
python3 -c "
import csv
rows=list(csv.DictReader(open('$TMP/rb_design.csv')))
print('run_id,response')
for r in rows:
    base = 12 if r['setting']=='high' else 10
    d = 3 if r['temp']=='hot' else -3      # same swing either way
    print('%s,%.4f' % (r['run_id'], base + d))
" > "$TMP/rb_add.csv"
json_is "False" "d['recommendations_differ']" \
    "robust --json: no interaction means the columns agree" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb_add.csv" --json

expect_exit 1 "robust rejects a .tgu with no noise: section" \
    $TAGUCHI robust "$TMP/exp.tgu" "$TMP/grad.csv"
expect_match "no .noise:. section" "and says why" \
    $TAGUCHI robust "$TMP/exp.tgu" "$TMP/grad.csv"
expect_exit 1 "robust rejects an unknown --sn" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --sn sideways
expect_exit 1 "robust rejects an unknown option" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb.csv" --format json
# A crossed design needs every pair: a missing one is a missing number.
head -5 "$TMP/rb.csv" > "$TMP/rb_short.csv"
expect_exit 1 "robust refuses an incomplete crossed design" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb_short.csv"
expect_match "missing response" "and names the gap" \
    $TAGUCHI robust "$TMP/rb.tgu" "$TMP/rb_short.csv"

# ---------------------------------------------------------------- summary
echo
echo "taguchi CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
