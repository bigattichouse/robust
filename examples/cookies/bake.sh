#!/bin/sh
# bake.sh — run a design through the kitchen and write a results CSV.
#
#   bake.sh <design.csv> [--all]
#
# Reads a design (the CSV that any `sample` or `generate` command prints),
# bakes every row with model.sh, and writes `run_id,taste` — or
# `run_id,taste,cost,minutes` with --all, which is what the multi-objective
# tools want.
#
# This exists so the tutorial can say "bake the design" in one line instead of
# a shell loop. In a real study it is where the thing that actually runs your
# experiment would go: a benchmark, a rig, an actual oven.
set -eu

design="${1:?usage: bake.sh <design.csv> [--all]}"
all="${2:-}"
here=$(dirname "$0")

header=$(head -1 "$design")
names=$(printf '%s' "$header" | cut -d, -f2-)
# Count fields, not newlines: `printf '%s' | wc -l` returns one FEWER than
# there are columns, because there is no trailing newline -- which silently
# dropped the last factor and left it at its default.
ncols=$(printf '%s\n' "$names" | awk -F, '{print NF}')

if [ "$all" = "--all" ]; then
    echo "run_id,taste,cost,minutes"
else
    echo "run_id,taste"
fi

tail -n +2 "$design" | while IFS= read -r line; do
    [ -z "$line" ] && continue
    id=$(printf '%s' "$line" | cut -d, -f1)
    vals=$(printf '%s' "$line" | cut -d, -f2-)

    # Each tool passes its factors under its own prefix (MORRIS_x, SOBOL_x,
    # ...), so set them all and let model.sh take whichever is present.
    i=1
    assignments=""
    while [ "$i" -le "$ncols" ]; do
        n=$(printf '%s' "$names" | cut -d, -f"$i")
        v=$(printf '%s' "$vals"  | cut -d, -f"$i")
        for p in MORRIS SOBOL TAGUCHI OFAT GRID; do
            assignments="$assignments ${p}_${n}=${v}"
        done
        i=$((i + 1))
    done

    # shellcheck disable=SC2086
    out=$(env $assignments "$here/model.sh" $all)
    printf '%s,%s\n' "$id" "$out"
done
