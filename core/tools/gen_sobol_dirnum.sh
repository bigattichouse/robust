#!/usr/bin/env bash
#
# Regenerate core/src/sobol_dirnum.h from Joe & Kuo's published direction
# numbers. The generated header is committed, so this script is only needed to
# change the dimension cap or to re-derive the table from a fresh download.
#
#   sources/fetch.sh                       # downloads new-joe-kuo-6.21201
#   core/tools/gen_sobol_dirnum.sh \
#       sources/pdf/joe-kuo/new-joe-kuo-6.21201 1024 > core/src/sobol_dirnum.h
#
# Why 1024 dimensions and not more: `sobol` needs 2k dimensions for k factors
# (Saltelli 2010 Sec. 5.1 -- A is the left half, B the right half of one
# 2k-dimensional sequence), so 1024 dimensions supports 512 factors. The cap is
# set by quality, not by file size: Joe & Kuo state that Property A holds only
# up to dimension 1111 for this file, and `make validate` check G reproduces
# that independently -- Property A holds at 1111 and fails at 1112. Every
# dimension shipped here is inside that region. See sources/README.md.
#
# Input format (from the Joe-Kuo web page): one header line, then rows of
#   d  s  a  m_1 .. m_s
# where d is the dimension, s the degree of the primitive polynomial, a the
# integer encoding its coefficients, and m_i the initial direction numbers.
# The file starts at d = 2; dimension 1 is implicit (all m_i = 1) and is
# special-cased in sample.c rather than stored.
set -euo pipefail

src=${1:?usage: gen_sobol_dirnum.sh <direction-number-file> [max-dim]}
maxdim=${2:-1024}

[ -s "$src" ] || { echo "no such file: $src" >&2; exit 1; }

awk -v maxdim="$maxdim" -v src="$(basename "$src")" '
NR == 1 { next }                      # header: d s a m_i
$1 > maxdim { exit }
{
  n++
  d[n] = $1; s[n] = $2; a[n] = $3
  if ($2 > maxs) maxs = $2
  if ($3 > maxa) maxa = $3
  off[n] = total
  line = ""
  for (i = 4; i <= NF; i++) { m[++total] = $i; if ($i > maxm) maxm = $i }
  if (NF - 3 != $2) { print "row " $1 ": " NF-3 " m values, expected " $2 > "/dev/stderr"; exit 1 }
}
END {
  if (n + 1 != maxdim) { print "wanted " maxdim " dims, file gave " n+1 > "/dev/stderr"; exit 1 }
  if (maxs > 255)   { print "degree overflows uint8"  > "/dev/stderr"; exit 1 }
  if (maxa > 65535) { print "a overflows uint16"      > "/dev/stderr"; exit 1 }
  if (maxm > 65535) { print "m overflows uint16"      > "/dev/stderr"; exit 1 }

  print "/*"
  print " * sobol_dirnum.h -- Joe-Kuo direction numbers for the Sobol sequence."
  print " *"
  print " * GENERATED FILE -- do not edit. Regenerate with:"
  print " *   core/tools/gen_sobol_dirnum.sh \\"
  print " *       sources/pdf/joe-kuo/" src " " maxdim " > core/src/sobol_dirnum.h"
  print " *"
  print " * Data: " src ", the D(6) set from"
  print " *   S. Joe and F. Y. Kuo, \"Constructing Sobol sequences with better"
  print " *   two-dimensional projections\", SIAM J. Sci. Comput. 30, 2635-2654 (2008),"
  print " *   https://web.maths.unsw.edu.au/~fkuo/sobol/  (the authors recommended set)."
  print " *"
  print " * Dimensions 2.." maxdim " are tabulated; dimension 1 (all m_i = 1) is"
  print " * implicit and handled in sample.c. Degrees run to " maxs ", so a and m fit"
  print " * uint16 and the degree fits uint8."
  print " *"
  print " * ---------------------------------------------------------------------------"
  print " * Licence pertaining to sobol.cc and the accompanying sets of direction numbers"
  print " * ---------------------------------------------------------------------------"
  print " * Copyright (c) 2008, Frances Y. Kuo and Stephen Joe"
  print " * All rights reserved."
  print " *"
  print " * Redistribution and use in source and binary forms, with or without"
  print " * modification, are permitted provided that the following conditions are met:"
  print " *"
  print " *     * Redistributions of source code must retain the above copyright"
  print " *       notice, this list of conditions and the following disclaimer."
  print " *"
  print " *     * Redistributions in binary form must reproduce the above copyright"
  print " *       notice, this list of conditions and the following disclaimer in the"
  print " *       documentation and/or other materials provided with the distribution."
  print " *"
  print " *     * Neither the names of the copyright holders nor the names of the"
  print " *       University of New South Wales and the University of Waikato"
  print " *       and its contributors may be used to endorse or promote products derived"
  print " *       from this software without specific prior written permission."
  print " *"
  print " * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'\'' AND ANY"
  print " * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED"
  print " * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE"
  print " * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY"
  print " * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES"
  print " * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;"
  print " * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND"
  print " * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT"
  print " * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS"
  print " * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
  print " * ---------------------------------------------------------------------------"
  print " */"
  print ""
  print "#ifndef DOE_SOBOL_DIRNUM_H"
  print "#define DOE_SOBOL_DIRNUM_H"
  print ""
  print "#include <stdint.h>"
  print ""
  print "/* Highest dimension this table covers. sample.c static-asserts that it"
  print " * equals DOE_SOBOL_MAX_DIM in doe.h, so regenerating with a different cap"
  print " * without updating the public header is a build error, not a silent"
  print " * disagreement between the table and the limit the tools enforce. */"
  print "#define DOE_SOBOL_TABLE_DIM " maxdim
  print ""
  print "/* Entry i describes dimension i + 2. */"
  printf "static const uint8_t doe_sobol_s[%d] = {\n", n
  emit(s, n, 8, "%u")
  print "};"
  print ""
  printf "static const uint16_t doe_sobol_a[%d] = {\n", n
  emit(a, n, 8, "%u")
  print "};"
  print ""
  print "/* Entry i is where dimension i + 2 starts in doe_sobol_m. */"
  printf "static const uint32_t doe_sobol_m_off[%d] = {\n", n
  emit(off, n, 8, "%u")
  print "};"
  print ""
  printf "/* Initial direction numbers, concatenated: %d values. */\n", total
  printf "static const uint16_t doe_sobol_m[%d] = {\n", total
  emit(m, total, 12, "%u")
  print "};"
  print ""
  print "#endif /* DOE_SOBOL_DIRNUM_H */"
}

function emit(arr, cnt, per, fmt,    i, line) {
  line = "   "
  for (i = 1; i <= cnt; i++) {
    line = line sprintf(" %5u,", arr[i])
    if (i % per == 0) { print line; line = "   " }
  }
  if (line != "   ") print line
}
' "$src"
