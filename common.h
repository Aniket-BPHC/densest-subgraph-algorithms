#pragma once

#include <vector>
#include <queue>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <array>

using namespace std;

// -----------------------------------------------------------------------
// Dinic's max-flow with floating-point capacities
// -----------------------------------------------------------------------
struct Dinic {
    struct Edge { int to, rev; double cap; };
    int N;
    vector<vector<Edge>> graph;
    vector<int> level, iter;

    explicit Dinic(int n) : N(n), graph(n), level(n), iter(n) {}

    void add_edge(int from, int to, double cap) {
        graph[from].push_back({to, (int)graph[to].size(), cap});
        graph[to].push_back({from, (int)graph[from].size() - 1, 0.0});
    }

    bool bfs(int s, int t) {
        fill(level.begin(), level.end(), -1);
        queue<int> q;
        level[s] = 0; q.push(s);
        while (!q.empty()) {
            int v = q.front(); q.pop();
            for (auto& e : graph[v])
                if (e.cap > 1e-9 && level[e.to] < 0) {
                    level[e.to] = level[v] + 1; q.push(e.to);
                }
        }
        return level[t] >= 0;
    }

    double dfs(int v, int t, double f) {
        if (v == t) return f;
        for (int& i = iter[v]; i < (int)graph[v].size(); ++i) {
            Edge& e = graph[v][i];
            if (e.cap > 1e-9 && level[v] < level[e.to]) {
                double d = dfs(e.to, t, min(f, e.cap));
                if (d > 1e-9) { e.cap -= d; graph[e.to][e.rev].cap += d; return d; }
            }
        }
        return 0.0;
    }

    double max_flow(int s, int t) {
        double flow = 0;
        while (bfs(s, t)) {
            fill(iter.begin(), iter.end(), 0);
            double d;
            while ((d = dfs(s, t, 1e18)) > 1e-9) flow += d;
        }
        return flow;
    }

    vector<bool> reachable_from_source(int s) {
        vector<bool> vis(N, false);
        queue<int> q;
        vis[s] = true; q.push(s);
        while (!q.empty()) {
            int v = q.front(); q.pop();
            for (auto& e : graph[v])
                if (!vis[e.to] && e.cap > 1e-9) { vis[e.to] = true; q.push(e.to); }
        }
        return vis;
    }
};

// -----------------------------------------------------------------------
// Result type
// -----------------------------------------------------------------------
struct Result {
    vector<int> vertices;
    double density;   // triangles / |vertices|  for h=3
};

// -----------------------------------------------------------------------
// Triangle-core decomposition (k, triangle)-core
//
// For each vertex, computes its "triangle core number" = the highest k
// such that the vertex survives after iteratively removing all vertices
// with triangle-degree < k.
//
// Returns: core[] array indexed by vertex (0-based), and kmax.
// -----------------------------------------------------------------------
inline pair<vector<int>, int> triangle_core_decomposition(
    int n,
    const vector<vector<int>>& adj,
    const vector<array<int,3>>& triangles)
{
    // Triangle degree of each vertex
    vector<int> tdeg(n, 0);
    for (auto& tri : triangles) {
        tdeg[tri[0]]++;
        tdeg[tri[1]]++;
        tdeg[tri[2]]++;
    }

    // Which triangles are still "alive"
    int T = (int)triangles.size();
    vector<bool> tri_alive(T, true);
    // Which vertices are still alive
    vector<bool> alive(n, true);

    vector<int> core(n, 0);
    int kmax = 0;

    // Process in order of increasing tdeg (bin-sort style)
    // For simplicity we use a priority queue
    // (paper uses bin-sort for O(n * C(d-1,h-1)) time)
    using pii = pair<int,int>;
    priority_queue<pii, vector<pii>, greater<pii>> pq;
    for (int v = 0; v < n; ++v)
        pq.push({tdeg[v], v});

    // vertex -> list of triangle indices it appears in
    vector<vector<int>> vtri(n);
    for (int i = 0; i < T; ++i) {
        vtri[triangles[i][0]].push_back(i);
        vtri[triangles[i][1]].push_back(i);
        vtri[triangles[i][2]].push_back(i);
    }

    vector<bool> processed(n, false);

    while (!pq.empty()) {
        auto [d, v] = pq.top(); pq.pop();
        if (processed[v]) continue;
        if (d != tdeg[v]) { pq.push({tdeg[v], v}); continue; } // stale entry
        processed[v] = true;
        core[v] = tdeg[v];
        kmax = max(kmax, core[v]);

        // Remove v: for each alive triangle containing v,
        // decrement tdeg of the other two vertices
        for (int ti : vtri[v]) {
            if (!tri_alive[ti]) continue;
            tri_alive[ti] = false;
            auto& tri = triangles[ti];
            for (int j = 0; j < 3; ++j) {
                int u = tri[j];
                if (u != v && !processed[u]) {
                    tdeg[u]--;
                    // We'll re-push with updated degree; stale entries ignored above
                    pq.push({tdeg[u], u});
                }
            }
        }
    }

    return {core, kmax};
}

// -----------------------------------------------------------------------
// Flow feasibility oracle for h=3 (triangle density)
//
// Algorithm 1 from paper, Psi = triangle:
//   Node set:  s | vertices V | edge-nodes (one per undirected edge in subgraph) | t
//
//   s  -> v        cap = tri_deg(v)
//   v  -> t        cap = T + 3*alpha - tri_deg(v)    [T = #triangles in subgraph]
//
//   For each edge (u,v) as the intermediate (h-1)-clique node 'psi':
//     u -> psi_node   cap = 1
//     v -> psi_node   cap = 1
//     psi_node -> u   cap = INF
//     psi_node -> v   cap = INF
//
// Returns {is_feasible, reachable_vertices_from_s}
// -----------------------------------------------------------------------
// In common.h, replace the entire flow_feasibility_h3 with this:

inline pair<bool, vector<int>> flow_feasibility_h3(
    const vector<int>& verts,
    const vector<pair<int,int>>& edges,
    const vector<array<int,3>>& triangles,
    double alpha)
{
    int sz = (int)verts.size();
    if (sz == 0) return {false, {}};

    unordered_map<int,int> idx;
    for (int i = 0; i < sz; ++i) idx[verts[i]] = i;

    vector<pair<int,int>> enodes;
    for (auto& [u, v] : edges) {
        auto iu = idx.find(u), iv = idx.find(v);
        if (iu == idx.end() || iv == idx.end()) continue;
        enodes.push_back({iu->second, iv->second});
    }
    int ne = (int)enodes.size();

    vector<int> tdeg(sz, 0);
    int T = 0;
    for (auto& tri : triangles) {
        auto ia = idx.find(tri[0]), ib = idx.find(tri[1]), ic = idx.find(tri[2]);
        if (ia == idx.end() || ib == idx.end() || ic == idx.end()) continue;
        tdeg[ia->second]++;
        tdeg[ib->second]++;
        tdeg[ic->second]++;
        T++;
    }

    int s_node = sz + ne;
    int t_node = sz + ne + 1;
    Dinic dinic(sz + ne + 2);
    const double INF = 1e15;

    for (int i = 0; i < sz; ++i) {
        dinic.add_edge(s_node, i, (double)tdeg[i]);
        double cap_vt = (double)T + 3.0 * alpha - tdeg[i];
        if (cap_vt < 0.0) cap_vt = 0.0;
        dinic.add_edge(i, t_node, cap_vt);
    }

    for (int e = 0; e < ne; ++e) {
        int li = enodes[e].first, lj = enodes[e].second;
        int enode = sz + e;
        dinic.add_edge(li, enode, 1.0);
        dinic.add_edge(lj, enode, 1.0);
        dinic.add_edge(enode, li, INF);
        dinic.add_edge(enode, lj, INF);
    }

    dinic.max_flow(s_node, t_node);
    auto vis = dinic.reachable_from_source(s_node);

    vector<int> reachable;
    for (int i = 0; i < sz; ++i)
        if (vis[i]) reachable.push_back(verts[i]);

    return {!reachable.empty(), reachable};
}
