/**
 * algorithm_a.cpp  —  Charikar's Greedy Densest Subgraph + Betweenness Centrality
 * ==================================================================================
 * Implements Algorithm 1 from:
 *   "Flowless: Extracting Densest Subgraphs Without Flow Computations"
 *    Boob et al., WWW '20
 *
 * Usage:
 *   g++ -O2 -std=c++17 -o algorithm_a algorithm_a.cpp -lz
 *   ./algorithm_a
 *
 * The program automatically runs on these three datasets (expected in the
 * same directory as the executable):
 *   • email-Enron.txt.gz
 *   • wiki-Vote.txt.gz
 *   • as-skitter.txt.gz
 *
 * Input (.txt.gz):
 *   Lines starting with '#' are skipped.
 *   Every other line: <u> <v>  (whitespace-separated integer node IDs)
 *   Self-loops and duplicate edges are ignored.
 *   Node IDs need not be contiguous — they are remapped internally.
 *
 * Output (per dataset):
 *   1. Densest subgraph: node count + density rho(S) = e[S]/|S|
 *   2. Top-20 nodes in S ranked by Betweenness Centrality
 *   3. Execution time breakdown (load / greedy / BC)
 *   4. Memory (RSS) at each phase
 *
 * Algorithm guarantee:
 *   rho(output) >= rho* / 2   (1/2-approximation)
 *   Time:  O((n+m) log n)
 *   Space: O(n+m)
 *
 * Portability:
 *   Compiles and runs on Windows, Linux, and macOS.
 *   The executable directory is resolved portably:
 *     - POSIX  : /proc/self/exe  (Linux) or _NSGetExecutablePath (macOS)
 *     - Windows: GetModuleFileNameA
 *   Memory (RSS) reporting falls back gracefully when unavailable.
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
#include <filesystem>   // C++17 — portable path manipulation

#include <zlib.h>

// ---- OS-specific headers for exe path + RSS ---------------------------
#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>   // _NSGetExecutablePath
#  include <sys/resource.h>
#else
// Linux / other POSIX
#  include <unistd.h>
#  include <sys/resource.h>
#endif

namespace fs = std::filesystem;
using namespace std;
using Clock = chrono::high_resolution_clock;

// ============================================================
//  Portable: locate the directory that contains this executable
// ============================================================
static fs::path exe_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);          // query needed size
    string buf(size, '\0');
    _NSGetExecutablePath(buf.data(), &size);
    return fs::canonical(fs::path(buf)).parent_path();

#else  // Linux
    // /proc/self/exe is available on virtually every modern Linux kernel.
    // Fall back to current working directory if it somehow isn't.
    error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return fs::current_path();
#endif
}

// ============================================================
//  Result  — shared return type for the DSP algorithm
// ============================================================
struct Result {
    vector<int> vertices;  // node IDs (0-based, contiguous) in densest subgraph
    double      density;   // e[S] / |S|
};

// ============================================================
//  SECTION 1 — Charikar's Greedy Peeling  (Algorithm 1)
// ============================================================
//
// Maintains H = G and repeatedly removes the vertex u with the
// minimum current degree in H, tracking the densest subgraph seen.
//
// Key implementation details:
//   • edge_count is decremented by deg(u) on each removal (exact, no recount)
//   • lazy min-heap: stale (degree, vertex) pairs are skipped on pop
//   • best node set is recovered in O(n) at the end via removal_order,
//     without storing a snapshot at every improvement step
//
Result charikar_greedy(int n, const vector<pair<int,int>>& edges) {

    // --- Build adjacency list -------------------------------------------
    vector<vector<int>> adj(n);
    for (auto& [u, v] : edges) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    int m = (int)edges.size();

    vector<int>  deg(n);
    vector<bool> active(n, true);
    for (int v = 0; v < n; ++v)
        deg[v] = (int)adj[v].size();

    // --- State -----------------------------------------------------------
    int    active_count = n;
    int    edge_count   = m;
    double best_density = (n > 0) ? (double)m / n : 0.0;
    int    best_ac      = n;   // active_count at the best snapshot

    // peel order lets us reconstruct the best node set at the end
    vector<int> removal_order;
    removal_order.reserve(n);

    // --- Lazy min-heap: (degree, vertex) ---------------------------------
    using pii = pair<int,int>;
    priority_queue<pii, vector<pii>, greater<pii>> pq;
    for (int v = 0; v < n; ++v)
        pq.push({deg[v], v});

    // --- Peeling loop (Algorithm 1, lines 3–9) ---------------------------
    while (active_count > 0) {
        // skip stale heap entries
        while (!pq.empty()) {
            auto [d, v] = pq.top();
            if (active[v] && deg[v] == d) break;
            pq.pop();
        }
        if (pq.empty()) break;

        auto [d, u] = pq.top(); pq.pop();

        // remove u
        active[u]  = false;
        edge_count -= d;        // exactly d edges incident to u remain in H
        --active_count;
        removal_order.push_back(u);

        // update neighbours
        for (int nb : adj[u]) {
            if (active[nb]) {
                --deg[nb];
                pq.push({deg[nb], nb});
            }
        }

        // check density of H \ {u}
        if (active_count > 0) {
            double rho = (double)edge_count / active_count;
            if (rho > best_density) {
                best_density = rho;
                best_ac      = active_count;
            }
        }
    }

    // --- Materialise best node set ---------------------------------------
    // The best_ac nodes that survived longest are the LAST best_ac entries
    // in removal_order (they were removed after the density peak).
    Result result;
    result.density = best_density;

    if (best_ac == n) {
        result.vertices.resize(n);
        iota(result.vertices.begin(), result.vertices.end(), 0);
    } else {
        int start = (int)removal_order.size() - best_ac;
        result.vertices.assign(removal_order.begin() + start,
                               removal_order.end());
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
    double elapsed_ms() const { return chrono::duration<double,milli>(Clock::now()-t0).count(); }
};

// --- RSS memory (KB) — portable, degrades gracefully --------------------
long get_rss_kb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.WorkingSetSize / 1024);
    return -1;

#elif defined(__linux__)
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long kb = -1;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line + 6, " %ld", &kb); break; }
    fclose(f);
    return kb;

#elif defined(__APPLE__)
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return (long)(ru.ru_maxrss / 1024);   // macOS reports bytes
    return -1;

#else
    // Other POSIX: ru_maxrss is in KB
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return ru.ru_maxrss;
    return -1;
#endif
}

static string kb_str(long kb) {
    if (kb < 0)    return "N/A";
    if (kb < 1024) return to_string(kb) + " KB";
    return to_string(kb / 1024) + "." + to_string((kb % 1024) * 10 / 1024) + " MB";
}

// --- .gz reader ----------------------------------------------------------
static string read_gz(const string& path) {
    gzFile gz = gzopen(path.c_str(), "rb");
    if (!gz) {
        fprintf(stderr, "Error: cannot open '%s'\n", path.c_str());
        return "";
    }
    string out;
    out.reserve(1 << 22);
    char buf[65536];
    int  nr;
    while ((nr = gzread(gz, buf, sizeof(buf))) > 0)
        out.append(buf, nr);
    gzclose(gz);
    return out;
}

// --- Graph parser --------------------------------------------------------
// Remaps arbitrary integer node IDs to contiguous [0, n).
struct GraphData {
    int                   n;            // vertex count
    vector<pair<int,int>> edges;        // deduplicated edge list (contiguous IDs)
    vector<int>           original_id;  // original_id[new_id] = original label
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

        auto skip_ws  = [&]{ while (p < end && (*p == ' ' || *p == '\t')) ++p; };
        auto read_int = [&]() -> long long {
            skip_ws();
            if (p >= end || *p == '\n' || *p == '\r') return -1;
            long long v = 0; bool neg = false;
            if (*p == '-') { neg = true; ++p; }
            while (p < end && *p >= '0' && *p <= '9') v = v * 10 + (*p++) - '0';
            return neg ? -v : v;
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

// --- Print helpers -------------------------------------------------------
static void rule(char c = '-', int w = 70) { cout << string(w, c) << "\n"; }

// ============================================================
//  Per-dataset runner
// ============================================================
static void run_dataset(const string& gz_path) {
    cout << "\n"; rule('=');
    cout << "  Charikar Greedy DSP  (Algorithm 1, Boob et al. WWW'20)\n";
    cout << "  File : " << gz_path << "\n";
    rule('=');

    long  rss_start = get_rss_kb();
    Timer tmr;

    // ---- 1. Load & parse ------------------------------------------------
    tmr.reset();
    string    text = read_gz(gz_path);
    if (text.empty()) {
        cerr << "  [SKIP] Could not read file.\n";
        return;
    }
    GraphData G      = parse_graph(text);
    text.clear(); text.shrink_to_fit();
    double t_load  = tmr.elapsed_ms();
    long rss_load  = get_rss_kb();

    int n = G.n, m = (int)G.edges.size();
    cout << "\n[Graph]\n";
    cout << "  Vertices : " << n << "\n";
    cout << "  Edges    : " << m << "\n";

    // ---- 2. Greedy DSP --------------------------------------------------
    tmr.reset();
    Result dsp      = charikar_greedy(n, G.edges);
    double t_greedy = tmr.elapsed_ms();
    long rss_greedy = get_rss_kb();

    int ds = (int)dsp.vertices.size();
    cout << "\n[Densest Subgraph]\n";
    cout << fixed << setprecision(6);
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
             << setw(6)  << (r + 1)
             << setw(14) << orig
             << setw(14) << gid
             << setw(22) << bc[lid]
             << deg << "\n";
    }

    // ---- 6. Timing ------------------------------------------------------
    cout << "\n"; rule();
    cout << "  Execution Time\n"; rule();
    cout << fixed << setprecision(3);
    cout << "  Graph load          : " << setw(10) << t_load              << " ms\n";
    cout << "  Greedy DSP          : " << setw(10) << t_greedy            << " ms\n";
    cout << "  Betweenness Central.: " << setw(10) << t_bc                << " ms\n";
    cout << "  Total               : " << setw(10) << t_load+t_greedy+t_bc << " ms\n";

    // ---- 7. Memory ------------------------------------------------------
    cout << "\n"; rule();
    cout << "  Memory (RSS)\n"; rule();
    cout << "  Baseline (pre-load) : " << kb_str(rss_start)           << "\n";
    cout << "  After graph load    : " << kb_str(rss_load)             << "\n";
    cout << "  After greedy DSP    : " << kb_str(rss_greedy)           << "\n";
    cout << "  After BC compute    : " << kb_str(rss_final)            << "\n";
    cout << "  Net usage by prog   : " << kb_str(rss_final - rss_start) << "\n";
    rule('='); cout << "\n";
}

// ============================================================
//  main — no CLI arguments; datasets are resolved relative to
//          the directory that contains this executable.
// ============================================================
int main() {
    fs::path base = exe_dir();

    // The three hardcoded datasets, expected alongside the executable.
    const vector<string> datasets = {
        "wiki-Vote.txt.gz",
        "email-Enron.txt.gz",
        "as-skitter.txt.gz"
    };

    for (const auto& name : datasets) {
        fs::path full = base / name;
        run_dataset(full.string());
    }

    return 0;
}
