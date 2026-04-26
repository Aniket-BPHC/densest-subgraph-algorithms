# =============================================================================
#  run.ps1  -  CS F464 Assignment 2, Team 24
#  Densest Subgraph Problem: compile and run all four algorithms
#  Run from the directory containing all .cpp source files and datasets.
# =============================================================================

$datasets = @("wiki-Vote.txt.gz", "email-Enron.txt.gz", "as-skitter.txt.gz")
$snapUrls = @(
    "https://snap.stanford.edu/data/wiki-Vote.txt.gz",
    "https://snap.stanford.edu/data/email-Enron.txt.gz",
    "https://snap.stanford.edu/data/as-skitter.txt.gz"
)

function Info  { param($msg) Write-Host "[INFO]  $msg" -ForegroundColor Green }
function Warn  { param($msg) Write-Host "[WARN]  $msg" -ForegroundColor Yellow }
function Err   { param($msg) Write-Host "[ERROR] $msg" -ForegroundColor Red }

# ── Step 1: Check g++ ────────────────────────────────────────────────────────
Info "Checking for g++..."
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Err "g++ not found. Install MinGW-w64 or MSYS2 and add to PATH."
    exit 1
}
Info "g++ found."

# ── Step 2: Download datasets if missing ─────────────────────────────────────
Info "Checking datasets..."
for ($i = 0; $i -lt $datasets.Length; $i++) {
    $f = $datasets[$i]
    if (-not (Test-Path $f)) {
        Info "Downloading $f from SNAP..."
        Invoke-WebRequest -Uri $snapUrls[$i] -OutFile $f -UseBasicParsing
        Info "$f downloaded."
    } else {
        Info "$f already present, skipping download."
    }
}

# ── Step 3: Compile ───────────────────────────────────────────────────────────
Info "Compiling Algorithm 1 (Charikar Greedy)..."
g++ -O3 -std=c++17 algorithm_a.cpp -o algorithm_a.exe -lpsapi -lz
if ($LASTEXITCODE -ne 0) { Err "Compilation of algorithm_a failed."; exit 1 }

Info "Compiling Algorithm 2 (Greedy++)..."
g++ -O3 -std=c++17 algorithm_b.cpp -o algorithm_b.exe -lpsapi -lz
if ($LASTEXITCODE -ne 0) { Err "Compilation of algorithm_b failed."; exit 1 }

Info "Compiling Algorithm 1 Paper 2 (Exact Triangle DSP)..."
g++ -O3 -std=c++17 final_dense-subgraph_algo1.cpp -o dense-subgraph_algo1.exe -lpsapi -lz
if ($LASTEXITCODE -ne 0) { Err "Compilation of dense-subgraph_algo1 failed."; exit 1 }

Info "Compiling Algorithm 4 (CoreExact)..."
g++ -O3 -std=c++17 dense-subgraph_4.cpp -o dense-subgraph_4.exe -lpsapi -lz
if ($LASTEXITCODE -ne 0) { Err "Compilation of dense-subgraph_4 failed."; exit 1 }

Info "Compilation complete."

# ── Step 4: Run ───────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "============================================================"
Write-Host "  ALGORITHM 1 - Charikar Greedy DSP (Boob et al. WWW'20)"
Write-Host "============================================================"
.\algorithm_a.exe

Write-Host ""
Write-Host "============================================================"
Write-Host "  ALGORITHM 2 - Greedy++ DSP (Boob et al. WWW'20)"
Write-Host "============================================================"
.\algorithm_b.exe

Write-Host ""
Write-Host "============================================================"
Write-Host "  ALGORITHM 1 (Paper 2) - Exact Triangle DSP (Fang et al. PVLDB'19)"
Write-Host "============================================================"
foreach ($dataset in $datasets) {
    if ($dataset -eq "as-skitter.txt.gz") {
        Warn "Skipping as-skitter for Exact algorithm - requires >1 GB RAM and may be OOM-killed."
        Warn "To run manually: .\dense-subgraph_algo1.exe as-skitter.txt.gz"
        continue
    }
    Info "Running Exact DSP on $dataset..."
    .\dense-subgraph_algo1.exe $dataset
}

Write-Host ""
Write-Host "============================================================"
Write-Host "  ALGORITHM 4 - CoreExact (Fang et al. PVLDB'19)"
Write-Host "============================================================"
foreach ($dataset in $datasets) {
    if ($dataset -eq "as-skitter.txt.gz") {
        Warn "as-skitter with CoreExact takes ~50 min and ~1.7 GB RAM."
        Warn "To run manually: .\dense-subgraph_4.exe as-skitter.txt.gz"
        $ans = Read-Host "Run as-skitter with CoreExact now? [y/N]"
        if ($ans -notmatch '^[Yy]$') { Info "Skipped as-skitter CoreExact."; continue }
    }
    Info "Running CoreExact on $dataset..."
    .\dense-subgraph_4.exe $dataset
}

Write-Host ""
Info "All algorithms completed."
