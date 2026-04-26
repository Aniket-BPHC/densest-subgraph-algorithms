// =============================================================================
//  CoreExact  —  Algorithm 4 from Fang et al., PVLDB 2019
//  Densest Subgraph Discovery  |  Psi = Triangle (h = 3)
//
//  Hardcoded datasets (must be in the same folder as the executable):
//    wiki-Vote.txt.gz
//    email-Enron.txt.gz
//    as-skitter.txt.gz
//
//  Outputs for every dataset:
//    1. Top-20 nodes by Betweenness Centrality
//    2. Execution time breakdown  (timer resets to 0 for each dataset)
//    3. Peak memory / space consumption
//
//  ── HOW TO BUILD ─────────────────────────────────────────────────────────────
//
//  Windows — MSVC (Developer Command Prompt):
//    cl /O2 /std:c++17 /EHsc core_exact.cpp /Fe:core_exact.exe /link psapi.lib
//
//  Windows — MinGW-w64 (g++ in PATH):
//    g++ -O2 -std=c++17 -Wall -o core_exact.exe core_exact.cpp -lpsapi
//
//  Linux / macOS:
//    g++ -O2 -std=c++17 -Wall -o core_exact core_exact.cpp
//
//  With system zlib (any platform):
//    g++ -O2 -std=c++17 -Wall -D_USE_ZLIB_ -o core_exact core_exact.cpp -lz
//
//  ── EDGE-LIST FORMAT ─────────────────────────────────────────────────────────
//    One "u v" integer pair per line.
//    Lines starting with '#' or '%' are comments and are ignored.
//    Both 0-based and 1-based node IDs work; duplicates are removed.
// =============================================================================

// ── Platform detection ────────────────────────────────────────────────────────
#if defined(_WIN32) || defined(_WIN64)
  #define PLATFORM_WINDOWS 1
#else
  #define PLATFORM_WINDOWS 0
#endif

// ── Standard headers ──────────────────────────────────────────────────────────
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace std::chrono;

// =============================================================================
//  SECTION 0 — Executable directory (OS-agnostic)
//              Returns the folder that contains the running binary,
//              with a trailing path separator.
// =============================================================================
#if PLATFORM_WINDOWS
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <psapi.h>
  #pragma comment(lib, "psapi.lib")

  static string exe_dir() {
      char buf[MAX_PATH];
      DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
      if (!len) return "";
      string p(buf, len);
      auto pos = p.find_last_of("\\/");
      return (pos != string::npos) ? p.substr(0, pos + 1) : "";
  }
  static long mem_kb() {
      PROCESS_MEMORY_COUNTERS pmc{};
      if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
          return static_cast<long>(pmc.WorkingSetSize / 1024);
      return 0;
  }

#else
  // Linux / macOS
  #include <sys/resource.h>
  #if defined(__linux__)
    #include <unistd.h>   // readlink
  #elif defined(__APPLE__)
    #include <mach-o/dyld.h>
  #endif

  static string exe_dir() {
      char buf[4096] = {};
  #if defined(__linux__)
      ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
      if (len < 0) return "";
      string p(buf, len);
  #elif defined(__APPLE__)
      uint32_t sz = sizeof(buf);
      if (_NSGetExecutablePath(buf, &sz) != 0) return "";
      string p(buf);
  #else
      return "";   // fallback: use cwd
  #endif
      auto pos = p.find_last_of('/');
      return (pos != string::npos) ? p.substr(0, pos + 1) : "./";
  }
  static long mem_kb() {
      struct rusage r; getrusage(RUSAGE_SELF, &r);
  #ifdef __APPLE__
      return r.ru_maxrss / 1024;
  #else
      return r.ru_maxrss;
  #endif
  }
#endif   // platform

// =============================================================================
//  SECTION 1 — GZ / plain-text reader (auto-detects gzip by magic bytes)
//
//    1-A  _USE_ZLIB_         : link against system zlib (all platforms)
//    1-B  Linux/macOS        : shell out to `gzip -dc`  (no extra library)
//    1-C  Windows (default)  : built-in INFLATE (tinf)  (zero dependencies)
// =============================================================================

#if defined(_USE_ZLIB_)
// ── 1-A : zlib ────────────────────────────────────────────────────────────────
#include <zlib.h>
class GzReader {
    gzFile   gz_    = nullptr;
    bool     plain_ = false;
    ifstream pf_;
public:
    bool open(const string& p) {
        { ifstream t(p, ios::binary); char m[2]{}; t.read(m, 2);
          if ((uint8_t)m[0]==0x1f && (uint8_t)m[1]==0x8b)
              { gz_=gzopen(p.c_str(),"rb"); return gz_!=nullptr; }
        }
        plain_=true; pf_.open(p); return pf_.is_open();
    }
    bool getline(string& line) {
        if (plain_) return (bool)std::getline(pf_, line);
        char buf[4096]; line.clear();
        while (gzgets(gz_, buf, sizeof(buf))) {
            line += buf;
            if (!line.empty() && line.back()=='\n') {
                while (!line.empty() && (line.back()=='\n'||line.back()=='\r'))
                    line.pop_back();
                return true;
            }
        }
        return !line.empty();
    }
    ~GzReader() { if (gz_) gzclose(gz_); }
};

#elif !PLATFORM_WINDOWS
// ── 1-B : popen (Linux / macOS, no zlib) ─────────────────────────────────────
#include <cstdio>
class GzReader {
    FILE*    pipe_  = nullptr;
    bool     plain_ = false;
    ifstream pf_;
public:
    bool open(const string& p) {
        { ifstream t(p, ios::binary); char m[2]{}; t.read(m, 2);
          if ((uint8_t)m[0]==0x1f && (uint8_t)m[1]==0x8b)
              { pipe_=popen(("gzip -dc \""+p+"\"").c_str(),"r"); return pipe_!=nullptr; }
        }
        plain_=true; pf_.open(p); return pf_.is_open();
    }
    bool getline(string& line) {
        if (plain_) return (bool)std::getline(pf_, line);
        char buf[4096]; line.clear();
        while (fgets(buf, sizeof(buf), pipe_)) {
            line += buf;
            if (!line.empty() && line.back()=='\n') {
                while (!line.empty() && (line.back()=='\n'||line.back()=='\r'))
                    line.pop_back();
                return true;
            }
        }
        return !line.empty();
    }
    ~GzReader() { if (pipe_) pclose(pipe_); }
};

#else
// ── 1-C : Built-in INFLATE for Windows (tinf — zero external deps) ────────────
namespace tinf {

struct HuffTree { int count[16]{}; int symbol[288]{}; };

static void build_tree(HuffTree& t, const uint8_t* lens, int n) {
    memset(t.count, 0, sizeof(t.count));
    for (int i=0;i<n;i++) if (lens[i]) t.count[(int)lens[i]]++;
    int off[16]{};
    for (int i=1;i<15;i++) off[i+1]=off[i]+t.count[i];
    for (int i=0;i<n;i++) if (lens[i]) t.symbol[off[(int)lens[i]]++]=i;
}

struct Inf {
    const uint8_t* src; size_t slen, spos;
    vector<uint8_t>& dst;
    uint32_t bits=0; int nb=0;

    Inf(const uint8_t* s, size_t l, vector<uint8_t>& d)
        : src(s), slen(l), spos(0), dst(d) {}

    uint32_t rb(int n) {
        while (nb<n) {
            if (spos>=slen) throw runtime_error("gz: truncated stream");
            bits |= (uint32_t)src[spos++]<<nb; nb+=8;
        }
        uint32_t v=bits&((1u<<n)-1); bits>>=n; nb-=n; return v;
    }
    int dec(const HuffTree& t) {
        int sum=0,cur=0,len=0;
        do { cur=(cur<<1)|(int)rb(1); len++;
             sum+=t.count[len]; cur-=t.count[len]; }
        while (cur>=0 && len<15);
        return t.symbol[sum+cur];
    }
    static void fixed_ll(uint8_t* l, uint8_t* d) {
        for (int i=  0;i<144;i++) l[i]=8; for (int i=144;i<256;i++) l[i]=9;
        for (int i=256;i<280;i++) l[i]=7; for (int i=280;i<288;i++) l[i]=8;
        for (int i=0;i<32;i++) d[i]=5;
    }
    static const int LB[29],LE[29],DB[30],DE[30];

    void blk(const HuffTree& lt, const HuffTree& dt) {
        for (;;) {
            int s=dec(lt);
            if      (s<256)  { dst.push_back((uint8_t)s); }
            else if (s==256) { break; }
            else {
                int li=s-257, len=LB[li]+(int)rb(LE[li]);
                int di=dec(dt), dist=DB[di]+(int)rb(DE[di]);
                size_t p=dst.size();
                if ((size_t)dist>p) throw runtime_error("gz: bad back-ref");
                for (int k=0;k<len;k++) dst.push_back(dst[p-dist+k]);
            }
        }
    }
    void run() {
        bool fin=false;
        while (!fin) {
            fin=rb(1)!=0; int bt=(int)rb(2);
            if (bt==0) {
                bits=0; nb=0;
                if (spos+4>slen) throw runtime_error("gz: trunc");
                uint16_t ln=src[spos]|((uint16_t)src[spos+1]<<8); spos+=2;
                uint16_t nl=src[spos]|((uint16_t)src[spos+1]<<8); spos+=2;
                if ((uint16_t)(ln^nl)!=0xFFFFu) throw runtime_error("gz: stored check");
                if (spos+ln>slen) throw runtime_error("gz: trunc");
                dst.insert(dst.end(), src+spos, src+spos+ln); spos+=ln;
            } else if (bt==1) {
                uint8_t ll[288],dl[32]; fixed_ll(ll,dl);
                HuffTree lt,dt; build_tree(lt,ll,288); build_tree(dt,dl,32);
                blk(lt,dt);
            } else if (bt==2) {
                int hl=(int)rb(5)+257, hd=(int)rb(5)+1, hc=(int)rb(4)+4;
                static const int CO[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl[19]{};
                for (int i=0;i<hc;i++) cl[CO[i]]=(uint8_t)rb(3);
                HuffTree ct; build_tree(ct,cl,19);
                vector<uint8_t> lens(hl+hd,0);
                for (int i=0;i<hl+hd;) {
                    int s=dec(ct);
                    if      (s<16)  { lens[i++]=(uint8_t)s; }
                    else if (s==16) { int r=(int)rb(2)+3; uint8_t v=lens[i-1]; while(r--) lens[i++]=v; }
                    else if (s==17) { int r=(int)rb(3)+3; while(r--) lens[i++]=0; }
                    else            { int r=(int)rb(7)+11; while(r--) lens[i++]=0; }
                }
                HuffTree lt,dt;
                build_tree(lt,lens.data(),hl); build_tree(dt,lens.data()+hl,hd);
                blk(lt,dt);
            } else { throw runtime_error("gz: reserved block type"); }
        }
    }
};
const int Inf::LB[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const int Inf::LE[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const int Inf::DB[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const int Inf::DE[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static vector<uint8_t> decompress_gz(const string& path) {
    ifstream f(path, ios::binary);
    if (!f) throw runtime_error("Cannot open: "+path);
    vector<uint8_t> raw((istreambuf_iterator<char>(f)),{});
    if (raw.size()<18||raw[0]!=0x1F||raw[1]!=0x8B) throw runtime_error("Not gzip");
    if (raw[2]!=8) throw runtime_error("Unsupported compression method");
    uint8_t flg=raw[3]; size_t pos=10;
    if (flg&4)  { uint16_t xl=raw[pos]|((uint16_t)raw[pos+1]<<8); pos+=2+xl; }
    if (flg&8)  { while(raw[pos++]!=0); }
    if (flg&16) { while(raw[pos++]!=0); }
    if (flg&2)  { pos+=2; }
    if (raw.size()<pos+8) throw runtime_error("gz too short");
    size_t cl=raw.size()-pos-8;
    uint32_t isz=raw[raw.size()-4]|((uint32_t)raw[raw.size()-3]<<8)
                |((uint32_t)raw[raw.size()-2]<<16)|((uint32_t)raw[raw.size()-1]<<24);
    vector<uint8_t> out; out.reserve(isz);
    Inf inf(raw.data()+pos, cl, out); inf.run();
    return out;
}
} // namespace tinf

class GzReader {
    vector<uint8_t> data_; size_t pos_=0;
    bool plain_=false; ifstream pf_;
public:
    bool open(const string& path) {
        { ifstream pk(path, ios::binary); if (!pk) return false;
          char m[2]{}; pk.read(m,2);
          if ((uint8_t)m[0]==0x1f&&(uint8_t)m[1]==0x8b) {
              try { data_=tinf::decompress_gz(path); return true; }
              catch (exception& e) { cerr<<"[gz] "<<e.what()<<" -> plain text\n"; }
          }
        }
        plain_=true; pf_.open(path); return pf_.is_open();
    }
    bool getline(string& line) {
        if (plain_) return (bool)std::getline(pf_, line);
        if (pos_>=data_.size()) return false;
        line.clear();
        while (pos_<data_.size()) {
            char c=(char)data_[pos_++];
            if (c=='\n') break;
            if (c!='\r') line+=c;
        }
        return true;
    }
};
#endif   // end GzReader

// =============================================================================
//  SECTION 2 — Dinic's Maximum Flow  (double capacities)
// =============================================================================
struct FEdge { int to, rev; double cap; };

struct Dinic {
    int n;
    vector<vector<FEdge>> g;
    vector<int> lv, it;

    explicit Dinic(int n) : n(n), g(n), lv(n), it(n) {}

    void add_edge(int u, int v, double c) {
        g[u].push_back({v,(int)g[v].size(),c});
        g[v].push_back({u,(int)g[u].size()-1,0.0});
    }
    bool bfs(int s, int t) {
        fill(lv.begin(),lv.end(),-1); queue<int> q;
        lv[s]=0; q.push(s);
        while (!q.empty()) { int v=q.front();q.pop();
            for (auto& e:g[v])
                if (e.cap>1e-9&&lv[e.to]<0){lv[e.to]=lv[v]+1;q.push(e.to);}
        }
        return lv[t]>=0;
    }
    double dfs(int v, int t, double f) {
        if (v==t) return f;
        for (int& i=it[v];i<(int)g[v].size();i++) {
            auto& e=g[v][i];
            if (e.cap>1e-9&&lv[e.to]==lv[v]+1) {
                double d=dfs(e.to,t,min(f,e.cap));
                if (d>1e-9){e.cap-=d;g[e.to][e.rev].cap+=d;return d;}
            }
        }
        return 0.0;
    }
    double max_flow(int s, int t) {
        double fl=0.0;
        while (bfs(s,t)){
            fill(it.begin(),it.end(),0);
            double d; while ((d=dfs(s,t,1e18))>1e-9) fl+=d;
        }
        return fl;
    }
    vector<bool> reachable(int s) {
        vector<bool> vis(n,false); queue<int> q;
        vis[s]=true; q.push(s);
        while (!q.empty()){int v=q.front();q.pop();
            for (auto& e:g[v]) if (e.cap>1e-9&&!vis[e.to]){vis[e.to]=true;q.push(e.to);}
        }
        return vis;
    }
};

// =============================================================================
//  SECTION 3 — Graph  (undirected, simple, 0-indexed internally)
// =============================================================================
struct Graph {
    int  n=0; long long m=0;
    vector<vector<int>>        adj;
    vector<unordered_set<int>> aset;

    void init(int _n){ n=_n; adj.assign(n,{}); aset.assign(n,{}); }
    void add_edge(int u, int v){
        adj[u].push_back(v); aset[u].insert(v);
        adj[v].push_back(u); aset[v].insert(u);
        ++m;
    }
    bool has(int u, int v) const { return u<n && aset[u].count(v)>0; }
};

// =============================================================================
//  SECTION 4 — Triangle utilities
// =============================================================================
static vector<long long> tri_deg_in(const Graph& g, const vector<int>& verts) {
    unordered_set<int> vs(verts.begin(),verts.end());
    vector<long long>  deg(g.n,0);
    for (int u:verts)
        for (int v:g.adj[u]){ if (!vs.count(v)||v<=u) continue;
            for (int w:g.adj[u]){ if (!vs.count(w)||w<=v) continue;
                if (g.has(v,w)){++deg[u];++deg[v];++deg[w];}
            }
        }
    return deg;
}

static long long count_tri(const Graph& g, const vector<int>& verts) {
    unordered_set<int> vs(verts.begin(),verts.end());
    long long c=0;
    for (int u:verts)
        for (int v:g.adj[u]){ if (!vs.count(v)||v<=u) continue;
            for (int w:g.adj[u]){ if (!vs.count(w)||w<=v) continue;
                if (g.has(v,w)) ++c;
            }
        }
    return c;
}

static double tri_density(const Graph& g, const vector<int>& verts) {
    return verts.empty() ? 0.0 : (double)count_tri(g,verts)/(double)verts.size();
}

// =============================================================================
//  SECTION 5 — (k, Psi)-core decomposition  [Algorithm 3, Psi = triangle]
// =============================================================================
struct DecompResult {
    vector<int> core;
    int         kmax = 0;
    double      rho_prime = 0.0;
};

static DecompResult core_decomp(const Graph& g) {
    int n=g.n;
    DecompResult res; res.core.assign(n,0);

    vector<int> all(n); iota(all.begin(),all.end(),0);
    auto td0=tri_deg_in(g,all);
    vector<long long> td(td0.begin(),td0.end());

    long long cur_tri=0; for (auto x:td) cur_tri+=x; cur_tri/=3;

    long long max_d=td.empty()?0:*max_element(td.begin(),td.end());
    vector<vector<int>> bin(max_d+2);
    vector<long long>   pos(n);
    for (int v=0;v<n;v++){ bin[td[v]].push_back(v); pos[v]=td[v]; }

    vector<bool> removed(n,false);
    int remaining=n;
    double best_d = n>0 ? (double)cur_tri/n : 0.0;

    for (long long k=0; remaining>0; ) {
        while (k<=max_d && bin[k].empty()) ++k;
        if (k>max_d) break;

        int v=bin[k].back(); bin[k].pop_back();
        if (removed[v])  continue;
        if (pos[v]>k)    { bin[pos[v]].push_back(v); continue; }

        res.core[v]=(int)k;
        res.kmax=max(res.kmax,(int)k);
        removed[v]=true; --remaining;

        for (int u:g.adj[v]){
            if (removed[u]) continue;
            for (int w:g.adj[v]){
                if (removed[w]||w<=u) continue;
                if (g.has(u,w)){
                    --cur_tri;
                    { long long nu=max(td[u]-1,k);
                      bin[pos[u]].push_back(u);
                      td[u]=nu; pos[u]=nu; bin[nu].push_back(u); }
                    { long long nw=max(td[w]-1,k);
                      bin[pos[w]].push_back(w);
                      td[w]=nw; pos[w]=nw; bin[nw].push_back(w); }
                }
            }
        }

        if (remaining>0){
            double d=(double)cur_tri/remaining;
            if (d>best_d) best_d=d;
        }
    }
    res.rho_prime=best_d;
    return res;
}

// =============================================================================
//  SECTION 6 — Graph helpers
// =============================================================================
static vector<int> verts_ge(const vector<int>& core, int k) {
    vector<int> v;
    for (int i=0;i<(int)core.size();i++) if (core[i]>=k) v.push_back(i);
    return v;
}

static vector<vector<int>> conn_comps(const Graph& g, const vector<int>& verts) {
    unordered_set<int> vs(verts.begin(),verts.end());
    unordered_map<int,bool> vis; for (int v:verts) vis[v]=false;
    vector<vector<int>> comps;
    for (int s:verts){
        if (vis[s]) continue;
        vector<int> comp; queue<int> q; q.push(s); vis[s]=true;
        while (!q.empty()){int v=q.front();q.pop();comp.push_back(v);
            for (int u:g.adj[v]) if (vs.count(u)&&!vis[u]){vis[u]=true;q.push(u);}
        }
        comps.push_back(move(comp));
    }
    return comps;
}

// =============================================================================
//  SECTION 7 — Flow network construction + solve  (h=3, Psi=triangle)
// =============================================================================
struct FlowRes { bool found=false; vector<int> subverts; double density=0.0; };

static FlowRes solve_flow(const Graph& g, const vector<int>& comp, double alpha) {
    unordered_set<int> vs(comp.begin(),comp.end());

    vector<pair<int,int>> lambda;
    for (int u:comp) for (int v:g.adj[u]) if (vs.count(v)&&v>u) lambda.push_back({u,v});

    int nv=(int)comp.size(), ne=(int)lambda.size();
    int s=0, t=nv+ne+1;
    const double INF=1e15;
    Dinic din(t+1);

    unordered_map<int,int> vid;
    for (int i=0;i<nv;i++) vid[comp[i]]=i+1;

    auto td=tri_deg_in(g,comp);

    for (int i=0;i<nv;i++){
        int v=comp[i];
        din.add_edge(s,    vid[v], (double)td[v]);
        din.add_edge(vid[v], t,   alpha*3.0);
    }
    for (int j=0;j<ne;j++){
        auto [eu,ev]=lambda[j]; int pn=nv+1+j;
        din.add_edge(pn,vid[eu],INF);
        din.add_edge(pn,vid[ev],INF);
        for (int w:comp){
            if (w==eu||w==ev) continue;
            if (g.has(eu,w)&&g.has(ev,w)) din.add_edge(vid[w],pn,1.0);
        }
    }

    din.max_flow(s,t);
    auto reach=din.reachable(s);

    FlowRes fr;
    for (int i=0;i<nv;i++) if (reach[vid[comp[i]]]){fr.found=true;break;}
    if (fr.found){
        for (int i=0;i<nv;i++) if (reach[vid[comp[i]]]) fr.subverts.push_back(comp[i]);
        fr.density=tri_density(g,fr.subverts);
    }
    return fr;
}

// =============================================================================
//  SECTION 8 — CoreExact  (Algorithm 4)
// =============================================================================
struct CEResult {
    int    kmax=0;
    double rho_prime=0.0;     int k_prime=0;
    double rho_double_prime=0.0; int k_double_prime=0;
    double init_lower=0.0, init_upper=0.0;
    double final_lower=0.0, final_upper=0.0;
    int    flow_solves=0;
    vector<int> cds_verts;
    double      cds_density=0.0;
    long long   cds_triangles=0, cds_edges=0;
};

static CEResult core_exact(const Graph& g) {
    CEResult res;

    auto dc=core_decomp(g);
    res.kmax=dc.kmax; res.rho_prime=dc.rho_prime;

    res.k_prime=max(1,(int)ceil(dc.rho_prime));
    auto cv1=verts_ge(dc.core,res.k_prime);

    auto comps1=conn_comps(g,cv1);
    res.rho_double_prime=0.0; vector<int> best_cc;
    for (auto& cc:comps1){ double d=tri_density(g,cc);
        if (d>res.rho_double_prime){res.rho_double_prime=d;best_cc=cc;} }
    res.k_double_prime=max(res.k_prime,(int)ceil(res.rho_double_prime));

    double l=max(res.rho_double_prime,(double)res.kmax/3.0);
    double u=(double)res.kmax;
    res.init_lower=l; res.init_upper=u;

    auto wv=verts_ge(dc.core,res.k_double_prime);
    auto wc=conn_comps(g,wv);
    double D_dens=res.rho_double_prime; vector<int> D_verts=best_cc;
    for (auto& cc:wc){ double d=tri_density(g,cc);
        if (d>D_dens){D_dens=d;D_verts=cc;} }

    int fsol=0;

    for (auto& comp_orig:wc){
        if (comp_orig.empty()) continue;
        vector<int> curr=comp_orig;

        auto eps=[&](){ int sz=(int)curr.size();
            return (sz>1)?1.0/((double)sz*(sz-1)):1e-9; };

        auto refine=[&](){
            int kn=(int)ceil(l);
            if (kn>res.k_double_prime){
                vector<int> r; for (int v:curr) if (dc.core[v]>=kn) r.push_back(v);
                curr=move(r);
            }
        };
        refine(); if (curr.empty()) continue;

        { ++fsol; auto fr=solve_flow(g,curr,l);
          if (!fr.found) continue;
          if (fr.density>D_dens){D_dens=fr.density;D_verts=fr.subverts;}
          l=max(l,fr.density); refine(); if (curr.empty()) continue; }

        double lu=u;
        while (lu-l>=eps()){
            double alpha=(l+lu)/2.0; ++fsol;
            auto fr=solve_flow(g,curr,alpha);
            if (!fr.found){ lu=alpha; }
            else{ l=max(l,fr.density);
                  if (fr.density>D_dens){D_dens=fr.density;D_verts=fr.subverts;}
                  refine(); if (curr.empty()) break; }
        }
    }

    res.final_lower=l; res.final_upper=u;
    res.flow_solves=fsol;
    res.cds_verts=D_verts; res.cds_density=D_dens;
    res.cds_triangles=D_verts.empty()?0:count_tri(g,D_verts);
    unordered_set<int> ds(D_verts.begin(),D_verts.end());
    res.cds_edges=0;
    for (int u:D_verts) for (int v:g.adj[u]) if (ds.count(v)&&v>u) ++res.cds_edges;
    return res;
}

// =============================================================================
//  SECTION 9 — Betweenness Centrality  (Brandes 2001, O(V·E), normalised)
// =============================================================================
static vector<double> brandes_bc(const Graph& g) {
    int n=g.n; vector<double> bc(n,0.0);
    for (int s=0;s<n;s++){
        vector<int>         stk;
        vector<vector<int>> pred(n);
        vector<long long>   sigma(n,0LL);
        vector<int>         dist(n,-1);
        sigma[s]=1; dist[s]=0;
        queue<int> q; q.push(s);
        while (!q.empty()){int v=q.front();q.pop();stk.push_back(v);
            for (int w:g.adj[v]){
                if (dist[w]<0){dist[w]=dist[v]+1;q.push(w);}
                if (dist[w]==dist[v]+1){sigma[w]+=sigma[v];pred[w].push_back(v);}
            }
        }
        vector<double> delta(n,0.0);
        while (!stk.empty()){int w=stk.back();stk.pop_back();
            for (int v:pred[w]) delta[v]+=((double)sigma[v]/sigma[w])*(1.0+delta[w]);
            if (w!=s) bc[w]+=delta[w];
        }
    }
    double norm=(n>2)?2.0/((double)(n-1)*(n-2)):1.0;
    for (auto& b:bc) b*=norm;
    return bc;
}

// =============================================================================
//  SECTION 10 — Graph loader
// =============================================================================
static Graph load_graph(const string& path) {
    GzReader reader;
    if (!reader.open(path)){ cerr<<"ERROR: Cannot open: "<<path<<"\n"; exit(1); }

    map<int,int>          remap;
    vector<pair<int,int>> raw;
    string line;
    while (reader.getline(line)){
        if (line.empty()||line[0]=='#'||line[0]=='%') continue;
        istringstream ss(line); int a,b;
        if (!(ss>>a>>b)||a==b) continue;
        raw.push_back({a,b}); remap[a]; remap[b];
    }
    int idx=0; for (auto& [k,val]:remap) val=idx++;

    Graph g; g.init(idx);
    set<pair<int,int>> seen;
    for (auto [a,b]:raw){
        int ra=remap[a], rb=remap[b];
        if (ra>rb) swap(ra,rb);
        if (!seen.insert({ra,rb}).second) continue;
        g.add_edge(ra,rb);
    }
    return g;
}

// =============================================================================
//  SECTION 11 — Pretty-print helpers
// =============================================================================
static void hline(char c='-', int w=72){ for(int i=0;i<w;i++) cout<<c; cout<<"\n"; }

static string fmt_ms(double ms){
    ostringstream s;
    if      (ms<1.0)  s<<fixed<<setprecision(3)<<ms*1000.0<<" us";
    else if (ms<1000) s<<fixed<<setprecision(3)<<ms<<" ms";
    else              s<<fixed<<setprecision(3)<<ms/1000.0<<" s";
    return s.str();
}
static string fmt_kb(long kb){
    ostringstream s;
    if      (kb<1024)    s<<kb<<" KB";
    else if (kb<(1<<20)) s<<fixed<<setprecision(2)<<kb/1024.0<<" MB";
    else                 s<<fixed<<setprecision(2)<<kb/1048576.0<<" GB";
    return s.str();
}

// =============================================================================
//  SECTION 12 — Per-dataset processing pipeline
//               Timer is anchored to t_dataset_start so every dataset
//               reports elapsed time from 0.
// =============================================================================
static void process(const string& name, const string& path) {
    hline('=');
    cout<<"  DATASET : "<<name<<"\n"
        <<"  FILE    : "<<path<<"\n";
    hline('=');

    // ── Dataset-local epoch: all times below are relative to this point ───────
    auto t_dataset_start = high_resolution_clock::now();
    auto elapsed_ms = [&](high_resolution_clock::time_point tp) -> double {
        return duration_cast<microseconds>(tp - t_dataset_start).count() / 1000.0;
    };

    // Load ────────────────────────────────────────────────────────────────────
    long m0=mem_kb();
    auto t_load_begin = high_resolution_clock::now();
    Graph g=load_graph(path);
    auto t_load_end   = high_resolution_clock::now();
    long m1=mem_kb();

    double load_start_ms = elapsed_ms(t_load_begin);
    double load_end_ms   = elapsed_ms(t_load_end);
    double load_ms       = load_end_ms - load_start_ms;

    int n=g.n; long long me=g.m;
    vector<int> allv(n); iota(allv.begin(),allv.end(),0);
    long long ntri=count_tri(g,allv);

    cout<<fixed<<setprecision(4);
    cout<<"\n[1] GRAPH STATISTICS\n"; hline();
    cout<<"  Vertices                         : "<<n<<"\n"
        <<"  Edges (undirected)               : "<<me<<"\n"
        <<"  Triangles                        : "<<ntri<<"\n"
        <<"  Edge density     (|E|/|V|)       : "<<(n?(double)me/n:0.0)<<"\n"
        <<"  Triangle density (T/|V|)         : "<<(n?(double)ntri/n:0.0)<<"\n"
        <<"  Average degree   (2|E|/|V|)      : "<<(n?2.0*me/n:0.0)<<"\n"
        <<"  RSS after load                   : "<<fmt_kb(m1)
                                                 <<"  (+"<<fmt_kb(m1-m0)<<")\n";

    // CoreExact ───────────────────────────────────────────────────────────────
    cout<<"\n[2] CoreExact  ALGORITHM 4  (Psi = Triangle, h = 3)\n"; hline();
    long  ma0=mem_kb();
    auto  t_algo_begin = high_resolution_clock::now();
    auto  cer=core_exact(g);
    auto  t_algo_end   = high_resolution_clock::now();
    long  ma1=mem_kb();

    double algo_start_ms = elapsed_ms(t_algo_begin);
    double algo_end_ms   = elapsed_ms(t_algo_end);
    double algo_ms       = algo_end_ms - algo_start_ms;

    cout<<fixed<<setprecision(6);
    cout<<"  ---- Core Decomposition (Algorithm 3) ----\n"
        <<"  kmax                             : "<<cer.kmax<<"\n"
        <<"  rho'  (best residual tri-density): "<<cer.rho_prime<<"\n"
        <<"  k'    = ceil(rho')               : "<<cer.k_prime<<"\n"
        <<"  rho'' (max CC tri-density)       : "<<cer.rho_double_prime<<"\n"
        <<"  k''   = max(k', ceil(rho''))     : "<<cer.k_double_prime<<"\n"
        <<"  ---- Binary Search (Prunings 1/2/3) ----\n"
        <<"  Initial lower bound  l           : "<<cer.init_lower<<"\n"
        <<"  Initial upper bound  u = kmax    : "<<cer.init_upper<<"\n"
        <<"  Final   lower bound  l           : "<<cer.final_lower<<"\n"
        <<"  Final   upper bound  u           : "<<cer.final_upper<<"\n"
        <<"  Flow network solves              : "<<cer.flow_solves<<"\n"
        <<"  ---- Densest Subgraph (CDS) ----\n"
        <<"  CDS vertices                     : "<<cer.cds_verts.size()<<"\n"
        <<"  CDS edges                        : "<<cer.cds_edges<<"\n"
        <<"  CDS triangles                    : "<<cer.cds_triangles<<"\n"
        <<"  CDS triangle-density  (rho_opt)  : "<<cer.cds_density<<"\n"
        <<fixed<<setprecision(4)
        <<"  CDS edge-density                 : "
        <<(cer.cds_verts.empty()?0.0:(double)cer.cds_edges/cer.cds_verts.size())<<"\n";

    // Betweenness Centrality ──────────────────────────────────────────────────
    cout<<"\n[3] BETWEENNESS CENTRALITY  (Brandes, normalised, undirected)\n"; hline();

    bool   on_cds=false;
    Graph  subg;
    Graph* bcg=&g;

    const long long BC_LIMIT=50'000'000LL;
    if ((long long)n*me>BC_LIMIT && !cer.cds_verts.empty()){
        on_cds=true;
        cout<<"  V*E = "<<(long long)n*me<<" > "<<BC_LIMIT
            <<" -> BC computed on CDS subgraph ("
            <<cer.cds_verts.size()<<" vertices).\n\n";
        unordered_map<int,int> rmp;
        for (int i=0;i<(int)cer.cds_verts.size();i++) rmp[cer.cds_verts[i]]=i;
        subg.init((int)cer.cds_verts.size());
        unordered_set<int> ds(cer.cds_verts.begin(),cer.cds_verts.end());
        for (int u:cer.cds_verts)
            for (int v:g.adj[u]) if (ds.count(v)&&v>u) subg.add_edge(rmp[u],rmp[v]);
        bcg=&subg;
    }

    auto   t_bc_begin = high_resolution_clock::now();
    auto   bc=brandes_bc(*bcg);
    auto   t_bc_end   = high_resolution_clock::now();
    double bc_start_ms = elapsed_ms(t_bc_begin);
    double bc_end_ms   = elapsed_ms(t_bc_end);
    double bc_ms       = bc_end_ms - bc_start_ms;

    cout<<"  BC computation time              : "<<fmt_ms(bc_ms)<<"\n";
    if (on_cds) cout<<"  Node-IDs below are original graph IDs (within CDS).\n";

    vector<pair<double,int>> bcs; bcs.reserve(bc.size());
    for (int i=0;i<(int)bc.size();i++){
        int gid=on_cds?cer.cds_verts[i]:i;
        bcs.push_back({bc[i],gid});
    }
    sort(bcs.rbegin(),bcs.rend());

    cout<<"\n"
        <<"  "<<setw(5)<<"Rank"
        <<"  "<<setw(10)<<"Node-ID"
        <<"  "<<"Betweenness Centrality\n";
    hline('-',50);
    int top=min(20,(int)bcs.size());
    for (int i=0;i<top;i++)
        cout<<"  "<<setw(5)<<(i+1)
            <<"  "<<setw(10)<<bcs[i].second
            <<"  "<<fixed<<setprecision(8)<<bcs[i].first<<"\n";

    // Execution time (all relative to t_dataset_start = 0) ───────────────────
    cout<<"\n[4] EXECUTION TIME  (all timestamps relative to dataset start = 0)\n";
    hline();
    cout<<"  Graph load           start       : "<<fmt_ms(load_start_ms)<<"\n"
        <<"  Graph load           end         : "<<fmt_ms(load_end_ms)<<"\n"
        <<"  Graph load           duration    : "<<fmt_ms(load_ms)<<"\n"
        <<"  CoreExact            start       : "<<fmt_ms(algo_start_ms)<<"\n"
        <<"  CoreExact            end         : "<<fmt_ms(algo_end_ms)<<"\n"
        <<"  CoreExact            duration    : "<<fmt_ms(algo_ms)<<"\n"
        <<"  Betweenness (BC)     start       : "<<fmt_ms(bc_start_ms)<<"\n"
        <<"  Betweenness (BC)     end         : "<<fmt_ms(bc_end_ms)<<"\n"
        <<"  Betweenness (BC)     duration    : "<<fmt_ms(bc_ms)<<"\n"
        <<"  Total (end of BC - dataset start): "<<fmt_ms(bc_end_ms)<<"\n";

    // Space consumption ───────────────────────────────────────────────────────
    cout<<"\n[5] SPACE CONSUMPTION  (Working-Set RSS)\n"; hline();
    cout<<"  Baseline RSS                     : "<<fmt_kb(m0)<<"\n"
        <<"  After graph load    (delta)      : "<<fmt_kb(m1)
                                                 <<"  (+"<<fmt_kb(m1-m0)<<")\n"
        <<"  After CoreExact     (delta)      : "<<fmt_kb(ma1)
                                                 <<"  (+"<<fmt_kb(ma1-ma0)<<")\n"
        <<"  Peak increase vs baseline        : "<<fmt_kb(ma1-m0)<<"\n";

    hline('='); cout<<"\n";
}

// =============================================================================
//  SECTION 13 — main
//               Hardcoded to the three SNAP datasets.
//               The exe_dir() function resolves the folder containing the
//               running binary at runtime, so the .gz files just need to sit
//               next to the executable — no matter where you launch from.
// =============================================================================
int main(int /*argc*/, char* /*argv*/[]) {
    hline('=');
    cout<<"  CoreExact | Algorithm 4 | Psi=Triangle (h=3) | PVLDB 2019\n"
        <<"  Fang, Yu, Cheng, Lakshmanan, Lin\n";
    hline('='); cout<<"\n";

    // Locate the directory that holds the executable
    string dir = exe_dir();
    // Fallback: if exe_dir() returned empty (e.g. exotic OS), use current dir
    if (dir.empty()) dir = "./";

    // ── Three hardcoded SNAP datasets ─────────────────────────────────────────
    const vector<pair<string,string>> datasets = {
        { "wiki-Vote",    dir + "wiki-Vote.txt.gz"    },
        { "email-Enron",  dir + "email-Enron.txt.gz"  },
        { "as-skitter",   dir + "as-skitter.txt.gz"   },
    };

    for (auto& [name, path] : datasets) {
        // Verify the file exists before handing off to process()
        {
            ifstream chk(path, ios::binary);
            if (!chk) {
                cerr << "\n[SKIP] Cannot open: " << path
                     << "\n       Place the file next to the executable and retry.\n\n";
                continue;
            }
        }
        process(name, path);
    }

    cout << "All datasets processed.\n";
    return 0;
}