/**
 * algorithm_b.cpp  —  Greedy++ Densest Subgraph + Betweenness Centrality
 * =========================================================================
 * Implements Algorithm 2 (Greedy++) from:
 *   "Flowless: Extracting Densest Subgraphs Without Flow Computations"
 *    Boob et al., WWW '20
 *
 * -------------------------------------------------------------------------
 * BUILD — Linux / macOS
 * -------------------------------------------------------------------------
 *   g++ -O2 -std=c++17 -o algorithm_b algorithm_b.cpp -lz
 *   ./algorithm_b [num_iterations]          # default T=10
 *
 * -------------------------------------------------------------------------
 * BUILD — Windows  (three options, pick one)
 * -------------------------------------------------------------------------
 *  Option A — MSVC + vcpkg (recommended)
 *    1. Install vcpkg and zlib:
 *         git clone https://github.com/microsoft/vcpkg
 *         .\vcpkg\bootstrap-vcpkg.bat
 *         .\vcpkg\vcpkg install zlib:x64-windows
 *    2. Open "x64 Native Tools Command Prompt for VS":
 *         cl /O2 /std:c++17 /EHsc algorithm_b.cpp ^
 *            /I<vcpkg>\installed\x64-windows\include ^
 *            /link <vcpkg>\installed\x64-windows\lib\zlib.lib ^
 *            Psapi.lib /OUT:algorithm_b.exe
 *    3. Copy <vcpkg>\installed\x64-windows\bin\zlib1.dll next to the .exe
 *
 *  Option B — MinGW-w64 / MSYS2
 *    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-zlib   (once)
 *    g++ -O2 -std=c++17 -o algorithm_b.exe algorithm_b.cpp -lz
 *
 *  Option C — WSL2 (Ubuntu inside Windows)
 *    sudo apt install g++ zlib1g-dev
 *    g++ -O2 -std=c++17 -o algorithm_b algorithm_b.cpp -lz
 *    ./algorithm_b
 *
 * -------------------------------------------------------------------------
 * Input (.txt.gz)
 * -------------------------------------------------------------------------
 *   Lines starting with '#' are skipped.
 *   Every other line: <u> <v>  (whitespace-separated integer node IDs)
 *   Self-loops and duplicate edges are ignored.
 *   Node IDs need not be contiguous — they are remapped internally.
 *   Place the three dataset files in the same folder as the binary.
 *
 * -------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------
 *   1. Densest subgraph per iteration: node count + density rho(S) = e[S]/|S|
 *   2. Final best densest subgraph across all iterations
 *   3. Top-20 nodes in S ranked by Betweenness Centrality
 *   4. Execution time breakdown (load / greedy++ / BC)
 *   5. Memory (RSS) at each phase
 *
 * -------------------------------------------------------------------------
 * Algorithm guarantee
 * -------------------------------------------------------------------------
 *   rho(output) >= rho* / 2   (at least 1/2-approximation, improves with T)
 *   Conjectured: (1 + O(1/sqrt(T)))-approximation
 *   Time per iteration:  O((n+m) log n)
 *   Space: O(n+m)
 *
 * Key difference from Algorithm 1 (Charikar greedy):
 *   Each node carries a cumulative load l[v] across iterations.
 *   Removal order in iteration i is by (l[v] + current_degree), not just
 *   current_degree alone.  After removal, l[v] is incremented by the degree
 *   it had at removal time.  This load-balancing drives subsequent passes
 *   to peel a different node sequence, yielding higher-quality subgraphs.
 */

// ============================================================
//  Includes
// ============================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include <utility>
#include <chrono>
#include <iomanip>
#include <random>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <filesystem>   // C++17 — exe-relative path resolution

#include <zlib.h>       // Linux: -lz  |  Windows: zlib via vcpkg or MinGW

// ---- Platform-specific headers ------------------------------------------
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>  // GetModuleFileNameW, GetProcessMemoryInfo
#  include <psapi.h>    // PROCESS_MEMORY_COUNTERS  (link: Psapi.lib)
#else
#  include <sys/resource.h>   // getrusage
#  include <unistd.h>         // readlink  (Linux exe path)
#  ifdef __APPLE__
#    include <mach-o/dyld.h>  // _NSGetExecutablePath
#  endif
#endif

using namespace std;
namespace fs = std::filesystem;
using Clock = chrono::high_resolution_clock;

// ============================================================
//  Result  — shared return type for the DSP algorithm
// ============================================================
struct Result {
    vector<int> vertices;   // node IDs (0-based, contiguous) in densest subgraph
    double      density;    // e[S] / |S|
    int         best_iter;  // iteration that produced this density
};

// ============================================================
//  SECTION 1 — Greedy++ Peeling  (Algorithm 2, Boob et al.)
// ============================================================
//
// Runs T weighted peeling passes.  In each pass the removal key for
// node u is  l[u] + deg_H(u)  where l[u] is the *cumulative* load
// accumulated across all previous passes.  After u is removed in
// pass i, its load is updated:  l[u] += deg_H(u)  (the degree at
// the moment of removal, matching line 7 of Algorithm 2).
//
// Implementation details:
//   • Lazy min-heap on (load + current_degree, vertex).
//   • Stale entries are skipped on pop (degree or load changed).
//   • best node set is recovered in O(n) at the end via removal_order,
//     without storing a snapshot at every improvement step.
//   • The load vector l[] persists across iterations; everything else
//     (adj copy, deg, active flags) is reset per iteration.
//
Result greedy_pp(int n, const vector<pair<int,int>>& edges, int T) {

    // --- Build adjacency list -------------------------------------------
    vector<vector<int>> adj(n);
    for (auto& [u, v] : edges) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    int m = (int)edges.size();

    // --- Persistent load vector (Algorithm 2, line 2) -------------------
    // l[v] accumulates the degree of v at the time it was removed,
    // summed over all completed passes.
    vector<double> l(n, 0.0);

    // --- Tracking the global best across all iterations -----------------
    double global_best_density = (n > 0) ? (double)m / n : 0.0;
    int    global_best_ac      = n;   // active_count at the best snapshot
    int    global_best_iter    = 0;
    vector<int> global_removal_order(n);
    iota(global_removal_order.begin(), global_removal_order.end(), 0);

    // --- T passes (outer loop, Algorithm 2, line 3) ---------------------
    for (int iter = 1; iter <= T; ++iter) {

        // Reset per-pass state
        vector<int>  deg(n);
        vector<bool> active(n, true);
        for (int v = 0; v < n; ++v)
            deg[v] = (int)adj[v].size();

        int    active_count = n;
        int    edge_count   = m;
        double best_density = (n > 0) ? (double)m / n : 0.0;
        int    best_ac      = n;

        vector<int> removal_order;
        removal_order.reserve(n);

        // Lazy min-heap on (l[v] + deg[v], v) ----------------------------
        // Key type is double because l[] is a double after being updated
        // as a sum of integer degrees.  We compare with a small epsilon
        // tolerance when skipping stale entries.
        using pdi = pair<double, int>;
        priority_queue<pdi, vector<pdi>, greater<pdi>> pq;
        for (int v = 0; v < n; ++v)
            pq.push({l[v] + deg[v], v});

        // --- Peeling loop (Algorithm 2, lines 5–12) ----------------------
        while (active_count > 0) {
            // Skip stale heap entries: a heap entry (key, v) is stale if
            // the stored key no longer matches l[v] + deg[v].
            while (!pq.empty()) {
                auto [key, v] = pq.top();
                if (active[v] && fabs(key - (l[v] + deg[v])) < 1e-9) break;
                pq.pop();
            }
            if (pq.empty()) break;

            auto [key, u] = pq.top(); pq.pop();

            // Remove u from H (Algorithm 2, line 8)
            active[u]  = false;
            edge_count -= deg[u];   // deg[u] edges incident to u remain in H
            --active_count;
            removal_order.push_back(u);

            // Update load: l[u] += deg_H(u)  (Algorithm 2, line 7)
            // Note: deg[u] currently holds u's degree in H at this moment.
            l[u] += deg[u];

            // Update neighbours' degrees and push updated keys
            for (int nb : adj[u]) {
                if (active[nb]) {
                    --deg[nb];
                    pq.push({l[nb] + deg[nb], nb});
                }
            }

            // Evaluate density of H \ {u}  (Algorithm 2, line 9)
            if (active_count > 0) {
                double rho = (double)edge_count / active_count;
                if (rho > best_density) {
                    best_density = rho;
                    best_ac      = active_count;
                }
            }
        }

        // Track global best across all iterations
        if (best_density > global_best_density) {
            global_best_density  = best_density;
            global_best_ac       = best_ac;
            global_best_iter     = iter;
            global_removal_order = removal_order;
        }

        cout << "  Iter " << setw(3) << iter
             << "  |  density = " << fixed << setprecision(6) << best_density
             << "  |  |S| = " << best_ac << "\n";
    }

    // --- Materialise best node set ---------------------------------------
    // The best_ac nodes that survived longest are the LAST best_ac entries
    // in the removal_order of the iteration that produced the best density.
    Result result;
    result.density   = global_best_density;
    result.best_iter = global_best_iter;

    if (global_best_ac == n) {
        result.vertices.resize(n);
        iota(result.vertices.begin(), result.vertices.end(), 0);
    } else {
        int start = (int)global_removal_order.size() - global_best_ac;
        result.vertices.assign(global_removal_order.begin() + start,
                               global_removal_order.end());
    }
    return result;
}

// ============================================================
//  SECTION 2 — Betweenness Centrality
// ============================================================

// Brandes' exact O(V·E) algorithm — used when |S| <= EXACT_THRESHOLD
vector<double> brandes_bc(int sz, const vector<vector<int>>& sub_adj) {
    vector<double> bc(sz, 0.0);
    vector<double> delta(sz);
    vector<int>    sigma(sz), dist(sz);
    vector<vector<int>> pred(sz);

    for (int s = 0; s < sz; ++s) {
        fill(sigma.begin(), sigma.end(), 0);
        fill(dist.begin(),  dist.end(),  -1);
        for (auto& p : pred) p.clear();
        sigma[s] = 1; dist[s] = 0;

        stack<int> stk;
        queue<int> q;
        q.push(s);

        while (!q.empty()) {
            int v = q.front(); q.pop();
            stk.push(v);
            for (int w : sub_adj[v]) {
                if (dist[w] < 0) { dist[w] = dist[v] + 1; q.push(w); }
                if (dist[w] == dist[v] + 1) { sigma[w] += sigma[v]; pred[w].push_back(v); }
            }
        }

        fill(delta.begin(), delta.end(), 0.0);
        while (!stk.empty()) {
            int w = stk.top(); stk.pop();
            for (int v : pred[w])
                if (sigma[w] > 0)
                    delta[v] += (double)sigma[v] / sigma[w] * (1.0 + delta[w]);
            if (w != s) bc[w] += delta[w];
        }
    }
    return bc;
}

// Random-pivot approximation — used when |S| > EXACT_THRESHOLD
// Samples `num_pivots` random sources; scores scaled by |V|/pivots
vector<double> approx_bc(int sz, const vector<vector<int>>& sub_adj,
                          int num_pivots, unsigned seed = 42) {
    vector<double> bc(sz, 0.0);
    vector<double> delta(sz);
    vector<int>    sigma(sz), dist(sz);
    vector<vector<int>> pred(sz);

    mt19937 rng(seed);
    uniform_int_distribution<int> uni(0, sz - 1);
    double scale = (double)sz / num_pivots;

    for (int iter = 0; iter < num_pivots; ++iter) {
        int s = uni(rng);
        fill(sigma.begin(), sigma.end(), 0);
        fill(dist.begin(),  dist.end(),  -1);
        for (auto& p : pred) p.clear();
        sigma[s] = 1; dist[s] = 0;

        stack<int> stk;
        queue<int> q;
        q.push(s);

        while (!q.empty()) {
            int v = q.front(); q.pop();
            stk.push(v);
            for (int w : sub_adj[v]) {
                if (dist[w] < 0) { dist[w] = dist[v] + 1; q.push(w); }
                if (dist[w] == dist[v] + 1) { sigma[w] += sigma[v]; pred[w].push_back(v); }
            }
        }

        fill(delta.begin(), delta.end(), 0.0);
        while (!stk.empty()) {
            int w = stk.top(); stk.pop();
            for (int v : pred[w])
                if (sigma[w] > 0)
                    delta[v] += (double)sigma[v] / sigma[w] * (1.0 + delta[w]);
            if (w != s) bc[w] += delta[w] * scale;
        }
    }
    return bc;
}

// ============================================================
//  SECTION 3 — I/O  (gz reader, graph parser, memory, timing)
// ============================================================

// --- Timing --------------------------------------------------------------
struct Timer {
    chrono::time_point<Clock> t0 = Clock::now();
    void   reset()            { t0 = Clock::now(); }
    double elapsed_ms() const {
        return chrono::duration<double, milli>(Clock::now() - t0).count();
    }
};

// --- RSS memory (KB) — Windows, Linux, macOS -----------------------------
long get_rss_kb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<long>(pmc.WorkingSetSize / 1024);
    return -1L;
#elif defined(__linux__)
    // /proc/self/status is reliable on all Linux kernels
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1L;
    char line[256]; long kb = -1L;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line + 6, " %ld", &kb); break; }
    fclose(f);
    return kb;
#elif defined(__APPLE__)
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss / 1024;   // macOS reports bytes, divide to get KB
#else
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;          // BSD reports KB directly
#endif
}

static string kb_str(long kb) {
    if (kb < 0)    return "N/A";
    if (kb < 1024) return to_string(kb) + " KB";
    return to_string(kb / 1024) + "." + to_string((kb % 1024) * 10 / 1024) + " MB";
}

// --- Directory containing the running executable -------------------------
// Used so the binary can find the .gz files placed beside it, regardless
// of the working directory the user launched it from.
static fs::path exe_dir() {
#if defined(_WIN32)
    // Windows: use wide-char API to handle Unicode paths correctly
    wchar_t buf[32768] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0) return fs::current_path();
    return fs::path(wstring(buf, len)).parent_path();
#elif defined(__linux__)
    // Linux: read the /proc/self/exe symlink
    char buf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return fs::current_path();
    buf[n] = '\0';
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    // macOS: use the dyld API
    uint32_t sz = 0;
    _NSGetExecutablePath(nullptr, &sz);
    string buf(sz, '\0');
    if (_NSGetExecutablePath(buf.data(), &sz) != 0) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

// --- .gz reader ----------------------------------------------------------
// Uses zlib's gzFile API (identical on Windows and Linux).
// On Windows: link against zlib.lib (vcpkg) or libz.a (MinGW).
static string read_gz(const fs::path& path) {
    // gzopen on Windows accepts UTF-8 paths via gzopen_w alternative,
    // but the portable approach is to pass the narrow string.  For paths
    // with non-ASCII characters on Windows, use gzopen_w instead.
#ifdef _WIN32
    gzFile gz = gzopen_w(path.wstring().c_str(), "rb");
#else
    gzFile gz = gzopen(path.string().c_str(), "rb");
#endif
    if (!gz) {
        fprintf(stderr, "  Error: cannot open '%s'\n", path.string().c_str());
        return "";
    }
    string out;
    out.reserve(1 << 23);   // 8 MB initial reservation
    char buf[65536];
    int  nr;
    while ((nr = gzread(gz, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(nr));
    gzclose(gz);
    return out;
}

// --- Graph parser --------------------------------------------------------
// Remaps arbitrary integer node IDs to contiguous [0, n).
struct GraphData {
    int              n;            // vertex count
    vector<pair<int,int>> edges;   // deduplicated edge list (contiguous IDs)
    vector<int>      original_id;  // original_id[new_id] = original label
};

static GraphData parse_graph(const string& text) {
    unordered_map<int,int> remap; remap.reserve(1 << 17);
    vector<pair<int,int>>  raw;   raw.reserve(1 << 18);

    auto get_id = [&](int orig) -> int {
        auto it = remap.find(orig);
        if (it != remap.end()) return it->second;
        int id = (int)remap.size(); remap[orig] = id; return id;
    };

    const char* p   = text.c_str();
    const char* end = p + text.size();

    while (p < end) {
        if (*p == '#' || *p == '\n' || *p == '\r') {
            while (p < end && *p != '\n') ++p;
            if (p < end) ++p;
            continue;
        }

        auto skip_ws  = [&]{ while (p<end && (*p==' '||*p=='\t')) ++p; };
        auto read_int = [&]() -> long long {
            skip_ws();
            if (p>=end || *p=='\n' || *p=='\r') return -1;
            long long v=0; bool neg=false;
            if (*p=='-'){neg=true;++p;}
            while (p<end && *p>='0' && *p<='9') v=v*10+(*p++)-'0';
            return neg?-v:v;
        };

        long long a = read_int(), b = read_int();
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;

        if (a < 0 || b < 0 || a == b) continue;

        int u = get_id((int)a), v = get_id((int)b);
        if (u > v) swap(u, v);
        raw.push_back({u, v});
    }

    sort(raw.begin(), raw.end());
    raw.erase(unique(raw.begin(), raw.end()), raw.end());

    int n = (int)remap.size();
    vector<int> orig(n);
    for (auto& [lbl, id] : remap) orig[id] = lbl;

    return {n, move(raw), move(orig)};
}

// --- Print rule ----------------------------------------------------------
static void rule(char c='-', int w=70) { cout << string(w,c) << "\n"; }

// ============================================================
//  run_dataset  — full pipeline for one .txt.gz file
// ============================================================
static void run_dataset(const fs::path& gz_path, int T) {

    cout << "\n"; rule('=');
    cout << "  Greedy++ DSP  (Algorithm 2, Boob et al. WWW'20)\n";
    cout << "  File       : " << gz_path.string() << "\n";
    cout << "  Iterations : " << T << "\n";
    rule('=');

    long rss_start = get_rss_kb();
    Timer tmr;

    // ---- 1. Load & parse ------------------------------------------------
    tmr.reset();
    string text = read_gz(gz_path);
    if (text.empty()) {
        cerr << "  [SKIPPED — could not read file]\n";
        return;
    }
    GraphData G = parse_graph(text);
    text.clear(); text.shrink_to_fit();
    double t_load = tmr.elapsed_ms();
    long rss_load = get_rss_kb();

    int n = G.n, m = (int)G.edges.size();
    cout << "\n[Graph]\n";
    cout << "  Vertices : " << n << "\n";
    cout << "  Edges    : " << m << "\n";

    // ---- 2. Greedy++ DSP ------------------------------------------------
    cout << "\n[Greedy++ iterations]\n";
    tmr.reset();
    Result dsp = greedy_pp(n, G.edges, T);
    double t_greedy = tmr.elapsed_ms();
    long rss_greedy = get_rss_kb();

    int ds = (int)dsp.vertices.size();
    cout << "\n[Densest Subgraph  —  best over " << T << " iterations]\n";
    cout << fixed << setprecision(6);
    cout << "  Best iteration : " << dsp.best_iter << "\n";
    cout << "  Nodes in S     : " << ds << "\n";
    cout << "  Density rho(S) : " << dsp.density << "\n";

    // ---- 3. Build subgraph adjacency list for BC ------------------------
    unordered_map<int,int> local_id; local_id.reserve(ds * 2);
    for (int i = 0; i < ds; ++i) local_id[dsp.vertices[i]] = i;

    unordered_set<int> in_S(dsp.vertices.begin(), dsp.vertices.end());
    vector<vector<int>> sub_adj(ds);
    for (auto& [u, v] : G.edges) {
        if (in_S.count(u) && in_S.count(v)) {
            int lu = local_id[u], lv = local_id[v];
            sub_adj[lu].push_back(lv);
            sub_adj[lv].push_back(lu);
        }
    }

    // ---- 4. Betweenness Centrality --------------------------------------
    const int EXACT_THRESHOLD = 2000;
    const int NUM_PIVOTS      = 512;
    bool exact = (ds <= EXACT_THRESHOLD);

    tmr.reset();
    vector<double> bc = exact ? brandes_bc(ds, sub_adj)
                               : approx_bc(ds, sub_adj, NUM_PIVOTS);
    double t_bc    = tmr.elapsed_ms();
    long rss_final = get_rss_kb();

    // ---- 5. Top-20 by BC ------------------------------------------------
    vector<int> rank_order(ds);
    iota(rank_order.begin(), rank_order.end(), 0);
    sort(rank_order.begin(), rank_order.end(),
         [&](int a, int b){ return bc[a] > bc[b]; });

    int top_k = min(20, ds);

    cout << "\n"; rule();
    cout << "  Top-" << top_k << " nodes by Betweenness Centrality";
    cout << (exact ? " (exact — Brandes O(VE))"
                   : " (approx — " + to_string(NUM_PIVOTS) + " random pivots)") << "\n";
    rule();
    cout << left
         << setw(6)  << "Rank"
         << setw(14) << "Node (orig)"
         << setw(14) << "Node (intern)"
         << setw(22) << "BC Score"
         << "Degree-in-S\n";
    rule();
    cout << fixed << setprecision(4);
    for (int r = 0; r < top_k; ++r) {
        int lid  = rank_order[r];
        int gid  = dsp.vertices[lid];
        int orig = G.original_id[gid];
        int deg  = (int)sub_adj[lid].size();
        cout << left
             << setw(6)  << (r+1)
             << setw(14) << orig
             << setw(14) << gid
             << setw(22) << bc[lid]
             << deg << "\n";
    }

    // ---- 6. Timing ------------------------------------------------------
    cout << "\n"; rule();
    cout << "  Execution Time\n"; rule();
    cout << fixed << setprecision(3);
    cout << "  Graph load          : " << setw(10) << t_load                    << " ms\n";
    cout << "  Greedy++ (" << setw(2) << T << " iters) : " << setw(10) << t_greedy << " ms\n";
    cout << "  Betweenness Central.: " << setw(10) << t_bc                      << " ms\n";
    cout << "  Total               : " << setw(10) << t_load+t_greedy+t_bc      << " ms\n";

    // ---- 7. Memory ------------------------------------------------------
    cout << "\n"; rule();
    cout << "  Memory (RSS)\n"; rule();
    cout << "  Baseline (pre-load) : " << kb_str(rss_start)            << "\n";
    cout << "  After graph load    : " << kb_str(rss_load)              << "\n";
    cout << "  After Greedy++      : " << kb_str(rss_greedy)            << "\n";
    cout << "  After BC compute    : " << kb_str(rss_final)             << "\n";
    cout << "  Net usage by prog   : " << kb_str(rss_final - rss_start) << "\n";
    rule('='); cout << "\n";
}

// ============================================================
//  main  — hardcoded dataset list, run in specified order
// ============================================================
int main(int argc, char* argv[]) {

    // Optional: override iteration count via first CLI argument.
    // Default is 10 passes (paper recommends ~12 for convergence).
    int T = (argc >= 2) ? atoi(argv[1]) : 10;
    if (T < 1) T = 1;

    // Resolve the directory containing this binary so the datasets are
    // found correctly regardless of where the user runs the command from.
    // On Windows this handles both MSVC .exe and MinGW builds.
    fs::path base = exe_dir();

    // Order: wiki-Vote, email-Enron, as-skitter  (as requested).
    const vector<fs::path> datasets = {
        base / "wiki-Vote.txt.gz",
        base / "email-Enron.txt.gz",
        base / "as-skitter.txt.gz"
    };

    cout << "\n"; rule('*', 70);
    cout << "  Greedy++ Batch Run  —  " << datasets.size() << " datasets"
         << "  |  T=" << T << " iterations each\n";
    cout << "  Dataset directory  : " << base.string() << "\n";
    rule('*', 70);

    for (const auto& ds : datasets)
        run_dataset(ds, T);

    cout << "  All datasets processed.\n\n";
    return 0;
}