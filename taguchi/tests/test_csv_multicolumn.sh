#!/bin/sh
# tests/test_csv_multicolumn.sh
#
# CLI integration tests for multi-column CSV metric selection.
# Tests the --metric flag for 'effects' and 'analyze' commands.
#
# Run via: make test   (or directly: bash tests/test_csv_multicolumn.sh)

TAGUCHI="${TAGUCHI:-./build/taguchi}"

# ---- setup ------------------------------------------------------------------
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

PASS=0
FAIL=0

pass() { printf "  PASS: %s\n" "$1"; PASS=$((PASS + 1)); }
fail() { printf "  FAIL: %s\n" "$1"; FAIL=$((FAIL + 1)); }

# command must exit 0
check_ok() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name  (command exited non-zero)"
    fi
}

# command must exit non-0 AND stderr/stdout must match grep pattern
check_fails_with() {
    local name="$1" pattern="$2"; shift 2
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" -eq 0 ]; then
        fail "$name  (expected failure but command succeeded)"
    elif echo "$out" | grep -q "$pattern"; then
        pass "$name"
    else
        fail "$name  (expected pattern '$pattern' not in: $out)"
    fi
}

# command must exit 0 AND output must match grep pattern
check_output() {
    local name="$1" pattern="$2"; shift 2
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" -ne 0 ]; then
        fail "$name  (command failed: $out)"
    elif echo "$out" | grep -q "$pattern"; then
        pass "$name"
    else
        fail "$name  (pattern '$pattern' not in: $out)"
    fi
}

# ---- shared fixtures --------------------------------------------------------

TGU="$TMPDIR_TEST/exp.tgu"
cat > "$TGU" << 'EOF'
factors:
  endpoint_type: endpoint_only, both_ends, mixed
  pressure: low, medium, high
array: L9
EOF

# Multi-column CSV: run_id + two factor columns + four metric columns.
# Runs 1-3 are endpoint_only; 4-6 are both_ends; 7-9 are mixed.
MULTI_CSV="$TMPDIR_TEST/multi_results.csv"
cat > "$MULTI_CSV" << 'EOF'
run_id,endpoint_type,pressure,system_COP,heating_COP,T_fridge_C,Qh_W
1,endpoint_only,low,3.21,4.12,-18.5,1200
2,endpoint_only,medium,2.98,3.87,-19.1,1180
3,endpoint_only,high,3.45,4.31,-17.8,1250
4,both_ends,low,3.10,4.05,-18.9,1210
5,both_ends,medium,2.87,3.76,-20.2,1160
6,both_ends,high,3.33,4.22,-18.3,1230
7,mixed,low,3.05,4.01,-19.4,1195
8,mixed,medium,3.18,4.14,-18.7,1205
9,mixed,high,2.92,3.81,-19.8,1170
EOF

# Traditional two-column CSV (backward compatibility).
TWO_COL_CSV="$TMPDIR_TEST/two_col.csv"
cat > "$TWO_COL_CSV" << 'EOF'
run_id,response
1,3.21
2,2.98
3,3.45
4,3.10
5,2.87
6,3.33
7,3.05
8,3.18
9,2.92
EOF

# Headerless CSV (first field is an integer → no header row).
NO_HDR_CSV="$TMPDIR_TEST/no_header.csv"
cat > "$NO_HDR_CSV" << 'EOF'
1,3.21
2,2.98
3,3.45
4,3.10
5,2.87
6,3.33
7,3.05
8,3.18
9,2.92
EOF

# ---- tests ------------------------------------------------------------------

printf "CSV Multi-Column Metric Tests:\n"

# --- named metric selection --------------------------------------------------

check_ok   "effects: --metric system_COP from multi-column CSV" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric system_COP

check_ok   "effects: --metric heating_COP from multi-column CSV" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric heating_COP

check_ok   "effects: --metric Qh_W from multi-column CSV" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric Qh_W

check_ok   "analyze: --metric T_fridge_C --minimize from multi-column CSV" \
    "$TAGUCHI" analyze "$TGU" "$MULTI_CSV" --metric T_fridge_C --minimize

# --- backward compatibility --------------------------------------------------

check_ok   "effects: 2-col CSV without --metric (default col 1)" \
    "$TAGUCHI" effects "$TGU" "$TWO_COL_CSV"

check_ok   "effects: 2-col CSV with --metric response (named 'response' col)" \
    "$TAGUCHI" effects "$TGU" "$TWO_COL_CSV" --metric response

check_ok   "effects: headerless CSV without --metric (default col 1)" \
    "$TAGUCHI" effects "$TGU" "$NO_HDR_CSV"

# --- end-to-end value checks -------------------------------------------------

# endpoint_type L1 (runs 1,2,3): system_COP = 3.21+2.98+3.45 = 9.64 / 3 = 3.213
check_output "end-to-end: system_COP endpoint_type L1 mean = 3.213" \
    "3.213" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric system_COP

# Qh_W output should label metric name in header line
check_output "end-to-end: Qh_W metric name appears in effects output" \
    "Qh_W" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric Qh_W

# heating_COP metric name should appear in analyze output label
check_output "end-to-end: heating_COP metric name in analyze output" \
    "heating_COP" \
    "$TAGUCHI" analyze "$TGU" "$MULTI_CSV" --metric heating_COP

# analyze recommend output present (any metric — use system_COP)
check_output "end-to-end: analyze produces Optimal Configuration line" \
    "Optimal Configuration" \
    "$TAGUCHI" analyze "$TGU" "$MULTI_CSV" --metric system_COP

# T_fridge_C with --minimize should say "minimizing" in output
check_output "end-to-end: --minimize reflected in analyze output" \
    "minimizing" \
    "$TAGUCHI" analyze "$TGU" "$MULTI_CSV" --metric T_fridge_C --minimize

# --- expected failure cases --------------------------------------------------

check_fails_with \
    "failure: --metric nonexistent gives 'not found in CSV header' error" \
    "not found in CSV header" \
    "$TAGUCHI" effects "$TGU" "$MULTI_CSV" --metric nonexistent

check_fails_with \
    "failure: named --metric on headerless CSV gives 'No header row' error" \
    "No header row" \
    "$TAGUCHI" effects "$TGU" "$NO_HDR_CSV" --metric system_COP

# --- summary -----------------------------------------------------------------

printf "\nCSV multi-column metric tests: %d passed, %d failed\n" "$PASS" "$FAIL"

[ "$FAIL" -eq 0 ] || exit 1
exit 0
