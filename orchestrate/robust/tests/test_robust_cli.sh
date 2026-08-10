#!/usr/bin/env bash
# test_robust_cli.sh — the robust binary end to end.
#
# test_robust.c covers the funnel library; nothing covered the binary. `screen`
# in particular emits a KEEP/drop DECISION and had no machine path at all --
# --json was documented "funnel only" even though screen builds the same
# result -- so the one command whose entire output is a decision was the one
# a caller had to scrape.
set -u
ROBUST="${ROBUST:-build/bin/robust}"
TMP="build/robust_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$ROBUST" ] || { echo "robust not found at $ROBUST" >&2; exit 2; }
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }

HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
skip() { echo "  SKIP: $1  (no python3)"; }

json_ok() { local l="$1"; shift; "$@" >"$TMP/o" 2>"$TMP/e"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP/o" 2>"$TMP/pe"
    then ok "$l"; else bad "$l" "$(head -1 "$TMP/pe")"; fi; }

json_is() { local want="$1" expr="$2" l="$3"; shift 3; "$@" >"$TMP/o" 2>"$TMP/e"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    local got
    got=$(python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))' "$TMP/o" "$expr" 2>"$TMP/pe")
    [ "$got" = "$want" ] && ok "$l" || bad "$l" "got '${got:-$(head -1 "$TMP/pe")}', wanted '$want'"; }

cat > "$TMP/s.space" <<'EOF'
factors:
  loud: 0,10
  quiet: 0,10
  inert: 0,10
seed: 5
trajectories: 6
EOF
# `loud` dominates, `quiet` is a thousandth of it, `inert` does nothing --
# so the keep/drop decision is unambiguous and a test of it means something.
cat > "$TMP/m.sh" <<'EOF'
#!/bin/sh
awk -v a="$ROBUST_loud" -v b="$ROBUST_quiet" 'BEGIN{printf "%.6f\n", 10*a + 0.01*b}'
EOF
chmod +x "$TMP/m.sh"

expect_exit 0 "screen runs" "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh"
expect_match "factors kept" "screen reports how many survived" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh"
expect_exit 1 "no arguments exits 1" "$ROBUST"
expect_exit 0 "--version prints a version" "$ROBUST" --version

# ---- screen --json: the decision, as data --------------------------------
json_ok "screen --json - parses" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "screen" "d['stage']" "screen --json names the stage" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "1" "d['schema']" "screen --json carries a schema version" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "3" "len(d['morris'])" "screen --json lists every factor" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "True" "all(isinstance(m['mu_star'], (int, float)) for m in d['morris'])" \
    "screen --json: mu* is a number" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "True" "all(isinstance(m['keep'], bool) for m in d['morris'])" \
    "screen --json: keep is a boolean, not a KEEP/drop word" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "True" "d['n_survivors'] == sum(1 for m in d['morris'] if m['keep'])" \
    "screen --json: the survivor count matches the keep flags" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
# The decision itself: the dominant factor is kept, the inert one is not.
json_is "True" "[m['keep'] for m in d['morris'] if m['factor']=='loud'] == [True]" \
    "screen --json keeps the dominant factor" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
json_is "True" "[m['keep'] for m in d['morris'] if m['factor']=='inert'] == [False]" \
    "screen --json drops the inert factor" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -
# An empty sobol array on a screen is not the same claim as "Sobol found
# nothing", which is why `stage` exists.
json_is "0" "len(d['sobol'])" "screen --json carries an empty sobol array" \
    "$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json -

# --json - must put the document on stdout ALONE; the table goes to stderr so
# the two never collide in a pipe.
"$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json - >"$TMP/out" 2>"$TMP/err"
if grep -q "KEEP\|drop" "$TMP/out"; then
    bad "screen --json - keeps the table off stdout" "table text found in the document"
else
    ok "screen --json - keeps the table off stdout"
fi
grep -q "factors kept" "$TMP/err" \
    && ok "screen --json - still reports the count on stderr" \
    || bad "screen --json - still reports the count on stderr" "nothing on stderr"

# A path still writes a file, the behaviour funnel already had.
"$ROBUST" screen "$TMP/s.space" "$TMP/m.sh" --json "$TMP/s.json" >/dev/null 2>&1
if [ -s "$TMP/s.json" ]; then ok "screen --json PATH writes a file"
else bad "screen --json PATH writes a file" "no file"; fi
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP/s.json" 2>/dev/null \
        && ok "screen --json PATH writes valid JSON" \
        || bad "screen --json PATH writes valid JSON" "parse failed"
else
    skip "screen --json PATH writes valid JSON"
fi

# ---- funnel: the flagship command, previously untested ------------------
#
# cmd_funnel had ZERO CLI coverage -- the orchestrator's headline path, the one
# the README leads with. It runs in about 0.15s at these sizes, so there was
# never a cost reason for the gap.
cat > "$TMP/f.space" <<'EOF'
factors:
  loud: 0,10
  quiet: 0,10
seed: 5
trajectories: 4
samples: 32
EOF
expect_exit 0 "funnel runs" "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh"
expect_match "Sobol attribution" "funnel reports the attribution stage" \
    "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh"

json_ok "funnel --json - parses" "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json -
json_is "funnel" "d['stage']" "funnel --json names the stage" \
    "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json -
json_is "True" "len(d['sobol']) > 0" \
    "funnel --json carries Sobol indices, unlike a screen-only run" \
    "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json -
json_is "True" "all(k in x for x in d['sobol'] for k in ('factor','S1','ST'))" \
    "funnel --json: each index has its fields" \
    "$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json -

# The regression this guards: adding "-" support to the JSON writer left the
# progress banner, both tables and a "Wrote JSON: -" line on stdout AROUND the
# document, so `funnel --json - | jq` got something no parser would accept.
"$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json - >"$TMP/fo" 2>"$TMP/fe"
if grep -qE "Morris screening|Wrote JSON|Running funnel" "$TMP/fo"; then
    bad "funnel --json - keeps prose off stdout" "human output found in the document"
else
    ok "funnel --json - keeps prose off stdout"
fi
grep -q "Sobol attribution" "$TMP/fe" \
    && ok "funnel --json - still reports progress on stderr" \
    || bad "funnel --json - still reports progress on stderr" "nothing on stderr"

# Writing to a real path keeps the tables on stdout, as before.
"$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --json "$TMP/f.json" >"$TMP/fo2" 2>/dev/null
grep -q "Wrote JSON" "$TMP/fo2" \
    && ok "funnel --json PATH still says where it wrote" \
    || bad "funnel --json PATH still says where it wrote" "no confirmation line"
if [ "$HAVE_PY" -eq 1 ]; then
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP/f.json" 2>/dev/null \
        && ok "funnel --json PATH writes valid JSON" \
        || bad "funnel --json PATH writes valid JSON" "parse failed"
else
    skip "funnel --json PATH writes valid JSON"
fi

# --tgu hands the survivors to the optimize stage.
"$ROBUST" funnel "$TMP/f.space" "$TMP/m.sh" --tgu "$TMP/out.tgu" >/dev/null 2>&1
if [ -s "$TMP/out.tgu" ]; then ok "funnel --tgu writes an array for the survivors"
else bad "funnel --tgu writes an array for the survivors" "no file"; fi

echo
echo "robust CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
