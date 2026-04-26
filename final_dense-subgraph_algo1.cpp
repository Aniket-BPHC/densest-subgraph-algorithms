/* Algorithm 1: Exact Densest Subgraph via Binary Search + Max-Flow  (h=3)
   Triangle density: density(S) = triangles_in_S / |S|
   
   This is a FAITHFUL implementation of Algorithm 1 (Exact) from the paper:
   "Efficient Algorithms for Densest Subgraph Discovery" (Fang et al., PVLDB 2019)
   
   Key properties of Algorithm 1:
   - l initialized to 0, u initialized to max clique-degree over all vertices
   - Flow network is built on the ENTIRE GRAPH in each iteration (no core pruning)
   - Stopping criterion: u - l < 1/(n*(n-1))
   - For h=3, Λ = all edges (the (h-1)-cliques = 2-cliques = edges)
   - Flow network: {s} ∪ V ∪ Λ ∪ {t}
       s  -> v      cap = triangle_degree(v)          [number of triangles v is in]
       v  -> t      cap = alpha * |V_Psi| = 3*alpha   [|V_Psi| = 3 for triangle]
       v  -> psi    cap = 1                            [for each edge psi containing v]
       psi -> v     cap = INF                          [for each vertex v of edge psi]

   Graph is read as UNDIRECTED (directed edges deduplicated).
*/

#include <chrono>
#include <array>
#include <sstream>
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <set>
#include <utility>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <cmath>
#include "common3.h"
using namespace std;

// -----------------------------------------------------------------------
// Exact Algorithm 1 for h=3 (triangle density)
// Direct implementation matching the paper's Algorithm 1
// -----------------------------------------------------------------------
Result exact_algorithm1_h3(
    int n,
    const vector<vector<int>>& adj,
    const vector<pair<int,int>>& edges,     // all undirected edges (u<v)
    const vector<array<int,3>>& triangles)
{
    int m = (int)edges.size();
    int T = (int)triangles.size();

    // ---------------------------------------------------------------
    // Step 1: Compute triangle-degree for each vertex
    // triangle_deg[v] = number of triangles containing v
    // This is deg_G(v, Psi) in the paper (clique-degree w.r.t. triangle)
    // ---------------------------------------------------------------
    vector<int> tri_deg(n, 0);
    for (auto& tri : triangles) {
        tri_deg[tri[0]]++;
        tri_deg[tri[1]]++;
        tri_deg[tri[2]]++;
    }

    // ---------------------------------------------------------------
    // Step 2: Initialize bounds
    // l = 0  (line 1 of Algorithm 1)
    // u = max_{v in V} deg_G(v, Psi)  (line 1 of Algorithm 1)
    // ---------------------------------------------------------------
    double l = 0.0;
    double u = 0.0;
    for (int v = 0; v < n; v++)
        u = max(u, (double)tri_deg[v]);

    cout << "  Initial bounds: l=" << l << " u=" << u << "\n";

    // Stopping criterion from paper: 1 / (n*(n-1))
    // We use n = total number of vertices in the graph
    double EPS = (n > 1) ? 1.0 / ((double)n * (double)(n - 1)) : 1e-9;
    cout << "  Stopping epsilon: " << scientific << EPS << fixed << "\n";

    // ---------------------------------------------------------------
    // Step 3: Collect all (h-1)-clique instances = edges (for h=3)
    // Λ = all edges in the graph  (line 2 of Algorithm 1)
    // ---------------------------------------------------------------
    // edges[] is already our Λ

    // ---------------------------------------------------------------
    // Step 4: Binary search with max-flow (lines 3-18)
    // ---------------------------------------------------------------
    Result best;
    best.vertices.clear();
    best.density = 0.0;

    // Map edge index: for each edge (u,v), we need to know which
    // edge-node to connect to for each endpoint
    // edge_of[v] = list of edge indices where v appears
    vector<vector<int>> edge_of(n);
    for (int e = 0; e < m; e++) {
        edge_of[edges[e].first].push_back(e);
        edge_of[edges[e].second].push_back(e);
    }

    int iteration = 0;
    while (u - l >= EPS) {
        double alpha = (l + u) / 2.0;  // line 4

        // Build flow network (lines 5-15 of Algorithm 1)
        // Node layout:
        //   0 .. n-1        : vertices V
        //   n .. n+m-1      : edge-nodes Λ (one per undirected edge)
        //   n+m             : source s
        //   n+m+1           : sink t
        int s_node = n + m;
        int t_node = n + m + 1;
        Dinic dinic(n + m + 2);
        const double INF = 1e15;

        // Lines 6-8: for each vertex v
        //   s -> v  cap = deg_G(v, Psi) = tri_deg[v]
        //   v -> t  cap = alpha * |V_Psi| = 3*alpha  (|V_Psi|=3 for triangle)
        for (int v = 0; v < n; v++) {
            dinic.add_edge(s_node, v, (double)tri_deg[v]);
            dinic.add_edge(v, t_node, 3.0 * alpha);
        }

        // Lines 9-15: for each edge (u,v) as the (h-1)-clique psi:
        //   For each vertex w in psi (both endpoints of the edge):
        //     psi -> w  cap = INF   (line 11)
        //   For each vertex w in V that forms a triangle with psi:
        //     w -> psi  cap = 1     (lines 12-15: if psi and w form an h-clique)
        //
        // For h=3, psi = edge (u,v). A vertex w "forms a triangle with psi"
        // iff (u,v,w) is a triangle, i.e., w is a common neighbor of u and v.
        // Each such (u,v,w) triangle contributes:
        //   w -> psi_edge  cap = 1
        // Note: each triangle has 3 edges, so each triangle contributes
        // 3 such "w -> psi" edges (one per edge of the triangle).

        // First: psi -> each endpoint of the edge (INF capacity)
        for (int e = 0; e < m; e++) {
            int eu = edges[e].first, ev = edges[e].second;
            int enode = n + e;
            dinic.add_edge(enode, eu, INF);  // psi -> u
            dinic.add_edge(enode, ev, INF);  // psi -> v
        }

        // Second: for each triangle (a,b,c), each of its 3 edges
        // contributes one "third vertex -> edge-node" arc with cap=1.
        // Triangle (a,b,c) has edges (a,b), (a,c), (b,c).
        // Build a map from (u,v) pair to edge index for fast lookup.
        // We already have edges[] sorted so we can binary search.
        // Let's precompute a lookup:
        // (We'll use the edge_of adjacency list + binary_search)

        // For efficiency we build an edge index map once outside the loop,
        // but since this is Algorithm 1 (simple/exact, rebuilt each iter),
        // we do it straightforwardly here.
        // edge_idx[(min,max)] -> index in edges[]
        // We do this once before the loop ideally; here inlined for clarity.

        // For each triangle, add the 3 "vertex -> edge-node" arcs
        for (auto& tri : triangles) {
            int a = tri[0], b = tri[1], c = tri[2]; // a < b < c

            // Edge (a,b) = edge index we need
            // Edge (a,c) = edge index we need
            // Edge (b,c) = edge index we need
            // Use binary search on edges[] (sorted by first then second)
            // to find edge indices.

            // vertex c -> edge(a,b)-node
            // vertex b -> edge(a,c)-node
            // vertex a -> edge(b,c)-node

            // Find edge(a,b): binary search
            auto find_edge = [&](int u, int v) -> int {
                // edges are stored with u < v
                if (u > v) swap(u, v);
                auto it = lower_bound(edges.begin(), edges.end(), make_pair(u, v));
                if (it != edges.end() && *it == make_pair(u, v))
                    return (int)(it - edges.begin());
                return -1;
            };

            int eab = find_edge(a, b);
            int eac = find_edge(a, c);
            int ebc = find_edge(b, c);

            if (eab >= 0) dinic.add_edge(c, n + eab, 1.0); // c -> psi(a,b)
            if (eac >= 0) dinic.add_edge(b, n + eac, 1.0); // b -> psi(a,c)
            if (ebc >= 0) dinic.add_edge(a, n + ebc, 1.0); // a -> psi(b,c)
        }

        // Compute max-flow / min-cut (line 16)
        dinic.max_flow(s_node, t_node);

        // Find vertices reachable from s in residual graph
        auto vis = dinic.reachable_from_source(s_node);

        // S = vertices reachable from s, excluding s itself (line 16)
        vector<int> S_verts;
        for (int v = 0; v < n; v++)
            if (vis[v]) S_verts.push_back(v);

        if (S_verts.empty()) {
            // S = {s} only => line 17: u <- alpha
            u = alpha;
        } else {
            // S != {s} => line 18: l <- alpha, D <- subgraph induced by S\{s}
            l = alpha;
            best.vertices = S_verts;
        }

        iteration++;
        if (iteration % 5 == 0 || iteration <= 3)
            cout << "  Iter " << iteration << ": l=" << fixed << setprecision(6)
                 << l << " u=" << u
                 << " |S|=" << S_verts.size() << "\n";
    }

    // Compute exact triangle count in best subset
    set<int> in_set(best.vertices.begin(), best.vertices.end());
    long long tri_count = 0;
    for (auto& tri : triangles)
        if (in_set.count(tri[0]) && in_set.count(tri[1]) && in_set.count(tri[2]))
            tri_count++;

    best.density = best.vertices.empty() ? 0.0
                 : (double)tri_count / best.vertices.size();

    cout << "  Total iterations: " << iteration << "\n";
    return best;
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    vector<string> datasets = {"wiki-Vote.txt", "email-Enron.txt"};

    for (const string& filename : datasets) {
        auto start_time = chrono::high_resolution_clock::now();
        cout << "\n>>> Starting Dataset: " << filename << endl;
        //if (argc > 1) filename = argv[1];

        ifstream fin(filename);
        if (!fin) { cout << "Cannot open: " << filename << "\n"; continue;; }

        // Read graph as undirected (deduplicate directed edges)
        set<pair<int,int>> edge_set_raw;
        int max_node = -1;
        string line;
        while (getline(fin, line)) {
            if (line.empty() || line[0] == '#') continue;
            int u, v;
            stringstream ss(line); ss >> u >> v;
            if (u == v) continue;
            edge_set_raw.insert({min(u,v), max(u,v)});
            max_node = max(max_node, max(u,v));
        }

        int n = max_node + 1;
        vector<pair<int,int>> edges(edge_set_raw.begin(), edge_set_raw.end());

        vector<vector<int>> adj(n);
        for (auto& [u, v] : edges) { adj[u].push_back(v); adj[v].push_back(u); }
        for (int i = 0; i < n; i++) sort(adj[i].begin(), adj[i].end());

        cout << "Dataset: " << filename << "\n";
        cout << "Nodes = " << n << ", Edges = " << edges.size() << "\n";

        // Enumerate triangles (a < b < c)
        vector<array<int,3>> triangles;
        for (int u = 0; u < n; u++)
            for (int v : adj[u]) { if (v <= u) continue;
                for (int w : adj[v]) { if (w <= v) continue;
                    if (binary_search(adj[u].begin(), adj[u].end(), w))
                        triangles.push_back({u, v, w});
                }
            }
        cout << "Triangles = " << triangles.size() << "\n\n";
        cout << "\n=== Finding the Densest Subgraph (Algorithm 1 : Exact from paper 1. Densest Subgraph, h=3) ===\n";
        

        Result r = exact_algorithm1_h3(n, adj, edges, triangles);

        cout << "\n============================================================\n";
        cout << "            DENSEST SUBGRAPH (Triangle Density)\n";
        cout << "============================================================\n";
        int tri_in_S = (int)(r.density * r.vertices.size() + 0.5);
        cout << "  Number of vertices in D      : " << r.vertices.size() << "\n";
        cout << "  Number of triangles in D     : " << tri_in_S << "\n";
        cout << "  Triangle density (tri/|V|)  : " << fixed << setprecision(6) << r.density << "\n";
        cout << "\n  Vertices (first 30): ";
        int cnt = 0;
        for (int v : r.vertices) { 
            cout << v; 
            if (++cnt >= 30) { cout << " ..."; break; } 
            cout << " ";
        }
        auto end_time = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed = end_time - start_time;
        
        cout << "  Total time for " << filename << " : " << fixed << setprecision(2) 
             << elapsed.count() << " seconds" << endl;
        cout << "\n============================================================\n";
        fin.close();
    }
    

    return 0;
}
