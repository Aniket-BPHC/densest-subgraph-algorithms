#!/usr/bin/env bash

set -e

DATASETS=("wiki-Vote.txt.gz" "email-Enron.txt.gz" "as-skitter.txt.gz")
SNAP_URLS=(
    "https://snap.stanford.edu/data/wiki-Vote.txt.gz"
    "https://snap.stanford.edu/data/email-Enron.txt.gz"
    "https://snap.stanford.edu/data/as-skitter.txt.gz"
)

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

info "Checking dependencies..."
for cmd in g++ wget; do
    if ! command -v "$cmd" &>/dev/null; then
        error "'$cmd' not found. Install it and re-run."
        exit 1
    fi
done

if ! echo '#include <zlib.h>' | g++ -x c++ - -lz -o /dev/null 2>/dev/null; then
    error "zlib not found. Run: sudo apt install zlib1g-dev"
    exit 1
fi
info "Dependencies OK."

info "Checking datasets..."
for i in "${!DATASETS[@]}"; do
    f="${DATASETS[$i]}"
    if [[ ! -f "$f" ]]; then
        info "Downloading $f from SNAP..."
        wget -q --show-progress "${SNAP_URLS[$i]}" -O "$f"
    else
        info "$f already present, skipping download."
    fi
done

info "Compiling Algorithm 1 (Charikar Greedy)..."
g++ -O3 -std=c++17 algorithm_a.cpp -o algorithm_a -lz
info "Compiling Algorithm 2 (Greedy++)..."
g++ -O3 -std=c++17 algorithm_b.cpp -o algorithm_b -lz
info "Compiling Algorithm 1 Paper 2 (Exact Triangle DSP)..."
g++ -O3 -std=c++17 final_dense-subgraph_algo1.cpp -o dense-subgraph_algo1 -lz
info "Compiling Algorithm 4 (CoreExact)..."
g++ -O3 -std=c++17 dense-subgraph_4.cpp -o dense-subgraph_4 -lz
info "Compilation complete."

echo ""
echo "  ALGORITHM 1 – Charikar Greedy DSP"
./algorithm_a

echo ""
echo "  ALGORITHM 2 – Greedy++ DSP"
./algorithm_b

echo ""
echo "  ALGORITHM 1 (Paper 2) – Exact Triangle DSP"

for dataset in "${DATASETS[@]}"; do
    if [[ "$dataset" == "as-skitter.txt.gz" ]]; then
        warn "Skipping as-skitter for Exact algorithm — requires >1 GB RAM and may be killed by OS."
        warn "To run manually: ./dense-subgraph_algo1 as-skitter.txt.gz"
        continue
    fi
    info "Running Exact DSP on $dataset..."
    ./dense-subgraph_algo1 "$dataset"
done

echo ""
echo "  ALGORITHM 4 – CoreExact"
for dataset in "${DATASETS[@]}"; do
    if [[ "$dataset" == "as-skitter.txt.gz" ]]; then
        warn "as-skitter with CoreExact takes ~50 min and ~1.7 GB RAM."
        warn "To run manually: ./dense-subgraph_4 as-skitter.txt.gz"
        read -r -p "Run as-skitter with CoreExact now? [y/N] " ans
        [[ "$ans" =~ ^[Yy]$ ]] || { info "Skipped as-skitter CoreExact."; continue; }
    fi
    info "Running CoreExact on $dataset..."
    ./dense-subgraph_4 "$dataset"
done

echo ""
info "All algorithms completed."
