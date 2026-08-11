#!/bin/sh
# regenerate.sh — rebuild everything in output/ from scratch.
#
#   ./regenerate.sh          (run from examples/cookies/)
#
# The committed output/ directory is there so you can read the whole study —
# every design, every result, every table and the final HTML page — without
# building or running anything.
#
# It is also how the tutorial stays honest. `examples/tests/test_examples.sh`
# re-runs this and compares, so if a tool's output changes and the README is
# not updated, the build fails rather than the docs quietly becoming fiction.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
BIN="${BIN:-$root/build/bin}"
out="output"          # relative on purpose: tools echo the paths they are
                      # given, and an absolute one would bake a home directory
                      # into the committed output and break the drift check

[ -x "$BIN/morris" ] || { echo "build the tools first: make -C $root" >&2; exit 2; }
BIN=$(cd "$BIN" && pwd)
cd "$here"
mkdir -p "$out"

say() { printf '  %s\n' "$1"; }

# ---- 1. screening --------------------------------------------------------
say "morris: design, bake, analyze"
"$BIN/morris" sample cookies.space                       > "$out/1-screen-design.csv"
./bake.sh "$out/1-screen-design.csv"                     > "$out/1-screen-results.csv"
"$BIN/morris" analyze cookies.space "$out/1-screen-results.csv" \
    --metric taste                                       > "$out/1-screen-analysis.txt" 2>&1
"$BIN/morris" analyze cookies.space "$out/1-screen-results.csv" \
    --metric taste --json 2>/dev/null                    > "$out/1-screen-analysis.json"

# ---- 2. attribution ------------------------------------------------------
say "sobol: design, bake, analyze"
"$BIN/sobol" sample cookies.space                        > "$out/2-attribute-design.csv"
./bake.sh "$out/2-attribute-design.csv"                  > "$out/2-attribute-results.csv"
"$BIN/sobol" analyze cookies.space "$out/2-attribute-results.csv" \
    --metric taste                                       > "$out/2-attribute-analysis.txt" 2>&1
"$BIN/sobol" analyze cookies.space "$out/2-attribute-results.csv" \
    --metric taste --json 2>/dev/null                    > "$out/2-attribute-analysis.json"

# ---- 3. resolve the interaction -----------------------------------------
say "grid + ofat: resolve"
"$BIN/grid" cookies.space ./model.sh --factors temp,time --levels 3 \
                                                         > "$out/3-grid-temp-x-time.txt" 2>&1
"$BIN/ofat" cookies.space ./model.sh --factor butter --levels 5 \
                                                         > "$out/3-ofat-butter.txt" 2>&1

# ---- 4. optimise, then test the prediction ------------------------------
say "taguchi: optimize + confirm"
"$BIN/taguchi" generate cookies.tgu --csv                > "$out/4-optimize-design.csv"
./bake.sh "$out/4-optimize-design.csv"                   > "$out/4-optimize-results.csv"
"$BIN/taguchi" analyze cookies.tgu "$out/4-optimize-results.csv" \
    --metric taste                                       > "$out/4-optimize-analysis.txt" 2>&1
"$BIN/taguchi" confirm cookies.tgu "$out/4-optimize-results.csv" \
    --metric taste                                       > "$out/4-confirm-prediction.txt" 2>&1
"$BIN/taguchi" confirm cookies.tgu "$out/4-optimize-results.csv" \
    --metric taste --measured 96.5                       > "$out/4-confirm-measured.txt" 2>&1

# ---- 5. the peak ---------------------------------------------------------
say "rsm: response surface"
"$BIN/rsm" sample rsm.space                              > "$out/5-rsm-design.csv"
./bake.sh "$out/5-rsm-design.csv"                        > "$out/5-rsm-results.csv"
"$BIN/rsm" analyze rsm.space "$out/5-rsm-results.csv" \
    --metric taste                                       > "$out/5-rsm-analysis.txt" 2>&1

# ---- 6. robustness -------------------------------------------------------
say "taguchi robust: control x noise"
"$BIN/taguchi" generate cookies-robust.tgu 2>/dev/null   > "$out/6-robust-design.csv"
./bake.sh "$out/6-robust-design.csv"                     > "$out/6-robust-results.csv"
"$BIN/taguchi" robust cookies-robust.tgu "$out/6-robust-results.csv" \
    --metric taste --sn larger                           > "$out/6-robust-analysis.txt" 2>&1

# ---- 7. several objectives at once --------------------------------------
say "desire + pareto: multi-objective"
./bake.sh "$out/1-screen-design.csv" --all               > "$out/7-multi-results.csv"
"$BIN/desire" --max taste --min cost --min minutes "$out/7-multi-results.csv" \
                                                         > "$out/7-desirability.csv"
"$BIN/pareto" filter "$out/7-multi-results.csv" --max taste --min cost \
                                                         > "$out/7-pareto-front.csv"

# ---- 8. the page ---------------------------------------------------------
say "report: study.html"
"$BIN/report" "$out/1-screen-analysis.json" "$out/2-attribute-analysis.json" \
    --html "$out/study.html"

printf '\nWrote %s files to output/\n' "$(ls -1 "$out" | wc -l)"
