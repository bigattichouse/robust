#!/usr/bin/env bash
#
# test_pareto_cli.sh — the `cli:` section of spec/pareto.bp.
#
# test_pareto.c exercises the library; this exercises the *binary*: argument
# handling, exit codes, the text `why` prints, and the pipeline composition
# claim. Those were verified by hand when pareto was built and would otherwise
# rot.
#
# Driven by $PARETO so it works against any build tree (build/bin, build/asan/bin).
# Run from the repo root:  PARETO=build/bin/pareto bash <this>

set -u

PARETO="${PARETO:-build/bin/pareto}"
TMP="build/pareto_cli_$$"
pass=0; fail=0

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

# Fail loudly rather than let every "absence" assertion pass against no output.
if [ ! -x "$PARETO" ]; then
    echo "pareto binary not found at '$PARETO' — run 'make pareto' first" >&2
    exit 2
fi

mkdir -p "$TMP" || { echo "cannot create $TMP"; exit 2; }

ok()   { pass=$((pass+1)); echo "  PASS: $1"; }
bad()  { fail=$((fail+1)); echo "  FAIL: $1  ($2)"; }

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
    if grep -q -- "$pat" "$TMP/out" "$TMP/err"; then ok "$label"
    else bad "$label" "pattern '$pat' not found"; fi
}

# ---------------------------------------------------------------- fixtures
cat > "$TMP/b1.csv" <<'EOF'
run_id,yield,cost,temp
1,0.50,10,300
2,0.80,20,350
3,0.30,5,280
4,0.40,30,400
EOF

cat > "$TMP/b2.csv" <<'EOF'
run_id,yield,cost,temp
5,0.90,20,360
6,0.10,50,410
EOF

# ---------------------------------------------------------------- filter
$PARETO filter "$TMP/b1.csv" --max yield --min cost > "$TMP/front.csv" 2>/dev/null
if [ "$(grep -c . "$TMP/front.csv")" -eq 4 ]; then          # header + 3 rows
    ok "filter: keeps 3 of 4 rows (run 4 is dominated)"
else
    bad "filter: keeps 3 of 4 rows" "$(grep -c . "$TMP/front.csv") lines"
fi

if head -1 "$TMP/front.csv" | grep -q '^run_id,yield,cost,temp$'; then
    ok "filter: header preserved verbatim"
else
    bad "filter: header preserved verbatim" "$(head -1 "$TMP/front.csv")"
fi

if grep -q '^4,' "$TMP/front.csv"; then
    bad "filter: dominated row absent" "run 4 present"
else
    ok "filter: dominated row absent"
fi

# stdin
if cat "$TMP/b1.csv" | $PARETO filter - --max yield --min cost 2>/dev/null \
     | diff -q - "$TMP/front.csv" >/dev/null; then
    ok "filter: reads stdin identically to a file"
else
    bad "filter: reads stdin identically to a file" "output differs"
fi

# ---------------------------------------------------------------- store
$PARETO init --max yield --min cost --columns-from "$TMP/b1.csv" > "$TMP/s.front" 2>/dev/null
expect_exit 0 "init: writes a .front" test -s "$TMP/s.front"
expect_match "tgu-front" "init: emits the format magic" head -1 "$TMP/s.front"

$PARETO merge "$TMP/s.front" "$TMP/b1.csv" >/dev/null 2>"$TMP/m1"
expect_match "3 admitted" "merge: reports admissions" cat "$TMP/m1"
$PARETO merge "$TMP/s.front" "$TMP/b2.csv" >/dev/null 2>"$TMP/m2"
expect_match "1 evicted" "merge: reports the eviction of run 2" cat "$TMP/m2"
$PARETO merge "$TMP/s.front" "$TMP/b1.csv" >/dev/null 2>"$TMP/m3"
expect_match "0 admitted" "merge: re-merge admits nothing" cat "$TMP/m3"
expect_match "already present" "merge: re-merge reports duplicates" cat "$TMP/m3"

# ---------------------------------------------------------------- why
$PARETO why "$TMP/s.front" "$TMP/b1.csv" --run 2 > "$TMP/why2" 2>&1
expect_match "dominated by run 5" "why: names the dominator of an evicted run" cat "$TMP/why2"
expect_match "yield" "why: shows the per-objective margin" cat "$TMP/why2"

$PARETO why "$TMP/s.front" "$TMP/b1.csv" --run 3 > "$TMP/why3" 2>&1
expect_match "non-dominated" "why: reports a front member as non-dominated" cat "$TMP/why3"
if grep -q "dominated by run" "$TMP/why3"; then
    bad "why: front member names no dominator" "found 'dominated by run'"
else
    ok "why: front member names no dominator"
fi

# ---------------------------------------------------------------- compose
# The claim that makes the whole design work: a .front IS a results CSV.
$PARETO list "$TMP/s.front" 2>/dev/null | $PARETO filter - --max yield --min cost \
    > "$TMP/refiltered.csv" 2>/dev/null
if [ -s "$TMP/refiltered.csv" ]; then
    ok "compose: list | filter round-trips"
else
    bad "compose: list | filter round-trips" "empty output"
fi

# A front is already non-dominated, so re-filtering must change nothing.
$PARETO list "$TMP/s.front" 2>/dev/null > "$TMP/listed.csv"
if diff -q "$TMP/listed.csv" "$TMP/refiltered.csv" >/dev/null; then
    ok "compose: re-filtering a front is a no-op"
else
    bad "compose: re-filtering a front is a no-op" "output differs"
fi

# ---------------------------------------------------------------- errors
expect_exit 2 "error: one objective exits 2" \
    $PARETO filter "$TMP/b1.csv" --max yield
expect_match "sort" "error: one objective suggests sort" \
    $PARETO filter "$TMP/b1.csv" --max yield
expect_exit 1 "error: unknown column exits 1" \
    $PARETO filter "$TMP/b1.csv" --max yield --max nope
expect_match "available" "error: unknown column lists the real ones" \
    $PARETO filter "$TMP/b1.csv" --max yield --max nope
expect_exit 1 "error: missing file exits 1" \
    $PARETO merge "$TMP/s.front" "$TMP/nonexistent.csv"
expect_exit 2 "error: unknown command exits 2" \
    $PARETO bogus-command
expect_exit 2 "error: no arguments exits 2" $PARETO
expect_exit 0 "--version exits 0" $PARETO --version

# not-a-front
expect_exit 1 "error: merging into a plain CSV exits 1" \
    $PARETO merge "$TMP/b1.csv" "$TMP/b2.csv"
expect_match "tgu-front" "error: plain CSV names the missing magic" \
    $PARETO merge "$TMP/b1.csv" "$TMP/b2.csv"

# header mismatch
sed 's/temp/temperature/' "$TMP/b2.csv" > "$TMP/b3.csv"
expect_exit 1 "error: header mismatch exits 1" \
    $PARETO merge "$TMP/s.front" "$TMP/b3.csv"
expect_match "mismatch" "error: header mismatch says so" \
    $PARETO merge "$TMP/s.front" "$TMP/b3.csv"

# a failed merge must leave the front untouched
cp "$TMP/s.front" "$TMP/s.before"
$PARETO merge "$TMP/s.front" "$TMP/b3.csv" >/dev/null 2>&1
if diff -q "$TMP/s.front" "$TMP/s.before" >/dev/null; then
    ok "error: a rejected merge leaves the front byte-identical"
else
    bad "error: a rejected merge leaves the front byte-identical" "front changed"
fi

# ---------------------------------------------------------------- summary
echo
echo "pareto CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
