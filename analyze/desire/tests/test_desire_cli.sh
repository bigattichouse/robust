#!/usr/bin/env bash
# test_desire_cli.sh — the desire binary (E7).
set -u
BIN="${BIN:-build/bin}"
DESIRE="$BIN/desire"
TMP="build/desire_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$DESIRE" ] || { echo "desire not found at $DESIRE" >&2; exit 2; }
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }

cat > "$TMP/m.csv" <<'EOF'
run_id,yield,cost,cycle
1,90,10,5
2,50,5,9
3,95,50,2
4,70,20,6
EOF

expect_exit 0 "runs" "$DESIRE" --max yield --min cost "$TMP/m.csv"
expect_match "desirability" "appends a desirability column" \
    "$DESIRE" --max yield --min cost "$TMP/m.csv"

# The output must be the SAME CSV dialect plus one column, so the existing
# single-response pipeline runs on it unchanged.
in_cols=$(head -1 "$TMP/m.csv" | tr ',' '\n' | wc -l)
out_cols=$("$DESIRE" --max yield --min cost "$TMP/m.csv" | head -1 | tr ',' '\n' | wc -l)
[ "$out_cols" -eq "$((in_cols + 1))" ] && ok "output is the input plus one column" \
    || bad "output is the input plus one column" "$in_cols -> $out_cols"
rows=$("$DESIRE" --max yield --min cost "$TMP/m.csv" | tail -n +2 | wc -l)
[ "$rows" -eq 4 ] && ok "every row survives" || bad "every row survives" "$rows rows"

# GEOMETRIC mean, and that is the method: a zero on any objective takes the
# whole row to zero. Row 2 has the worst yield, row 3 the worst cost.
python3 - "$TMP/m.csv" <<'PY' && ok "a zero on one objective zeroes the row" \
    || bad "a zero on one objective zeroes the row" "see above"
import csv, io, subprocess, sys
out = subprocess.run(["build/bin/desire", "--max", "yield", "--min", "cost", sys.argv[1]],
                     capture_output=True, text=True).stdout
rows = {r["run_id"]: float(r["desirability"]) for r in csv.DictReader(io.StringIO(out))}
assert rows["2"] == 0.0, "worst yield should zero the row, got %s" % rows["2"]
assert rows["3"] == 0.0, "worst cost should zero the row, got %s" % rows["3"]
assert rows["1"] > rows["4"] > 0, "ordering wrong: %s" % rows
PY

# An arithmetic mean would rescue row 3 (best yield, worst cost). It must not.
python3 - "$TMP/m.csv" <<'PY' && ok "excelling elsewhere does not rescue a failure" \
    || bad "excelling elsewhere does not rescue a failure" "see above"
import csv, io, subprocess, sys
out = subprocess.run(["build/bin/desire", "--max", "yield", "--min", "cost", sys.argv[1]],
                     capture_output=True, text=True).stdout
rows = {r["run_id"]: float(r["desirability"]) for r in csv.DictReader(io.StringIO(out))}
# row 3 has the single best yield in the file, and still scores zero
assert rows["3"] == 0.0
PY

# Direction matters: flipping an objective must change the ranking.
python3 - "$TMP/m.csv" <<'PY' && ok "--max and --min are not interchangeable" \
    || bad "--max and --min are not interchangeable" "see above"
import csv, io, subprocess, sys
def score(*args):
    out = subprocess.run(["build/bin/desire", *args, sys.argv[1]],
                         capture_output=True, text=True).stdout
    return {r["run_id"]: float(r["desirability"]) for r in csv.DictReader(io.StringIO(out))}
a = score("--max", "yield")
b = score("--min", "yield")
assert a["3"] > a["2"] and b["2"] > b["3"], "direction ignored: %s vs %s" % (a, b)
PY

expect_match "desirability" "--target accepts COL:VALUE" \
    "$DESIRE" --target cycle:5 "$TMP/m.csv"
python3 - "$TMP/m.csv" <<'PY' && ok "--target peaks at the requested value" \
    || bad "--target peaks at the requested value" "see above"
import csv, io, subprocess, sys
out = subprocess.run(["build/bin/desire", "--target", "cycle:5", sys.argv[1]],
                     capture_output=True, text=True).stdout
rows = {r["run_id"]: float(r["desirability"]) for r in csv.DictReader(io.StringIO(out))}
assert rows["1"] == 1.0, "cycle=5 is the target and should score 1, got %s" % rows["1"]
PY

# stdin, so it composes in a pipe.
cat "$TMP/m.csv" | "$DESIRE" --max yield --min cost - > "$TMP/piped.csv" 2>/dev/null
[ -s "$TMP/piped.csv" ] && ok "reads stdin" || bad "reads stdin" "no output"

# ---- errors -------------------------------------------------------------
expect_exit 2 "no objectives exits 2" "$DESIRE" "$TMP/m.csv"
expect_exit 2 "no file exits 2" "$DESIRE" --max yield
expect_exit 2 "unknown option exits 2" "$DESIRE" --max yield --bogus "$TMP/m.csv"
expect_exit 2 "--target without a value exits 2" "$DESIRE" --target cycle "$TMP/m.csv"
expect_exit 2 "--target with a non-numeric value exits 2" \
    "$DESIRE" --target cycle:abc "$TMP/m.csv"
expect_exit 1 "an unknown column exits 1" "$DESIRE" --max nope "$TMP/m.csv"
expect_match "not found" "and names it" "$DESIRE" --max nope "$TMP/m.csv"
expect_exit 1 "a missing file exits 1" "$DESIRE" --max yield "$TMP/nope.csv"

printf 'run_id,yield\n' > "$TMP/empty.csv"
expect_exit 1 "a header with no rows exits 1" "$DESIRE" --max yield "$TMP/empty.csv"

echo
echo "desire CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
