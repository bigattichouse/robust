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

# all-inert note
{ echo "run_id,response"; "$MORRIS" sample "$TMP/f.space" | tail -n +2 \
  | awk '{printf "%d,1\n", NR}'; } > "$TMP/flat.csv"
expect_match "every mu\* is exactly zero" "analyze notes an all-inert result" \
    "$MORRIS" analyze "$TMP/f.space" "$TMP/flat.csv"

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

expect_exit 1 "unknown command exits 1" "$MORRIS" not-a-command "$TMP/f.space"
expect_exit 1 "no arguments exits 1" "$MORRIS"

echo
echo "morris CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
