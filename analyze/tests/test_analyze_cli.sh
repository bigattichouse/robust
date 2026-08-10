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

echo
echo "analyze/resolve CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
