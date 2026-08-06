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

# ---------------------------------------------------------------- summary
echo
echo "taguchi CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
