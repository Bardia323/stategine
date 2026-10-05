// Fewer faces where a surface says little: edges collapsed by the error they
// make against the planes the surface was made of (Garland and Heckbert's
// quadrics), one end moved onto the other - so every point left is a point
// the surface had - cheapest first. What it keeps: the exact faces a cut left
// (locked), open edges, the borders of materials, faces the right way out,
// and the surface as closed as it was.
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>

#include "ModelerKernel.hpp"

namespace sg::sculpt::kernel {

namespace {

// A sum of planes: the squared distance from them all of a point, as
// p' A p + 2 b.p + c.
struct Quadric {
    double a[6] = {0, 0, 0, 0, 0, 0};  // xx xy xz yy yz zz
    double b[3] = {0, 0, 0};
    double c = 0;
    void plane(V3 n, double d) {
        a[0] += n.x * n.x, a[1] += n.x * n.y, a[2] += n.x * n.z, a[3] += n.y * n.y, a[4] += n.y * n.z, a[5] += n.z * n.z;
        b[0] += n.x * d, b[1] += n.y * d, b[2] += n.z * d;
        c += d * d;
    }
    void add(const Quadric& q) {
        for (int i = 0; i < 6; ++i) a[i] += q.a[i];
        for (int i = 0; i < 3; ++i) b[i] += q.b[i];
        c += q.c;
    }
    double at(V3 p) const {
        return a[0] * p.x * p.x + 2 * a[1] * p.x * p.y + 2 * a[2] * p.x * p.z + a[3] * p.y * p.y + 2 * a[4] * p.y * p.z + a[5] * p.z * p.z +
               2 * (b[0] * p.x + b[1] * p.y + b[2] * p.z) + c;
    }
};

struct Key3 {
    long long x, y, z;
    bool operator==(const Key3& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct Hash3 {
    std::size_t operator()(const Key3& k) const { return std::size_t(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL); }
};
Key3 key_of(V3 p) { return {std::llround(p.x * 1e7), std::llround(p.y * 1e7), std::llround(p.z * 1e7)}; }

struct Candidate {
    double cost;
    int u, v;
    unsigned su, sv;
    bool operator>(const Candidate& o) const {
        if (cost != o.cost) return cost > o.cost;
        if (u != o.u) return u > o.u;
        return v > o.v;
    }
};

}  // namespace

void weld(Geom& g) {
    std::unordered_map<Key3, int, Hash3> at;
    std::vector<V3> p;
    std::vector<int> id(g.p.size());
    for (std::size_t i = 0; i < g.p.size(); ++i) {
        auto it = at.emplace(key_of(g.p[i]), int(p.size()));
        if (it.second) p.push_back(g.p[i]);
        id[i] = it.first->second;
    }
    std::vector<int> t;
    std::vector<int> mat;
    for (std::size_t f = 0; f + 2 < g.t.size(); f += 3) {
        const int a = id[std::size_t(g.t[f])], b = id[std::size_t(g.t[f + 1])], c = id[std::size_t(g.t[f + 2])];
        if (a == b || b == c || a == c) continue;
        t.insert(t.end(), {a, b, c});
        if (!g.mat.empty()) mat.push_back(g.mat[f / 3]);
    }
    g.p = std::move(p), g.t = std::move(t), g.mat = std::move(mat);
}

void simplify(Geom& g, double tol, std::size_t target, const std::vector<char>* locked_in) {
    // Points at one place are one point (a lock on any of them locks it).
    std::vector<char> lock_of(g.p.size(), 0);
    if (locked_in)
        for (std::size_t i = 0; i < g.p.size() && i < locked_in->size(); ++i) lock_of[i] = (*locked_in)[i];
    {
        std::unordered_map<Key3, int, Hash3> at;
        std::vector<int> id(g.p.size());
        std::vector<V3> p;
        std::vector<char> l;
        for (std::size_t i = 0; i < g.p.size(); ++i) {
            auto it = at.emplace(key_of(g.p[i]), int(p.size()));
            if (it.second) p.push_back(g.p[i]), l.push_back(0);
            id[i] = it.first->second;
            l[std::size_t(id[i])] = char(l[std::size_t(id[i])] | lock_of[i]);
        }
        for (int& v : g.t) v = id[std::size_t(v)];
        g.p = std::move(p);
        lock_of = std::move(l);
    }
    const std::size_t nv = g.p.size(), nf = g.t.size() / 3;
    if (nf < 8) return;
    std::vector<std::array<int, 3>> F(nf);
    std::vector<char> alive(nf, 1);
    std::vector<int> mat(nf, 0);
    for (std::size_t f = 0; f < nf; ++f) {
        F[f] = {g.t[f * 3], g.t[f * 3 + 1], g.t[f * 3 + 2]};
        if (!g.mat.empty()) mat[f] = g.mat[f];
        if (F[f][0] == F[f][1] || F[f][1] == F[f][2] || F[f][0] == F[f][2]) alive[f] = 0;
    }
    std::vector<std::vector<int>> around(nv);
    for (std::size_t f = 0; f < nf; ++f)
        if (alive[f])
            for (int c : F[f]) around[std::size_t(c)].push_back(int(f));
    // Locked too: the ends of an edge that is not met by exactly two faces
    // (an open rim, or worse), and where materials meet.
    {
        std::map<std::pair<int, int>, int> uses;
        for (std::size_t f = 0; f < nf; ++f)
            if (alive[f])
                for (int k = 0; k < 3; ++k) {
                    const int a = F[f][std::size_t(k)], b = F[f][std::size_t((k + 1) % 3)];
                    ++uses[{std::min(a, b), std::max(a, b)}];
                }
        for (const auto& [e, n] : uses)
            if (n != 2) lock_of[std::size_t(e.first)] = lock_of[std::size_t(e.second)] = 1;
        for (std::size_t v = 0; v < nv; ++v)
            for (int f : around[v])
                if (mat[std::size_t(f)] != mat[std::size_t(around[v][0])]) lock_of[v] = 1;
    }
    const auto normal = [&](V3 a, V3 b, V3 c) { return cross(b - a, c - a); };
    std::vector<Quadric> Q(nv);
    for (std::size_t f = 0; f < nf; ++f) {
        if (!alive[f]) continue;
        const V3 n = normal(g.p[std::size_t(F[f][0])], g.p[std::size_t(F[f][1])], g.p[std::size_t(F[f][2])]);
        if (len(n) < 1e-20) continue;
        const V3 u = unit(n);
        const double d = -dot(u, g.p[std::size_t(F[f][0])]);
        for (int c : F[f]) Q[std::size_t(c)].plane(u, d);
    }
    std::vector<unsigned> stamp(nv, 0);
    std::vector<char> gone(nv, 0);
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
    const auto neighbours = [&](int v, std::vector<int>& out) {
        out.clear();
        for (int f : around[std::size_t(v)])
            if (alive[std::size_t(f)])
                for (int c : F[std::size_t(f)])
                    if (c != v) out.push_back(c);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    const auto offer = [&](int u, int v) {
        if (lock_of[std::size_t(u)] || gone[std::size_t(u)] || gone[std::size_t(v)]) return;
        Quadric q = Q[std::size_t(u)];
        q.add(Q[std::size_t(v)]);
        heap.push({std::max(0.0, q.at(g.p[std::size_t(v)])), u, v, stamp[std::size_t(u)], stamp[std::size_t(v)]});
    };
    std::vector<int> nb, nb2;
    for (std::size_t v = 0; v < nv; ++v) {
        neighbours(int(v), nb);
        for (int w : nb) offer(int(v), w);
    }
    const double limit = tol > 0 ? tol * tol : 1e300;
    std::size_t faces = 0;
    for (char a : alive) faces += std::size_t(a);
    while (!heap.empty()) {
        if (target > 0 && faces <= target) break;
        const Candidate c = heap.top();
        heap.pop();
        if (c.cost > limit) break;
        const int u = c.u, v = c.v;
        if (gone[std::size_t(u)] || gone[std::size_t(v)] || stamp[std::size_t(u)] != c.su || stamp[std::size_t(v)] != c.sv) continue;
        // The faces on the edge: exactly two, and the only neighbours u and
        // v share are their third corners (else the collapse would pinch).
        std::vector<int> both, third;
        for (int f : around[std::size_t(u)]) {
            if (!alive[std::size_t(f)]) continue;
            const auto& t = F[std::size_t(f)];
            if (t[0] == v || t[1] == v || t[2] == v) {
                both.push_back(f);
                for (int q : t)
                    if (q != u && q != v) third.push_back(q);
            }
        }
        if (both.size() != 2) continue;
        neighbours(u, nb);
        neighbours(v, nb2);
        std::vector<int> common;
        std::set_intersection(nb.begin(), nb.end(), nb2.begin(), nb2.end(), std::back_inserter(common));
        std::sort(third.begin(), third.end());
        if (common != third) continue;
        // No face turned over, or squeezed to nothing.
        bool ok = true;
        for (int f : around[std::size_t(u)]) {
            if (!alive[std::size_t(f)]) continue;
            const auto& t = F[std::size_t(f)];
            if (t[0] == v || t[1] == v || t[2] == v) continue;
            V3 p[3], q[3];
            for (int k = 0; k < 3; ++k) {
                p[k] = g.p[std::size_t(t[std::size_t(k)])];
                q[k] = t[std::size_t(k)] == u ? g.p[std::size_t(v)] : p[k];
            }
            const V3 n0 = normal(p[0], p[1], p[2]), n1 = normal(q[0], q[1], q[2]);
            const double l0 = len(n0), l1 = len(n1);
            if (l1 < 1e-14 || l1 < l0 * 1e-3 || dot(n0, n1) < 0.25 * l0 * l1) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        // Nor a corner that too many faces meet at: thin slivers, all of them.
        std::vector<int> joined = nb;
        joined.insert(joined.end(), nb2.begin(), nb2.end());
        std::sort(joined.begin(), joined.end());
        joined.erase(std::unique(joined.begin(), joined.end()), joined.end());
        if (joined.size() > 14) continue;
        for (int f : both) alive[std::size_t(f)] = 0, --faces;
        for (int f : around[std::size_t(u)]) {
            if (!alive[std::size_t(f)]) continue;
            for (int& q : F[std::size_t(f)])
                if (q == u) q = v;
            around[std::size_t(v)].push_back(f);
        }
        around[std::size_t(u)].clear();
        auto& av = around[std::size_t(v)];
        av.erase(std::remove_if(av.begin(), av.end(), [&](int f) { return !alive[std::size_t(f)]; }), av.end());
        gone[std::size_t(u)] = 1;
        Q[std::size_t(v)].add(Q[std::size_t(u)]);
        // Only what v's edges cost has moved; whatever else is in the heap is
        // looked at again when it comes up.
        ++stamp[std::size_t(v)];
        neighbours(v, nb);
        for (int w : nb) {
            auto& aw = around[std::size_t(w)];
            aw.erase(std::remove_if(aw.begin(), aw.end(), [&](int f) { return !alive[std::size_t(f)]; }), aw.end());
            offer(v, w);
            offer(w, v);
        }
    }
    // What is left, its points renumbered.
    std::vector<int> id(nv, -1);
    std::vector<V3> p;
    std::vector<int> t, m;
    for (std::size_t f = 0; f < nf; ++f) {
        if (!alive[f]) continue;
        for (int c : F[f]) {
            if (id[std::size_t(c)] < 0) id[std::size_t(c)] = int(p.size()), p.push_back(g.p[std::size_t(c)]);
            t.push_back(id[std::size_t(c)]);
        }
        if (!g.mat.empty()) m.push_back(mat[f]);
    }
    g.p = std::move(p), g.t = std::move(t), g.mat = std::move(m);
}

}  // namespace sg::sculpt::kernel
