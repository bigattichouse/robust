#!/usr/bin/env bash
# Re-download the reference PDFs indexed in README.md. Skips what is present.
# PDFs are large and disposable; this script is the reproducible substitute.
set -u
cd "$(dirname "$0")"
mkdir -p pdf

ARXIV=(
  2202.02643  # The Unreasonable Effectiveness of Random Pruning (ICLR 2022)
  2310.05175  # OWL -- outlier-weighed layerwise sparsity (ICLR 2024)
  2402.05406  # Bonsai -- structured pruning, forward passes only
  1804.08838  # Measuring the Intrinsic Dimension of Objective Landscapes
  2606.15161  # inter-layer perturbation absorption
)

get() {
  local out="pdf/$1" url="$2"
  [ -s "$out" ] && { echo "have   $1"; return; }
  if curl -sfL --max-time 120 -A "research-refs/1.0 (personal archive)" \
       -o "$out" "$url" && [ "$(file -b --mime-type "$out")" = application/pdf ]
  then echo "got    $1"; else rm -f "$out"; echo "FAILED $1  ($url)"; fi
  sleep 2
}

for id in "${ARXIV[@]}"; do get "arxiv-$id.pdf" "https://arxiv.org/pdf/$id"; done
get campolongo-2007-morris-screening.pdf \
    "https://www.asc.ohio-state.edu/statistics/comp_exp/jour.club/CamCarSal_EngModellingSoftware-2007.pdf"

# ---- Joe-Kuo Sobol' direction numbers -------------------------------------
# These are DATA, not a paper, and unlike the PDFs above they are BSD-licensed
# and redistributable -- core/src/sobol_dirnum.h is a generated slice of the
# first file, with the copyright notice retained. They are fetched here anyway
# because three things need the originals rather than the slice:
#   - regenerating the table  (core/tools/gen_sobol_dirnum.sh)
#   - `make validate` check H, whose Property A boundary at dimension 1112 is
#     past the 1024 dimensions we ship, so it SKIPS without the full file
#   - re-deriving the reference vectors in core/tests/test_doe.c from the
#     authors' own sobol.cc rather than from our implementation
JK=https://web.maths.unsw.edu.au/~fkuo/sobol
mkdir -p pdf/joe-kuo
jk() {
  local out="pdf/joe-kuo/$1"
  [ -s "$out" ] && { echo "have   joe-kuo/$1"; return; }
  if curl -sfL --max-time 300 -A "research-refs/1.0 (personal archive)" \
       -o "$out" "$JK/$1"
  then echo "got    joe-kuo/$1"; else rm -f "$out"; echo "FAILED joe-kuo/$1"; fi
  sleep 2
}
jk licence               # BSD-3-clause covering sobol.cc and the direction numbers
jk sobol.cc              # the authors' reference generator — our ground truth
jk new-joe-kuo-6.21201   # the D(6) set, their recommended choice (1.8 MB)
get joe-kuo-notes.pdf "$JK/joe-kuo-notes.pdf"   # 3-page derivation of the algorithm

echo
echo "Paywalled, cannot be fetched here:"
echo "  saltelli-2010-total-index-estimator.pdf  -- supplied manually; if it is"
echo "     missing from pdf/, obtain it via institutional access. It is the"
echo "     estimator the sobol tool implements."
echo "  Bettonvil & Kleijnen (sequential bifurcation, both papers) -- no local"
echo "     copy; the method was superseded, so this is no longer blocking."
echo "See README.md."
