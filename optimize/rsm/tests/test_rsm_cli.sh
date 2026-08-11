#!/usr/bin/env bash
# test_rsm_cli.sh — the rsm binary (E4).
#
# EXPANSION.md states the validation: "recovers the known optimum of a
# synthetic quadratic bowl to tolerance; degenerate fits (saddle,
# rank-deficient) produce clean errors." So plant a bowl and check the tool
# finds it, then hand it surfaces that have no optimum to find.
set -u
BIN="${BIN:-build/bin}"
RSM="$BIN/rsm"
TMP="build/rsm_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$RSM" ] || { echo "rsm not found at $RSM" >&2; exit 2; }
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
json_is() { local want="$1" expr="$2" l="$3"; shift 3; "$@" >"$TMP/o" 2>"$TMP/e"
    [ "$HAVE_PY" -eq 1 ] || { skip "$l"; return; }
    local got
    got=$(python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))' "$TMP/o" "$expr" 2>"$TMP/pe")
    [ "$got" = "$want" ] && ok "$l" || bad "$l" "got '${got:-$(head -1 "$TMP/pe")}', wanted '$want'"; }

cat > "$TMP/s.space" <<'EOF'
factors:
  x: -10,10
  y: -10,10
seed: 1
EOF

# ---- the design ---------------------------------------------------------
expect_exit 0 "sample runs" "$RSM" sample "$TMP/s.space"
n=$("$RSM" sample "$TMP/s.space" | tail -n +2 | wc -l)
# 2^2 corners + 2*2 axial + 3 centres
[ "$n" -eq 11 ] && ok "sample emits corners + axial + centres = 11" \
                || bad "sample emits corners + axial + centres = 11" "$n rows"
# The axial points must reach BEYOND the corners -- that is what makes the
# pure quadratic terms estimable at all.
python3 - "$TMP/s.space" <<'PY' >/dev/null 2>&1 && ok "axial points extend past the corners" \
    || bad "axial points extend past the corners" "see above"
import subprocess, sys, csv, io
out = subprocess.run(["build/bin/rsm", "sample", sys.argv[1]], capture_output=True, text=True).stdout
rows = list(csv.DictReader(io.StringIO(out)))
xs = sorted(abs(float(r["x"])) for r in rows)
assert xs[-1] > xs[len(xs)//2] + 1e-9, "no point reaches past the corner radius"
PY

# ---- the stated validation: recover a planted optimum -------------------
# A bowl peaking at x=3, y=-4 with value 100.
{ echo "run_id,response"; "$RSM" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.10g\n", NR, 100 - 2*($2-3)*($2-3) - 3*($3+4)*($3+4)}'; } > "$TMP/bowl.csv"

expect_exit 0 "analyze runs" "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv"
expect_match "maximum" "finds a maximum" "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv"
json_is "maximum" "d['stationary_point_kind']" "json: reports a maximum" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
json_is "True" "abs([s['value'] for s in d['settings'] if s['factor']=='x'][0] - 3) < 1e-6" \
    "recovers the planted x = 3" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
json_is "True" "abs([s['value'] for s in d['settings'] if s['factor']=='y'][0] + 4) < 1e-6" \
    "recovers the planted y = -4" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
json_is "True" "abs(d['predicted'] - 100) < 1e-6" "recovers the peak value 100" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
json_is "True" "d['is_the_optimum_sought']" "a maximum is what --maximize wanted" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
json_is "True" "d['within_design_region']" "and it lies inside the region run" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --json
# Asking to minimise a bowl must say the surface turns the wrong way.
json_is "False" "d['is_the_optimum_sought']" "a maximum is NOT what --minimize wanted" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --minimize --json
expect_match "asked to minimize" "and says so" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --minimize

# An inverted bowl must be found as a minimum, or the verdict is not reading
# the surface at all.
{ echo "run_id,response"; "$RSM" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.10g\n", NR, 2*($2-3)*($2-3) + 3*($3+4)*($3+4)}'; } > "$TMP/cup.csv"
json_is "minimum" "d['stationary_point_kind']" "an inverted bowl is a minimum" \
    "$RSM" analyze "$TMP/s.space" "$TMP/cup.csv" --json

# ---- degenerate fits ----------------------------------------------------
{ echo "run_id,response"; "$RSM" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.10g\n", NR, $2*$2 - $3*$3}'; } > "$TMP/saddle.csv"
json_is "saddle" "d['stationary_point_kind']" "x^2 - y^2 is a saddle" \
    "$RSM" analyze "$TMP/s.space" "$TMP/saddle.csv" --json
json_is "False" "d['is_the_optimum_sought']" "and a saddle is not an optimum" \
    "$RSM" analyze "$TMP/s.space" "$TMP/saddle.csv" --json

{ echo "run_id,response"; "$RSM" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.10g\n", NR, 3*$2 + 2*$3}'; } > "$TMP/plane.csv"
expect_match "No stationary point" "a plane has no turning point" \
    "$RSM" analyze "$TMP/s.space" "$TMP/plane.csv"
json_is "none" "d['stationary_point_kind']" "json: reports none for a plane" \
    "$RSM" analyze "$TMP/s.space" "$TMP/plane.csv" --json

# An optimum outside the region run is extrapolation, and must be labelled.
{ echo "run_id,response"; "$RSM" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.10g\n", NR, 100 - 2*($2-90)*($2-90) - 3*($3+4)*($3+4)}'; } > "$TMP/far.csv"
json_is "False" "d['within_design_region']" "an optimum outside the region is flagged" \
    "$RSM" analyze "$TMP/s.space" "$TMP/far.csv" --json
expect_match "EXTRAPOLATION" "and the table says so" \
    "$RSM" analyze "$TMP/s.space" "$TMP/far.csv"

# ---- errors -------------------------------------------------------------
head -4 "$TMP/bowl.csv" > "$TMP/short.csv"
expect_exit 1 "an incomplete result set exits 1" \
    "$RSM" analyze "$TMP/s.space" "$TMP/short.csv"
expect_match "needs all of them" "and says why" \
    "$RSM" analyze "$TMP/s.space" "$TMP/short.csv"

cat > "$TMP/one.space" <<'EOF'
factors:
  x: -10,10
seed: 1
EOF
expect_exit 1 "one factor is refused" "$RSM" sample "$TMP/one.space"
expect_match "2 or 3 factors" "and says the range" "$RSM" sample "$TMP/one.space"

cat > "$TMP/four.space" <<'EOF'
factors:
  a: -1,1
  b: -1,1
  c: -1,1
  d: -1,1
seed: 1
EOF
expect_exit 1 "four factors are refused" "$RSM" sample "$TMP/four.space"

expect_exit 2 "no arguments exits 2" "$RSM"
expect_exit 2 "unknown command exits 2" "$RSM" wat "$TMP/s.space"
expect_exit 2 "unknown option exits 2" \
    "$RSM" analyze "$TMP/s.space" "$TMP/bowl.csv" --bogus
expect_exit 1 "a missing space file exits 1" "$RSM" sample "$TMP/nope.space"

echo
echo "rsm CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
