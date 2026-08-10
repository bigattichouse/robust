#!/usr/bin/env bash
#
# test_analyze_cli.sh — regress, uq, ofat and grid, through their binaries.
#
# These four shipped with no tests, which showed up immediately as a coverage
# drop. They read untrusted files and take user arguments, so the error paths
# matter as much as the happy ones.
#
# Run from the repo root:  BIN=build/bin bash <this>

set -u
BIN="${BIN:-build/bin}"
TMP="build/analyze_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

for t in regress uq ofat grid; do
    [ -x "$BIN/$t" ] || { echo "$t not found in $BIN — run 'make all' first" >&2; exit 2; }
done
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }
expect_stderr() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/e" && ok "$l" || bad "$l" "stderr lacks '$p'"; }

# `--json` promises a document a program can load, so check it with a parser
# rather than grepping for a key and hoping. Skipped LOUDLY without python3 --
# a check that reports success without running is worse than no check.
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

cat > "$TMP/m.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
seed: 3
trajectories: 8
EOF

# y = 10a - 5b : a raises, b lowers, c inert. Exactly linear.
"$BIN/../bin/morris" sample "$TMP/m.space" 2>/dev/null \
  | awk -F, 'NR==1{print $0",response";next}{printf "%s,%.10g\n",$0,10*$2-5*$3}' > "$TMP/d.csv" \
  || build/bin/morris sample "$TMP/m.space" \
  | awk -F, 'NR==1{print $0",response";next}{printf "%s,%.10g\n",$0,10*$2-5*$3}' > "$TMP/d.csv"

# ---------------------------------------------------------------- regress
expect_exit 0 "regress: runs" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv"
expect_match "raises the response" "regress: reports a raising factor" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv"
expect_match "lowers the response" "regress: reports a lowering factor" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv"
expect_match "no effect" "regress: an inert factor is 'no effect', not a direction" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv"
expect_match "R^2 = 1.0000" "regress: exact linear model gives R^2 = 1" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv"
expect_exit 0 "regress: --ranks (SRRC)" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --ranks
expect_match "SRRC" "regress: --ranks says so" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --ranks
expect_exit 0 "regress: --json" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json
expect_match '"r2"' "regress: --json carries r2" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json
json_ok "regress: --json parses" "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json
json_is "3" "len(d['coefficients'])" "regress: --json lists every factor" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json
json_is "True" "all(isinstance(c['coef'], (int, float)) for c in d['coefficients'])" \
    "regress: --json coefficients are numbers" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json
json_is "True" "abs(d['r2'] - 1.0) < 1e-9" "regress: --json carries R^2 as a number" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --json

# A factor name may hold a quote -- the .space parser rejects only control
# characters -- and --metric comes straight from argv. Interpolated raw, either
# one produced a document no parser would accept, from the mode whose only
# purpose is being parsed.
cat > "$TMP/q.space" <<'EOF'
factors:
  a"b: 0,10
  c: 0,10
seed: 3
trajectories: 8
EOF
awk -F, 'NR==1{print "a\"b,c,resp\"onse";next}{printf "%s,%s,%.10g\n",$2,$4,10*$2}' \
    "$TMP/d.csv" > "$TMP/q.csv"
json_ok "regress: --json escapes a quoted factor name" \
    "$BIN/regress" "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json
json_is 'resp"onse' "d['metric']" "regress: --json round-trips a quoted metric" \
    "$BIN/regress" "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json
json_is "True" "'a\"b' in [c['factor'] for c in d['coefficients']]" \
    "regress: --json round-trips a quoted factor name" \
    "$BIN/regress" "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json
expect_exit 1 "regress: missing metric column exits 1" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --metric nope
expect_exit 1 "regress: missing file exits 1" "$BIN/regress" "$TMP/m.space" "$TMP/nope.csv"
expect_exit 2 "regress: no arguments exits 2" "$BIN/regress"
expect_exit 2 "regress: unknown option exits 2" \
    "$BIN/regress" "$TMP/m.space" "$TMP/d.csv" --bogus

# a CSV lacking a factor column must be refused, not silently regressed
awk -F, '{print $1","$3","$4","$5}' "$TMP/d.csv" > "$TMP/missing.csv"
expect_exit 1 "regress: a missing factor column exits 1" \
    "$BIN/regress" "$TMP/m.space" "$TMP/missing.csv"

# ---------------------------------------------------------------- uq
awk -F, 'NR==1{print "run_id,response";next}{print $1","$5}' "$TMP/d.csv" > "$TMP/u.csv"
expect_exit 0 "uq: runs" "$BIN/uq" "$TMP/u.csv"
expect_match "median" "uq: reports a median" "$BIN/uq" "$TMP/u.csv"
expect_match "histogram" "uq: draws a histogram" "$BIN/uq" "$TMP/u.csv"
expect_match "cdf" "uq: histogram carries the CDF" "$BIN/uq" "$TMP/u.csv"
expect_exit 0 "uq: --json" "$BIN/uq" "$TMP/u.csv" --json
expect_match '"p95"' "uq: --json carries percentiles" "$BIN/uq" "$TMP/u.csv" --json
json_ok "uq: --json parses" "$BIN/uq" "$TMP/u.csv" --json
json_is "True" "d['min'] <= d['p05'] <= d['p50'] <= d['p95'] <= d['max']" \
    "uq: --json percentiles are ordered numbers" "$BIN/uq" "$TMP/u.csv" --json
# --metric is argv, so it is the field a user can put a quote in.
awk -F, 'NR==1{print "run_id,resp\"onse";next}{print}' "$TMP/u.csv" > "$TMP/uq.csv"
json_ok "uq: --json escapes a quoted metric" \
    "$BIN/uq" "$TMP/uq.csv" --metric 'resp"onse' --json
json_is 'resp"onse' "d['metric']" "uq: --json round-trips a quoted metric" \
    "$BIN/uq" "$TMP/uq.csv" --metric 'resp"onse' --json
expect_exit 2 "uq: bad --bins exits 2" "$BIN/uq" "$TMP/u.csv" --bins 0
expect_exit 1 "uq: missing metric exits 1" "$BIN/uq" "$TMP/u.csv" --metric nope
expect_exit 2 "uq: no arguments exits 2" "$BIN/uq"

# a constant response must be called out, not summarised into zeros
{ echo "run_id,response"; for i in $(seq 1 10); do echo "$i,42"; done; } > "$TMP/const.csv"
expect_match "no distribution" "uq: a constant response is called out" \
    "$BIN/uq" "$TMP/const.csv"

# ---------------------------------------------------------------- ofat
cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
awk -v a="$OFAT_a" -v g="$GRID_a" -v gb="$GRID_b" 'BEGIN{
  if (a != "") { printf "%.6f\n", a*a } else { printf "%.6f\n", g + gb + 0.2*g*gb }
}'
EOF
chmod +x "$TMP/run.sh"
expect_exit 0 "ofat: runs" "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor a
expect_match "Measured effect" "ofat: reports the measured effect" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor a
expect_match "Curvature" "ofat: detects curvature a 2-level design would miss" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor a --levels 3
expect_match "did NOT move" "ofat: an inert factor is named as aliasing" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor c
expect_exit 1 "ofat: unknown factor exits 1" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor nope
expect_exit 2 "ofat: --factor is required" "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh"
expect_exit 2 "ofat: bad --levels exits 2" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor a --levels 99
expect_exit 2 "ofat: bad --base exits 2" \
    "$BIN/ofat" "$TMP/m.space" "$TMP/run.sh" --factor a --base sideways

# ---------------------------------------------------------------- grid
expect_exit 0 "grid: runs" "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,b
expect_match "DO interact" "grid: finds a real interaction" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,b
expect_match "Interaction relative to the larger main effect" \
    "grid: judges against the main effects" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,b
expect_exit 2 "grid: one factor is not a grid" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a
expect_exit 2 "grid: four factors is refused" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,b,c,a
expect_exit 2 "grid: a factor named twice is refused" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,a
expect_exit 1 "grid: unknown factor exits 1" \
    "$BIN/grid" "$TMP/m.space" "$TMP/run.sh" --factors a,nope
expect_exit 2 "grid: --factors is required" "$BIN/grid" "$TMP/m.space" "$TMP/run.sh"

# ------------------------------------------------- ofat/grid --json
#
# The confirmation stage. A silent partial parse here means believing you
# confirmed an effect you did not -- which is worse than the screening case,
# because this is the run that was supposed to settle it.
cat > "$TMP/c.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
seed: 5
trajectories: 4
EOF
cat > "$TMP/inter.sh" <<'EOF'
#!/bin/sh
awk -v a="${OFAT_a:-$GRID_a}" -v b="${OFAT_b:-$GRID_b}" \
    'BEGIN{printf "%.5f\n", a + b + 0.2*a*b}'
EOF
chmod +x "$TMP/inter.sh"

json_ok "ofat --json parses" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --json
json_is "3" "len(d['points'])" "ofat --json emits every swept level" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --json
json_is "True" "all(isinstance(p['response'], (int, float)) for p in d['points'])" \
    "ofat --json: responses are numbers" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --json
json_is "True" "abs(d['range'] - (max(p['response'] for p in d['points']) - min(p['response'] for p in d['points']))) < 1e-9" \
    "ofat --json: range equals max-min of the points it reports" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --json
json_is "True" "d['moved']" "ofat --json states whether the factor moved at all" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --json
json_is "5" "d['levels']" "ofat --json honours --levels" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --levels 5 --json

# An inert factor is the verdict that matters most, and the one a digits-only
# parse of the table is most likely to mangle.
cat > "$TMP/flat.sh" <<'EOF'
#!/bin/sh
echo 42
EOF
chmod +x "$TMP/flat.sh"
json_is "False" "d['moved']" "ofat --json reports an inert factor as not moved" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/flat.sh" --factor a --json
json_is "0" "d['range']" "ofat --json: an inert factor has zero range" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/flat.sh" --factor a --json
expect_stderr "did NOT move the response" "ofat still warns on stderr in --json mode" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/flat.sh" --factor a --json

json_ok "grid --json parses" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --json
json_is "9" "len(d['points'])" "grid --json emits the full factorial" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --json
json_is "True" "d['interaction']['interacts']" \
    "grid --json: y = a + b + 0.2ab is flagged as interacting" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --json
# The documented case: a quarter of each main effect, but only 7.7% of total
# variation. Judged against the main effects, which is the question asked.
json_is "True" "abs(d['interaction']['relative_to_larger_main_effect'] - 0.25) < 0.01" \
    "grid --json: interaction is a quarter of the larger main effect" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --json
json_is "True" "abs(d['interaction']['share_of_total_variation'] - 0.077) < 0.005" \
    "grid --json: and only 7.7% of total variation" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --json
# An additive model must NOT be flagged.
cat > "$TMP/add.sh" <<'EOF'
#!/bin/sh
awk -v a="$GRID_a" -v b="$GRID_b" 'BEGIN{printf "%.5f\n", a + b}'
EOF
chmod +x "$TMP/add.sh"
json_is "False" "d['interaction']['interacts']" \
    "grid --json: an additive model is not flagged as interacting" \
    "$BIN/grid" "$TMP/c.space" "$TMP/add.sh" --factors a,b --json
# Three crossed factors have no single two-factor interaction, and the field
# says null rather than a number that would mean something else.
cat > "$TMP/c3.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
seed: 5
trajectories: 4
EOF
json_is "None" "d['interaction']" \
    "grid --json: three crossed factors report a null interaction" \
    "$BIN/grid" "$TMP/c3.space" "$TMP/add.sh" --factors a,b,c --levels 2 --json

expect_exit 2 "ofat rejects an unknown option" \
    "$BIN/ofat" "$TMP/c.space" "$TMP/inter.sh" --factor a --format json
expect_exit 2 "grid rejects an unknown option" \
    "$BIN/grid" "$TMP/c.space" "$TMP/inter.sh" --factors a,b --format json

echo
echo "analyze/resolve CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
