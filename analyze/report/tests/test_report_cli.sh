#!/usr/bin/env bash
# test_report_cli.sh — the report binary.
#
# report consumes the --json documents the analyze stages emit, so its inputs
# are files a user names: malformed, hostile and truncated JSON all have to
# produce an error rather than a half-drawn chart.
set -u
BIN="${BIN:-build/bin}"
REPORT="$BIN/report"
TMP="build/report_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$REPORT" ] || { echo "report not found at $REPORT" >&2; exit 2; }
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }

cat > "$TMP/f.space" <<'EOF'
factors:
  alpha: 0,10
  beta: 0,10
  gamma: 0,10
seed: 5
trajectories: 6
EOF
{ echo "run_id,response"; "$BIN/morris" sample "$TMP/f.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 10*$2 + 3*$3}'; } > "$TMP/r.csv"
"$BIN/morris" analyze "$TMP/f.space" "$TMP/r.csv" --json 2>/dev/null > "$TMP/m.json"

# ---- the happy path ------------------------------------------------------
expect_exit 0 "renders a morris document" "$REPORT" "$TMP/m.json"
expect_match "<svg" "emits an inline SVG chart" "$REPORT" "$TMP/m.json"
expect_match "Pareto chart of effects" "the chart is labelled for a screen reader" \
    "$REPORT" "$TMP/m.json"
expect_match "Morris screening" "names the tool that produced the document" \
    "$REPORT" "$TMP/m.json"
expect_match "cumulative" "shows the cumulative share" "$REPORT" "$TMP/m.json"

"$REPORT" "$TMP/m.json" --html "$TMP/out.html" >/dev/null 2>&1
[ -s "$TMP/out.html" ] && ok "--html writes a file" || bad "--html writes a file" "empty"

# Self-contained: a report that fetches anything is useless in five years, and
# worse, leaks what you were studying to whoever hosts the resource.
if grep -qE '(src|href)="https?:' "$TMP/out.html"; then
    bad "no external resources" "found an http reference"
else
    ok "no external resources"
fi

# ---- the numbers on the page --------------------------------------------
# Ranked descending, shares summing to 100, cumulative ending at 100.
python3 - "$TMP/out.html" <<'PY' && ok "table is ranked, shares total 100%" \
    || bad "table is ranked, shares total 100%" "see above"
import re, sys
h = open(sys.argv[1]).read()
rows = re.findall(r'<tr><td class="n">(\d+)</td><td>(.*?)</td>'
                  r'<td class="n">([-\d.e+]+)</td><td class="n">([\d.]+)%</td>'
                  r'<td class="n">([\d.]+)%</td></tr>', h)
assert rows, "no rows parsed"
vals = [float(r[2]) for r in rows]
assert vals == sorted(vals, reverse=True), "not descending: %s" % vals
assert abs(sum(float(r[3]) for r in rows) - 100.0) < 0.5, "shares do not total 100"
assert abs(float(rows[-1][4]) - 100.0) < 0.5, "cumulative does not end at 100"
PY

# The factor with the largest effect must come first: `alpha` moves the
# response by 10 per unit, `beta` by 3, `gamma` not at all.
expect_match "alpha" "the dominant factor appears" "$REPORT" "$TMP/m.json"
python3 - "$TMP/out.html" <<'PY' && ok "ranks the dominant factor first" \
    || bad "ranks the dominant factor first" "see above"
import re, sys
h = open(sys.argv[1]).read()
first = re.search(r'<tr><td class="n">1</td><td>(.*?)</td>', h).group(1)
assert first == "alpha", "rank 1 was %r" % first
PY

# ---- other tools ---------------------------------------------------------
cat > "$TMP/s.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
samples: 64
seed: 3
EOF
{ echo "run_id,response"; "$BIN/sobol" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 3*$2 + $3*$3}'; } > "$TMP/s.csv"
"$BIN/sobol" analyze "$TMP/s.space" "$TMP/s.csv" --json 2>/dev/null > "$TMP/s.json"
expect_match "Sobol attribution" "renders a sobol document" "$REPORT" "$TMP/s.json"

cat > "$TMP/e.tgu" <<'EOF'
factors:
  temp: cold, warm, hot
  ph: low, mid, high
array: L9
EOF
printf 'run_id,response\n1,11\n2,12\n3,13\n4,21\n5,22\n6,23\n7,31\n8,32\n9,33\n' > "$TMP/t.csv"
"$BIN/taguchi" analyze "$TMP/e.tgu" "$TMP/t.csv" --json 2>/dev/null > "$TMP/t.json"
expect_match "Taguchi main effects" "renders a taguchi document" "$REPORT" "$TMP/t.json"

# Several documents in one page, in the order given.
"$REPORT" "$TMP/m.json" "$TMP/s.json" "$TMP/t.json" --html "$TMP/all.html" >/dev/null 2>&1
n=$(grep -c "<h2>" "$TMP/all.html")
[ "$n" -eq 3 ] && ok "combines three documents into one page" \
                || bad "combines three documents into one page" "$n sections"

# ---- input it must refuse ------------------------------------------------
expect_exit 2 "no arguments exits 2" "$REPORT"
expect_exit 2 "unknown option exits 2" "$REPORT" "$TMP/m.json" --bogus
expect_exit 1 "a missing file exits 1" "$REPORT" "$TMP/nope.json"

echo 'not json at all' > "$TMP/bad.json"
expect_exit 1 "malformed JSON exits 1" "$REPORT" "$TMP/bad.json"
expect_match "byte" "the parse error says where it stopped" "$REPORT" "$TMP/bad.json"

head -c 120 "$TMP/m.json" > "$TMP/trunc.json"
expect_exit 1 "truncated JSON exits 1" "$REPORT" "$TMP/trunc.json"

echo '{"tool":"nope","command":"analyze","schema":1}' > "$TMP/unknown.json"
expect_exit 1 "an unrecognised tool exits 1" "$REPORT" "$TMP/unknown.json"
expect_match "don't know how to chart" "and says so" "$REPORT" "$TMP/unknown.json"

# A newer schema means a key may have been renamed or removed. Refuse rather
# than draw a chart from fields that moved.
python3 -c "
import json,sys
d=json.load(open('$TMP/m.json')); d['schema']=99
json.dump(d, open('$TMP/future.json','w'))"
expect_exit 1 "a future schema is refused" "$REPORT" "$TMP/future.json"
expect_match "schema" "and says why" "$REPORT" "$TMP/future.json"

echo '{"tool":"morris","command":"analyze","schema":1,"factors":[]}' > "$TMP/empty.json"
expect_exit 1 "an empty factor list exits 1" "$REPORT" "$TMP/empty.json"

# Deeply nested input must hit the depth cap, not the stack.
python3 -c "open('$TMP/deep.json','w').write('['*5000 + ']'*5000)"
expect_exit 1 "deeply nested JSON is refused" "$REPORT" "$TMP/deep.json"
expect_match "deep" "the depth cap is named" "$REPORT" "$TMP/deep.json"

# A factor name carrying HTML must be escaped, not rendered.
python3 -c "
import json
d=json.load(open('$TMP/m.json'))
d['factors'][0]['factor'] = '<script>x</script>'
json.dump(d, open('$TMP/xss.json','w'))"
"$REPORT" "$TMP/xss.json" --html "$TMP/xss.html" >/dev/null 2>&1
if grep -q "<script>x</script>" "$TMP/xss.html"; then
    bad "escapes HTML in a factor name" "raw <script> reached the page"
else
    ok "escapes HTML in a factor name"
fi
grep -q "&lt;script&gt;" "$TMP/xss.html" \
    && ok "and shows the name escaped" \
    || bad "and shows the name escaped" "no escaped form found"

echo
echo "report CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
