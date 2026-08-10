#!/usr/bin/env bash
# test_sobol_cli.sh — the sobol binary end to end.
#
# There was no CLI suite for sobol at all: test_sobol.c covers the estimator,
# nothing covered the thing users and downstream tools actually run. That gap
# is how `analyze` shipped a table printing "0.812[0.79,0.83]" -- one token
# where a consumer expects two -- with every unit test green.
set -u
SOBOL="${SOBOL:-build/bin/sobol}"
TMP="build/sobol_cli_$$"
pass=0; fail=0
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
[ -x "$SOBOL" ] || { echo "sobol not found at $SOBOL" >&2; exit 2; }
mkdir -p "$TMP" || exit 2

ok()  { pass=$((pass+1)); echo "  PASS: $1"; }
bad() { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }
expect_exit() { local w="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"; local g=$?
    [ "$g" -eq "$w" ] && ok "$l" || bad "$l" "exit $g, wanted $w"; }
expect_match() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/o" "$TMP/e" && ok "$l" || bad "$l" "no '$p'"; }
expect_stderr() { local p="$1" l="$2"; shift 2; "$@" >"$TMP/o" 2>"$TMP/e"
    grep -q -- "$p" "$TMP/e" && ok "$l" || bad "$l" "stderr lacks '$p'"; }

# Strict JSON checks need a parser. Without python3 they are SKIPPED LOUDLY --
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

# Read the fixed-width table the way a positional parser does: split on
# whitespace, demand a bare number at `num`, and a single bracketed token with
# no internal space at `ci`.
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

cat > "$TMP/s.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
samples: 64
seed: 3
EOF
cat > "$TMP/s2.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
  c: 0,10
samples: 64
seed: 3
second_order: true
EOF
cat > "$TMP/run.sh" <<'EOF'
#!/bin/sh
awk -v a="$SOBOL_a" 'BEGIN{printf "%.6f\n", 10*a}'
EOF
chmod +x "$TMP/run.sh"

# ---- the subcommands ----------------------------------------------------
expect_exit 0 "validate accepts a good space" "$SOBOL" validate "$TMP/s.space"
expect_exit 1 "validate rejects a missing file" "$SOBOL" validate "$TMP/nope.space"
expect_match "Sampler:" "validate names the sampler" "$SOBOL" validate "$TMP/s.space"
expect_exit 0 "sample runs" "$SOBOL" sample "$TMP/s.space"
expect_exit 0 "generate runs" "$SOBOL" generate "$TMP/s.space"
expect_match "block A" "generate shows the Saltelli block layout" \
    "$SOBOL" generate "$TMP/s.space"
expect_exit 0 "run executes the script" "$SOBOL" run "$TMP/s.space" "$TMP/run.sh"
expect_exit 1 "run needs a script" "$SOBOL" run "$TMP/s.space"
expect_exit 1 "unknown command exits 1" "$SOBOL" not-a-command "$TMP/s.space"
expect_exit 1 "no arguments exits 1" "$SOBOL"
expect_exit 0 "--version prints a version" "$SOBOL" --version

# N * (k + 2) = 64 * 5 = 320 design points.
n=$("$SOBOL" sample "$TMP/s.space" | tail -n +2 | wc -l)
[ "$n" -eq 320 ] && ok "sample emits N*(k+2) = 320 runs" \
                 || bad "sample emits N*(k+2)" "$n runs"

# ---- analyze ------------------------------------------------------------
# y = 3a + b*c: `a` is purely first-order, b and c act only through their
# interaction, so ST-S1 must separate them.
{ echo "run_id,response"; "$SOBOL" sample "$TMP/s.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 3*$2 + $3*$4}'; } > "$TMP/r.csv"
expect_exit 0 "analyze runs" "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"
expect_exit 1 "analyze needs results" "$SOBOL" analyze "$TMP/s.space"
expect_match "95% CI" "analyze reports confidence intervals" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"
expect_match "Sum of first-order" "analyze reports the additivity check" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"

# The regression morris's analyze suffered, in the tool downstream of it: S1
# and ST were printed glued to their intervals ("0.812[0.79,0.83]"), so field 2
# of a whitespace-split row would not parse as a number.
table_fields_parse 2 3 3 "analyze: S1 is its own numeric column" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"
table_fields_parse 4 5 3 "analyze: ST is its own numeric column" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"
table_fields_parse 6 0 3 "analyze: the interaction column survives both CIs" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv"
"$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" > "$TMP/table.txt" 2>/dev/null
awk '/^-----/{r=1;next} r&&NF==0{r=0} r' "$TMP/table.txt" | grep -qE '\[[^]]* ' \
    && bad "analyze: no CI cell contains a space" "found '[nnn, nnn]' in a data row" \
    || ok "analyze: no CI cell contains a space"

# ---- analyze --json -----------------------------------------------------
json_ok "analyze --json parses" "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "3" "len(d['indices'])" "analyze --json lists every factor" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "True" "all(k in i for i in d['indices'] for k in ('factor','s1','s1_lo','s1_hi','st','st_lo','st_hi','interaction'))" \
    "analyze --json: each index and its bounds are separate fields" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "True" "all(isinstance(i['s1'], (int, float)) and isinstance(i['st'], (int, float)) for i in d['indices'])" \
    "analyze --json: the indices are numbers, not strings" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "True" "all(i['s1_lo'] <= i['s1_hi'] and i['st_lo'] <= i['st_hi'] for i in d['indices'])" \
    "analyze --json: the intervals are ordered" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "True" "all(abs(i['interaction'] - (i['st'] - i['s1'])) < 1e-9 for i in d['indices'])" \
    "analyze --json: interaction is ST - S1" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "1" "d['schema']" "analyze --json carries a schema version" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "sobol" "d['sampler']" "analyze --json records which sampler ran" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "320" "d['runs']" "analyze --json reports the runs it consumed" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "64" "d['samples']" "analyze --json reports the base sample count" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
json_is "None" "d['second_order']" "analyze --json: second_order is null when not requested" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json
# y = 3a + b*c is not additive, and the document must say so rather than
# leaving a consumer to infer it from a sum it has to recompute.
json_is "True" "d['additive'] == (d['sum_first_order'] > 0.9)" \
    "analyze --json: the additivity verdict matches the sum it reports" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --json

# second_order: the table stops at ten pairs; the JSON must not, or a consumer
# cannot tell a short list from a complete one.
{ echo "run_id,response"; "$SOBOL" sample "$TMP/s2.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 3*$2 + $3*$4}'; } > "$TMP/r2.csv"
expect_match "Second-order interactions" "analyze reports pairs when asked" \
    "$SOBOL" analyze "$TMP/s2.space" "$TMP/r2.csv"
json_ok "analyze --json parses with second_order" \
    "$SOBOL" analyze "$TMP/s2.space" "$TMP/r2.csv" --json
json_is "3" "len(d['second_order'])" "analyze --json carries every pair (k choose 2)" \
    "$SOBOL" analyze "$TMP/s2.space" "$TMP/r2.csv" --json
json_is "True" "all(k in p for p in d['second_order'] for k in ('a','b','s2','closed'))" \
    "analyze --json: each pair carries s2 and the closed index" \
    "$SOBOL" analyze "$TMP/s2.space" "$TMP/r2.csv" --json
# b and c interact and a does not, so the b/c pair must top the ranking.
json_is "True" "sorted((d['second_order'][0]['a'], d['second_order'][0]['b'])) == ['b','c']" \
    "analyze --json: pairs are ranked by interaction magnitude" \
    "$SOBOL" analyze "$TMP/s2.space" "$TMP/r2.csv" --json

# ---- error paths --------------------------------------------------------
expect_exit 1 "analyze rejects an unknown option instead of ignoring it" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --format json
expect_stderr "unknown option" "analyze names the option it rejected" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --format json
expect_exit 1 "analyze rejects a missing metric column" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/r.csv" --metric nope

# A constant response has no variance to share out; the indices are undefined
# and must error rather than print a table of -nan at exit 0.
{ echo "run_id,response"; "$SOBOL" sample "$TMP/s.space" | tail -n +2 \
  | awk '{printf "%d,1\n", NR}'; } > "$TMP/flat.csv"
expect_exit 1 "analyze rejects a constant response" \
    "$SOBOL" analyze "$TMP/s.space" "$TMP/flat.csv"

# Names come from user-written files and argv; the emitter escapes rather than
# interpolates, or one quote takes the whole document down.
cat > "$TMP/q.space" <<'EOF'
factors:
  a"b: 0,10
  c: 0,10
samples: 64
seed: 3
EOF
{ echo 'run_id,resp"onse'; "$SOBOL" sample "$TMP/q.space" | tail -n +2 \
  | awk -F, '{printf "%d,%.6f\n", NR, 3*$2 + $3}'; } > "$TMP/q.csv"
json_ok "analyze --json escapes a quote in a factor name" \
    "$SOBOL" analyze "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json
json_is 'resp"onse' "d['metric']" "analyze --json round-trips a quoted metric" \
    "$SOBOL" analyze "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json
json_is "True" "'a\"b' in [i['factor'] for i in d['indices']]" \
    "analyze --json round-trips the quoted factor name" \
    "$SOBOL" analyze "$TMP/q.space" "$TMP/q.csv" --metric 'resp"onse' --json

# ---- converge: spend runs until the indices resolve (E2) ----------------
#
# `samples:` is the guess this tool asks a .space author to make, and getting
# it wrong is quiet: a variance share with an interval spanning half the range
# still prints as a number.
cat > "$TMP/c.space" <<'EOF'
factors:
  a: 0,10
  b: 0,10
samples: 64
seed: 3
EOF
cat > "$TMP/cm.sh" <<'EOF'
#!/bin/sh
awk -v a="$SOBOL_a" -v b="$SOBOL_b" 'BEGIN{printf "%.6f\n", 3*a + b*b}'
EOF
chmod +x "$TMP/cm.sh"

expect_exit 0 "converge reaches a loose target" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2
expect_match "Converged at" "converge says where it landed" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2
expect_exit 1 "converge needs --target-ci" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh"
expect_exit 1 "converge rejects a non-positive target" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0
expect_exit 1 "converge rejects an unknown option" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2 --format json

# Capped without converging exits non-zero: a script must not read a wide
# interval as a narrow one.
expect_exit 1 "converge exits 1 when the cap is hit" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256
expect_stderr "Did NOT converge" "converge says so when capped" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256

json_ok "converge --json parses" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2 --json
json_is "True" "d['converged']" "converge --json reports success" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2 --json
json_is "True" "d['widest_ci'] <= d['target_ci']" \
    "converge --json: it stopped because the target was met" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 2 --json
json_is "False" "d['converged']" "converge --json reports a capped run" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256 --json
# Doubling from a power of two stays on the aligned blocks the sequence needs.
json_is "True" "[r['samples'] for r in d['rounds']] == [64, 128, 256]" \
    "converge --json: samples double each round" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256 --json
json_is "True" "d['rounds'][-1]['widest_ci'] < d['rounds'][0]['widest_ci']" \
    "converge --json: the interval narrows with more samples" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256 --json
json_is "True" "d['evaluations'] == sum(r['runs'] for r in d['rounds'])" \
    "converge --json: the spend is the sum of its rounds" \
    "$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.001 --max-samples 256 --json

# The claim the feature rests on: the reported N reproduces the run.
if [ "$HAVE_PY" -eq 1 ]; then
    conv=$("$SOBOL" converge "$TMP/c.space" "$TMP/cm.sh" --target-ci 0.7 --json 2>/dev/null)
    N=$(printf '%s' "$conv" | python3 -c 'import json,sys; print(json.load(sys.stdin)["samples"])')
    w=$(printf '%s' "$conv" | python3 -c 'import json,sys; print("%.6f" % json.load(sys.stdin)["widest_ci"])')
    sed "s/samples: 64/samples: $N/" "$TMP/c.space" > "$TMP/cr.space"
    { echo "run_id,response"; "$SOBOL" sample "$TMP/cr.space" | tail -n +2 \
      | awk -F, '{printf "%d,%.6f\n", NR, 3*$2 + $3*$3}'; } > "$TMP/cr.csv"
    w2=$("$SOBOL" analyze "$TMP/cr.space" "$TMP/cr.csv" --json 2>/dev/null \
         | python3 -c 'import json,sys; d=json.load(sys.stdin); print("%.6f" % max(max(i["s1_hi"]-i["s1_lo"], i["st_hi"]-i["st_lo"]) for i in d["indices"]))')
    same=$(python3 -c "import sys; print(abs(float(sys.argv[1])-float(sys.argv[2])) < 1e-4)" "$w" "$w2")
    [ "$same" = "True" ] && ok "converge: the reported N reproduces the run" \
        || bad "converge: the reported N reproduces the run" "$w vs $w2"
else
    skip "converge: the reported N reproduces the run"
fi

echo
echo "sobol CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
