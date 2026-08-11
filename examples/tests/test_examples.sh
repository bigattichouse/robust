#!/usr/bin/env bash
# test_examples.sh — every command in examples/cookies/README.md, actually run.
#
# A tutorial whose commands do not work is worse than no tutorial: it costs the
# reader their trust as well as their time. This runs the whole walkthrough and
# checks the conclusions the prose draws, so the docs cannot rot quietly.
set -u
BIN="${BIN:-build/bin}"
EX="examples/cookies"
TMP="build/examples_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
mkdir -p "$TMP" || exit 2
[ -x "$BIN/morris" ] || { echo "tools not built" >&2; exit 2; }

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
run() { local l="$1"; shift; if "$@" >"$TMP/o" 2>"$TMP/e"; then ok "$l"; else bad "$l" "$(tail -1 "$TMP/e")"; fi; }

HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
skip() { echo "  SKIP: $1  (no python3)"; }

# ---- the model itself ----------------------------------------------------
run "model.sh runs" "$EX/model.sh"
v=$("$EX/model.sh")
case "$v" in ''|*[!0-9.]*) bad "model.sh prints one bare number" "got '$v'";;
             *) ok "model.sh prints one bare number";; esac
v3=$("$EX/model.sh" --all | tr -cd , | wc -c)
[ "$v3" -eq 2 ] && ok "model.sh --all prints three metrics" \
                || bad "model.sh --all prints three metrics" "$v3 commas"

# The structure the tutorial depends on. If these stop holding, the prose is
# wrong and every conclusion below it is too.
hot_quick=$(MORRIS_temp=400 MORRIS_time=9  "$EX/model.sh")
cool_slow=$(MORRIS_temp=350 MORRIS_time=13 "$EX/model.sh")
burnt=$(    MORRIS_temp=400 MORRIS_time=14 "$EX/model.sh")
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c "
import sys
hq, cs, b = float('$hot_quick'), float('$cool_slow'), float('$burnt')
assert abs(hq-cs) < 6, 'hot-quick and cool-slow should brown alike: %s vs %s' % (hq, cs)
assert b < hq - 15, 'hot AND long should burn: %s vs %s' % (b, hq)
" 2>"$TMP/pe" && ok "browning is temperature x time (the interaction the docs claim)" \
             || bad "browning is temperature x time" "$(tail -1 "$TMP/pe")"
else
    skip "browning is temperature x time"
fi

# ---- 1. screening --------------------------------------------------------
run "morris sample" "$BIN/morris" sample "$EX/cookies.space"
"$BIN/morris" sample "$EX/cookies.space" > "$TMP/design.csv"
run "bake.sh runs a design" "$EX/bake.sh" "$TMP/design.csv"
"$EX/bake.sh" "$TMP/design.csv" > "$TMP/results.csv"
run "morris analyze" "$BIN/morris" analyze "$EX/cookies.space" "$TMP/results.csv" --metric taste

if [ "$HAVE_PY" -eq 1 ]; then
    "$BIN/morris" analyze "$EX/cookies.space" "$TMP/results.csv" --metric taste --json 2>/dev/null > "$TMP/m.json"
    python3 -c "
import json
d = json.load(open('$TMP/m.json'))
r = [f['factor'] for f in d['factors']]
mu = {f['factor']: f['mu_star'] for f in d['factors']}
assert set(r[:3]) == {'temp','butter','time'}, 'top three should be temp/butter/time, got %s' % r[:3]
assert r[-1] == 'egg', 'egg should rank last, got %s' % r[-1]
assert mu['egg'] < 0.05 * mu[r[0]], 'egg should be negligible: %s vs %s' % (mu['egg'], mu[r[0]])
" 2>"$TMP/pe" && ok "screening ranks temp/butter/time first and egg last, as documented" \
             || bad "screening ranking" "$(tail -1 "$TMP/pe")"
else
    skip "screening ranking"
fi

# ---- 2. attribution ------------------------------------------------------
"$BIN/sobol" sample "$EX/cookies.space" > "$TMP/sdesign.csv"
"$EX/bake.sh" "$TMP/sdesign.csv" > "$TMP/sresults.csv"
run "sobol analyze" "$BIN/sobol" analyze "$EX/cookies.space" "$TMP/sresults.csv" --metric taste

# ---- 3. the interaction --------------------------------------------------
run "grid crosses temp x time" "$BIN/grid" "$EX/cookies.space" "$EX/model.sh" --factors temp,time --levels 3
"$BIN/grid" "$EX/cookies.space" "$EX/model.sh" --factors temp,time --levels 3 --json 2>/dev/null > "$TMP/g.json"
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c "
import json
d = json.load(open('$TMP/g.json'))
assert d['interaction']['interacts'], 'temp x time must be flagged as interacting'
" 2>"$TMP/pe" && ok "grid confirms temp x time interact, as documented" \
             || bad "grid confirms temp x time interact" "$(tail -1 "$TMP/pe")"
else
    skip "grid confirms temp x time interact"
fi
run "ofat sweeps one factor" "$BIN/ofat" "$EX/cookies.space" "$EX/model.sh" --factor butter --levels 5

# ---- 4. optimise + confirm ----------------------------------------------
run "taguchi validates the array" "$BIN/taguchi" validate "$EX/cookies.tgu"
"$BIN/taguchi" generate "$EX/cookies.tgu" --csv > "$TMP/tdesign.csv"
"$EX/bake.sh" "$TMP/tdesign.csv" > "$TMP/tresults.csv"
run "taguchi analyze" "$BIN/taguchi" analyze "$EX/cookies.tgu" "$TMP/tresults.csv" --metric taste
run "taguchi confirm predicts" "$BIN/taguchi" confirm "$EX/cookies.tgu" "$TMP/tresults.csv" --metric taste
run "taguchi confirm judges a measurement" \
    "$BIN/taguchi" confirm "$EX/cookies.tgu" "$TMP/tresults.csv" --metric taste --measured 96.5

# ---- 5. response surface -------------------------------------------------
"$BIN/rsm" sample "$EX/rsm.space" > "$TMP/rdesign.csv"
"$EX/bake.sh" "$TMP/rdesign.csv" > "$TMP/rresults.csv"
run "rsm analyze" "$BIN/rsm" analyze "$EX/rsm.space" "$TMP/rresults.csv" --metric taste
"$BIN/rsm" analyze "$EX/rsm.space" "$TMP/rresults.csv" --metric taste --json 2>/dev/null > "$TMP/r.json"
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c "
import json
d = json.load(open('$TMP/r.json'))
assert d['stationary_point_kind'] == 'maximum', 'butter x temp should peak, got %s' % d['stationary_point_kind']
b = [s['value'] for s in d['settings'] if s['factor']=='butter'][0]
assert abs(b - 0.75) < 0.02, 'the butter peak is planted at 0.75, found %s' % b
" 2>"$TMP/pe" && ok "rsm finds the planted butter peak at 0.75, as documented" \
             || bad "rsm finds the planted peak" "$(tail -1 "$TMP/pe")"
else
    skip "rsm finds the planted peak"
fi

# ---- 6. robustness -------------------------------------------------------
"$BIN/taguchi" generate "$EX/cookies-robust.tgu" > "$TMP/cdesign.csv" 2>/dev/null
"$EX/bake.sh" "$TMP/cdesign.csv" > "$TMP/cresults.csv"
run "taguchi robust" "$BIN/taguchi" robust "$EX/cookies-robust.tgu" "$TMP/cresults.csv" \
    --metric taste --sn larger
"$BIN/taguchi" robust "$EX/cookies-robust.tgu" "$TMP/cresults.csv" --metric taste --sn larger --json 2>/dev/null > "$TMP/rb.json"
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c "
import json
d = json.load(open('$TMP/rb.json'))
assert d['recommendations_differ'], 'the README says robust and mean-optimal DIFFER here'
f = [x for x in d['factors'] if x['factor']=='flour'][0]
assert f['differs'], 'flour is the factor the README says differs'
assert float(f['robust_value']) > float(f['mean_value']), \
    'the README says the robust answer is MORE flour: %s vs %s' % (f['robust_value'], f['mean_value'])
" 2>"$TMP/pe" && ok "robust and mean-optimal differ on flour, as documented" \
             || bad "robust and mean-optimal differ on flour" "$(tail -1 "$TMP/pe")"
else
    skip "robust and mean-optimal differ on flour"
fi

# ---- 7. multi-objective --------------------------------------------------
"$EX/bake.sh" "$TMP/design.csv" --all > "$TMP/multi.csv"
run "desire scores three objectives" "$BIN/desire" --max taste --min cost --min minutes "$TMP/multi.csv"
run "pareto shows the trade-off set" "$BIN/pareto" filter "$TMP/multi.csv" --max taste --min cost
# the documented pipe: desire's output feeds the single-response tools
"$BIN/desire" --max taste --min cost --min minutes "$TMP/multi.csv" \
  | "$BIN/morris" analyze "$EX/cookies.space" - --metric desirability >/dev/null 2>&1 \
  && ok "desire pipes into morris, as documented" \
  || bad "desire pipes into morris" "pipeline failed"

# ---- 8. report -----------------------------------------------------------
"$BIN/morris" analyze "$EX/cookies.space" "$TMP/results.csv" --metric taste --json 2>/dev/null > "$TMP/m.json"
"$BIN/sobol"  analyze "$EX/cookies.space" "$TMP/sresults.csv" --metric taste --json 2>/dev/null > "$TMP/s.json"
run "report renders both documents" "$BIN/report" "$TMP/m.json" "$TMP/s.json" --html "$TMP/study.html"
grep -q "Pareto chart of effects" "$TMP/study.html" \
    && ok "the report contains the Pareto chart the README promises" \
    || bad "the report contains the Pareto chart" "not found"

# ---- convergence ---------------------------------------------------------
run "morris converge reaches a target" \
    "$BIN/morris" converge "$EX/cookies.space" "$EX/model.sh" --target-ci 40

echo
echo "examples tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
