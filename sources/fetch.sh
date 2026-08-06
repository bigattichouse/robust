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

echo
echo "Paywalled, cannot be fetched here:"
echo "  saltelli-2010-total-index-estimator.pdf  -- supplied manually; if it is"
echo "     missing from pdf/, obtain it via institutional access. It is the"
echo "     estimator the sobol tool implements."
echo "  Bettonvil & Kleijnen (sequential bifurcation, both papers) -- no local"
echo "     copy; the method was superseded, so this is no longer blocking."
echo "See README.md."
