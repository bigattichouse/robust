#!/bin/sh
# A cookie, as arithmetic.
#
# Stands in for the oven so you can follow the whole tutorial in seconds.
# Every tool sets its factors as environment variables, so this script reads
# whichever prefix the calling tool uses (MORRIS_, SOBOL_, TAGUCHI_, ...).
#
# Three numbers come out: taste (higher better), cost (lower better) and
# minutes (lower better).
#
# The structure is deliberate, and it is what makes the tutorial teach:
#
#   * BROWNING is temperature x time, not temperature plus time. Hot-and-quick
#     browns like cool-and-slow. That is a real interaction, and it is why
#     `sobol` reports S_T > S_1 for both and `grid` has something to draw.
#
#   * Taste PEAKS at the right browning and falls away on both sides -- pale
#     dough and burnt dough are both bad. An interior optimum, which is the
#     only kind `rsm` can find and no linear method ever will.
#
#   * FLOUR is a genuine trade-off. More of it makes a duller cookie -- so on a
#     perfect oven, less is better and screening will say so. But a drier dough
#     also rides over the oven being wrong, where a wetter one tracks it. So
#     the recipe with the best AVERAGE is not the one that survives a bad oven,
#     and only a crossed design can tell you that. This is the entire point of
#     `taguchi robust`.
#
#   * EGGS barely matter. Something has to be droppable, or screening has
#     nothing to teach.
val() {
    # first non-empty of the tool-specific env vars, else the default
    for v in "$1" "$2" "$3" "$4" "$5"; do
        [ -n "$v" ] && { printf '%s' "$v"; return; }
    done
    printf '%s' "$6"
}

butter=$(val "$MORRIS_butter" "$SOBOL_butter" "$TAGUCHI_butter" "$OFAT_butter" "$GRID_butter" 0.75)
sugar=$(val  "$MORRIS_sugar_ratio" "$SOBOL_sugar_ratio" "$TAGUCHI_sugar_ratio" "$OFAT_sugar_ratio" "$GRID_sugar_ratio" 1.0)
flour=$(val  "$MORRIS_flour" "$SOBOL_flour" "$TAGUCHI_flour" "$OFAT_flour" "$GRID_flour" 2.25)
egg=$(val    "$MORRIS_egg" "$SOBOL_egg" "$TAGUCHI_egg" "$OFAT_egg" "$GRID_egg" 2)
chips=$(val  "$MORRIS_chips" "$SOBOL_chips" "$TAGUCHI_chips" "$OFAT_chips" "$GRID_chips" 1.0)
temp=$(val   "$MORRIS_temp" "$SOBOL_temp" "$TAGUCHI_temp" "$OFAT_temp" "$GRID_temp" 375)
time_=$(val  "$MORRIS_time" "$SOBOL_time" "$TAGUCHI_time" "$OFAT_time" "$GRID_time" 11)

# The oven's error. Zero unless a crossed design is exercising it.
offset=$(val "$TAGUCHI_oven" "$MORRIS_oven" "$SOBOL_oven" "" "" 0)

all=0
[ "$1" = "--all" ] && all=1

awk -v all="$all" -v butter="$butter" -v sugar="$sugar" -v flour="$flour" -v egg="$egg" \
    -v chips="$chips" -v temp="$temp" -v t="$time_" -v offset="$offset" '
BEGIN {
    # A drier dough rides over the oven being wrong; a wetter one tracks it.
    damp = 1.0 / (1.0 + 4.0 * (flour - 2.0));
    if (damp < 0.15) damp = 0.15;
    effective = temp + offset * damp;

    # Browning is the PRODUCT of heat and time, which is the interaction.
    browning = ((effective - 300.0) / 75.0) * (t / 11.0);

    # Burnt is worse than pale, and not by a little: an underbaked cookie is
    # disappointing, a burnt one goes in the bin. That asymmetry is why the
    # robust answer is to bake COOLER than the theoretical best -- the downside
    # of the oven running hot is far worse than the upside of it running cold.
    dev = browning - 1.0;
    if (dev > 0) taste = 100.0 - 115.0 * dev * dev;   # burnt
    else         taste = 100.0 -  42.0 * dev * dev;   # pale
    taste += 9.0 * (chips - 0.75);                 # more chocolate, better
    taste -= 26.0 * (butter - 0.75) * (butter - 0.75) / 0.0625;   # peaks at 3/4 cup
    taste -= 7.0 * (sugar - 1.0) * (sugar - 1.0);  # mild
    taste += 0.4 * (egg - 2.0);                    # nearly nothing
    taste -= 4.0 * (flour - 2.0);                  # drier dough, duller cookie
    # The scale runs 1..100, not 0..100: 1 is "inedible", and a genuine zero
    # would make a larger-the-better S/N ratio undefined (it divides by y).
    if (taste < 1) taste = 1;

    cost    = 3.10 * butter + 4.40 * chips + 0.95 * flour + 0.55 * egg + 0.30 * sugar;
    minutes = t + 6.0;

    # One bare number by default: that is what the tools read from stdout.
    # `--all` gives all three, for building a multi-metric results CSV.
    if (all == "1") printf "%.4f,%.4f,%.4f\n", taste, cost, minutes;
    else            printf "%.4f\n", taste;
}'
