// The modeller's faces: exact shapes, shading, a Wavefront reader, and how
// solids are joined.
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

#include "ModelerKernel.hpp"
#include "sg/domains/Shapes.hpp"

namespace sg::sculpt::kernel {

namespace {
constexpr double kPi = 3.14159265358979323846;

// A face, wound so it looks along `out` (the way the thing's outside lies).
void face(Geom& g, int a, int b, int c, V3 out) {
    const V3 n = cross(g.p[std::size_t(b)] - g.p[std::size_t(a)], g.p[std::size_t(c)] - g.p[std::size_t(a)]);
    if (len(n) < 1e-14) return;
    if (dot(n, out) < 0) std::swap(b, c);
    g.t.insert(g.t.end(), {a, b, c});
}
void quad_face(Geom& g, int a, int b, int c, int d, V3 out) {
    face(g, a, b, c, out);
    face(g, a, c, d, out);
}
int add(Geom& g, V3 p) {
    g.p.push_back(p);
    return int(g.p.size()) - 1;
}
std::vector<shapes::P2> sp(const std::vector<P2>& v) {
    std::vector<shapes::P2> o;
    for (const P2& q : v) o.push_back({q.x, q.y});
    return o;
}
// A ring's faces, closed with a cap whose outside lies along `out`.
void cap(Geom& g, const std::vector<int>& ring, V3 out) {
    std::vector<shapes::P2> flat;
    // Flatten on the plane of the ring, by its two longest axes.
    V3 n = cross(g.p[std::size_t(ring[1])] - g.p[std::size_t(ring[0])], g.p[std::size_t(ring[2])] - g.p[std::size_t(ring[0])]);
    const V3 u = unit(g.p[std::size_t(ring[1])] - g.p[std::size_t(ring[0])]);
    const V3 w = unit(cross(n, u));
    for (int i : ring) flat.push_back({dot(g.p[std::size_t(i)], u), dot(g.p[std::size_t(i)], w)});
    const auto ears = shapes::detail::ears(shapes::detail::area(flat) >= 0 ? flat : std::vector<shapes::P2>(flat.rbegin(), flat.rend()));
    const bool rev = shapes::detail::area(flat) < 0;
    for (std::size_t k = 0; k + 2 < ears.size() + 0; k += 3) {
        const auto at = [&](int e) { return ring[std::size_t(rev ? int(ring.size()) - 1 - e : e)]; };
        face(g, at(ears[k]), at(ears[k + 1]), at(ears[k + 2]), out);
    }
}
}  // namespace

Geom g_box(V3 s) {
    Geom g;
    const V3 h = s * 0.5;
    for (int i = 0; i < 8; ++i) add(g, {i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z});
    const int f[6][4] = {{1, 3, 7, 5}, {0, 2, 6, 4}, {2, 3, 7, 6}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 1, 3, 2}};
    const V3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 6; ++i) quad_face(g, f[i][0], f[i][1], f[i][2], f[i][3], n[i]);
    return g;
}

Geom g_lathe(const std::vector<P2>& prof, int sides) {
    Geom g;
    sides = std::max(3, sides);
    std::vector<std::vector<int>> rings;
    for (const P2& q : prof) {
        std::vector<int> r;
        if (q.x < 1e-9) r.assign(std::size_t(sides), add(g, {0, q.y, 0}));
        else
            for (int j = 0; j < sides; ++j) {
                const double a = 2 * kPi * j / sides;
                r.push_back(add(g, {q.x * std::cos(a), q.y, q.x * std::sin(a)}));
            }
        rings.push_back(r);
    }
    for (std::size_t i = 0; i + 1 < rings.size(); ++i)
        for (int j = 0; j < sides; ++j) {
            const int j2 = (j + 1) % sides;
            const double a = 2 * kPi * (j + 0.5) / sides;
            const double slope = (prof[i + 1].y - prof[i].y);
            const double dr = prof[i + 1].x - prof[i].x;
            // outward: radial where it rises, tilted by the profile's lean
            const V3 out{std::cos(a) * slope, -dr, std::sin(a) * slope};
            const V3 hint = len(out) > 1e-12 ? out : V3{std::cos(a), 0, std::sin(a)};
            const int A = rings[i][std::size_t(j)], Bv = rings[i][std::size_t(j2)], C = rings[i + 1][std::size_t(j2)], D = rings[i + 1][std::size_t(j)];
            if (A != Bv) face(g, A, Bv, C, hint);
            if (C != D) face(g, A, C, D, hint);
        }
    if (!prof.empty() && prof.front().x > 1e-9) {
        const int c = add(g, {0, prof.front().y, 0});
        for (int j = 0; j < sides; ++j) face(g, c, rings.front()[std::size_t(j)], rings.front()[std::size_t((j + 1) % sides)], {0, -1, 0});
    }
    if (!prof.empty() && prof.back().x > 1e-9) {
        const int c = add(g, {0, prof.back().y, 0});
        for (int j = 0; j < sides; ++j) face(g, c, rings.back()[std::size_t(j)], rings.back()[std::size_t((j + 1) % sides)], {0, 1, 0});
    }
    return g;
}

Geom g_torus(double R, double r, int sides, int ring) {
    Geom g;
    sides = std::max(3, sides), ring = std::max(3, ring);
    std::vector<int> id;
    for (int i = 0; i < sides; ++i)
        for (int j = 0; j < ring; ++j) {
            const double a = 2 * kPi * i / sides, b = 2 * kPi * j / ring;
            id.push_back(add(g, {(R + r * std::cos(b)) * std::cos(a), r * std::sin(b), (R + r * std::cos(b)) * std::sin(a)}));
        }
    for (int i = 0; i < sides; ++i)
        for (int j = 0; j < ring; ++j) {
            const int i2 = (i + 1) % sides, j2 = (j + 1) % ring;
            const double a = 2 * kPi * (i + 0.5) / sides, b = 2 * kPi * (j + 0.5) / ring;
            const V3 out{std::cos(b) * std::cos(a), std::sin(b), std::cos(b) * std::sin(a)};
            quad_face(g, id[std::size_t(i * ring + j)], id[std::size_t(i2 * ring + j)], id[std::size_t(i2 * ring + j2)], id[std::size_t(i * ring + j2)], out);
        }
    return g;
}

Geom g_extrude(const std::vector<P2>& o, double depth, double chamfer) {
    const std::vector<float> v = shapes::extrude(sp(o), depth, chamfer);
    Geom g;
    for (std::size_t i = 0; i + 7 < v.size(); i += 8) {
        g.t.push_back(int(g.p.size()));
        g.p.push_back({v[i], v[i + 1], v[i + 2]});
    }
    return g;
}

Geom g_loft(const std::vector<std::vector<P2>>& rings, double h) {
    Geom g;
    const std::size_t R = rings.size();
    std::vector<std::vector<int>> id(R);
    for (std::size_t k = 0; k < R; ++k)
        for (const P2& q : rings[k]) id[k].push_back(add(g, {q.x, h * double(k) / double(R - 1), q.y}));
    for (std::size_t k = 0; k + 1 < R; ++k) {
        const std::size_t n = id[k].size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            const V3 c = (g.p[std::size_t(id[k][i])] + g.p[std::size_t(id[k][j])] + g.p[std::size_t(id[k + 1][i])] + g.p[std::size_t(id[k + 1][j])]) * 0.25;
            V3 mid;
            for (int q : id[k]) mid = mid + g.p[std::size_t(q)];
            mid = mid * (1.0 / double(n));
            const V3 out{c.x - mid.x, 0, c.z - mid.z};
            quad_face(g, id[k][i], id[k][j], id[k + 1][j], id[k + 1][i], out);
        }
    }
    cap(g, id.front(), {0, -1, 0});
    cap(g, id.back(), {0, 1, 0});
    return g;
}

Geom g_sweep(const std::vector<P2>& prof, const std::vector<V3>& path, bool close_ends) {
    Geom g;
    const std::size_t N = path.size(), n = prof.size();
    std::vector<std::vector<int>> id(N);
    V3 prev_n;
    for (std::size_t i = 0; i < N; ++i) {
        V3 t = unit(i == 0 ? path[1] - path[0] : i + 1 == N ? path[i] - path[i - 1] : unit(path[i] - path[i - 1]) + unit(path[i + 1] - path[i]));
        V3 nrm = i == 0 ? perpendicular(t) : unit(prev_n - t * dot(prev_n, t));
        prev_n = nrm;
        const V3 b = cross(t, nrm);
        // At a bend the section is widened so its width across the path holds.
        double widen = 1;
        if (i > 0 && i + 1 < N) widen = 1 / std::max(0.4, dot(t, unit(path[i + 1] - path[i])));
        for (const P2& q : prof) id[i].push_back(add(g, path[i] + nrm * (q.x * widen) + b * q.y));
    }
    for (std::size_t i = 0; i + 1 < N; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t j2 = (j + 1) % n;
            V3 mid;
            for (int q : id[i]) mid = mid + g.p[std::size_t(q)];
            mid = mid * (1.0 / double(n));
            const V3 c = (g.p[std::size_t(id[i][j])] + g.p[std::size_t(id[i][j2])]) * 0.5;
            quad_face(g, id[i][j], id[i][j2], id[i + 1][j2], id[i + 1][j], c - mid);
        }
    if (close_ends && n >= 3) {
        cap(g, id.front(), unit(path[0] - path[1]));
        cap(g, id.back(), unit(path[N - 1] - path[N - 2]));
    }
    return g;
}

void transform(Geom& g, const Mat& m) {
    for (V3& p : g.p) p = apply(m, p);
    if (det(m) < 0)
        for (std::size_t i = 0; i + 2 < g.t.size(); i += 3) std::swap(g.t[i + 1], g.t[i + 2]);
}

// --- shading ------------------------------------------------------------------------

void shade(const Geom& g, std::vector<std::vector<float>>& out, int default_material) {
    const std::size_t nf = g.t.size() / 3;
    struct Key {
        long long x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct H {
        std::size_t operator()(const Key& k) const { return std::size_t(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL); }
    };
    std::unordered_map<Key, int, H> weld;
    std::vector<int> wid(g.p.size());
    for (std::size_t i = 0; i < g.p.size(); ++i) {
        const Key k{std::llround(g.p[i].x * 1e5), std::llround(g.p[i].y * 1e5), std::llround(g.p[i].z * 1e5)};
        wid[i] = weld.emplace(k, int(weld.size())).first->second;
    }
    std::vector<V3> fn(nf);
    std::vector<std::vector<int>> around(weld.size());
    for (std::size_t f = 0; f < nf; ++f) {
        const auto P = [&](int c) { return g.p[std::size_t(g.t[f * 3 + std::size_t(c)])]; };
        fn[f] = cross(P(1) - P(0), P(2) - P(0));
        if (len(fn[f]) < 1e-14) continue;
        for (int c = 0; c < 3; ++c) around[std::size_t(wid[std::size_t(g.t[f * 3 + std::size_t(c)])])].push_back(int(f));
    }
    const double cc = std::cos(g.crease * kPi / 180);
    for (std::size_t f = 0; f < nf; ++f) {
        const double l = len(fn[f]);
        if (l < 1e-14) continue;
        const V3 nf0 = fn[f] * (1 / l);
        const int m = g.mat.empty() ? default_material : g.mat[f];
        if (std::size_t(m) >= out.size()) out.resize(std::size_t(m) + 1);
        for (int c = 0; c < 3; ++c) {
            const int vi = g.t[f * 3 + std::size_t(c)];
            V3 sum;
            for (int o : around[std::size_t(wid[std::size_t(vi)])]) {
                const double lo = len(fn[std::size_t(o)]);
                if (dot(fn[std::size_t(o)] * (1 / lo), nf0) >= cc) sum = sum + fn[std::size_t(o)] * (1 / lo) * lo;
            }
            const V3 n = len(sum) > 1e-12 ? unit(sum) : nf0;
            const V3 p = g.p[std::size_t(vi)];
            const double ax = std::abs(nf0.x), ay = std::abs(nf0.y), az = std::abs(nf0.z);
            const double u = ay >= ax && ay >= az ? p.x : ax >= az ? p.z : p.x, v = ay >= ax && ay >= az ? p.z : p.y;
            out[std::size_t(m)].insert(out[std::size_t(m)].end(), {float(p.x), float(p.y), float(p.z), float(n.x), float(n.y), float(n.z), float(u), float(v)});
        }
    }
}

// --- Wavefront ------------------------------------------------------------------------

Obj read_obj(const std::string& text) {
    Obj o;
    o.materials.push_back("");
    std::istringstream in(text);
    std::string line;
    std::vector<V3> v;
    int cur = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string w;
        ls >> w;
        if (w == "v") {
            V3 p;
            ls >> p.x >> p.y >> p.z;
            v.push_back(p);
        } else if (w == "usemtl") {
            std::string name;
            ls >> name;
            auto it = std::find(o.materials.begin(), o.materials.end(), name);
            cur = int(it - o.materials.begin());
            if (it == o.materials.end()) o.materials.push_back(name);
        } else if (w == "f") {
            std::vector<int> idx;
            std::string tok;
            while (ls >> tok) {
                const long k = std::strtol(tok.c_str(), nullptr, 10);
                const long i = k < 0 ? long(v.size()) + k : k - 1;
                if (i >= 0 && std::size_t(i) < v.size()) idx.push_back(int(i));
            }
            for (std::size_t k = 1; k + 1 < idx.size(); ++k) {
                o.geom.t.insert(o.geom.t.end(), {idx[0], idx[k], idx[k + 1]});
                o.geom.mat.push_back(cur);
            }
        }
    }
    o.geom.p = v;
    o.ok = !v.empty() && !o.geom.t.empty();
    return o;
}

// --- solids -------------------------------------------------------------------------

namespace {
SdfP field_of(const std::vector<const Piece*>& ps) {
    std::vector<SdfP> f;
    for (const Piece* p : ps) f.push_back(p->field);
    return sdf_union(f);
}
Piece merged(const std::vector<const Piece*>& ps, SdfP field) {
    Piece r;
    r.field = std::move(field);
    for (const Piece* p : ps) {
        r.bb.add(p->bb);
        if (p->res > 0 && (r.res == 0 || p->res < r.res)) r.res = p->res;
    }
    r.material = ps.empty() ? 0 : ps[0]->material;
    r.crease = ps.empty() ? 40.0 : ps[0]->crease;
    return r;
}
}  // namespace

Solid moved(const Solid& s, const Mat& m) {
    Solid r;
    for (const Piece& p : s.pieces) {
        Piece q = p;
        if (p.exact) {
            auto g = std::make_shared<Geom>(*p.exact);
            transform(*g, m);
            q.exact = g;
        }
        q.field = sdf_xform(p.field, m);
        q.bb = p.bb.moved(m);
        r.pieces.push_back(std::move(q));
    }
    return r;
}

Solid unite(const Solid& a, const Solid& b) {
    Solid r = a;
    r.pieces.insert(r.pieces.end(), b.pieces.begin(), b.pieces.end());
    return r;
}

void join(Solid& into, const Solid& s, Mode mode, double k) {
    if (s.empty() && mode != Mode::And) return;
    if (mode == Mode::Add) {
        into = unite(into, s);
        return;
    }
    Box cb;
    std::vector<const Piece*> cut;
    for (const Piece& p : s.pieces) cut.push_back(&p), cb.add(p.bb);
    const SdfP cutter = s.empty() ? nullptr : field_of(cut);
    std::vector<const Piece*> over;
    Solid rest;
    const double pad = mode == Mode::Blend || mode == Mode::Carve ? k : 0;
    for (const Piece& p : into.pieces) {
        if (mode == Mode::And ? p.bb.overlaps(cb, 0) : p.bb.overlaps(cb, pad)) over.push_back(&p);
        else if (mode != Mode::And) rest.pieces.push_back(p);
    }
    if (mode == Mode::And && s.empty()) {
        into.pieces.clear();
        return;
    }
    if (over.empty() && mode != Mode::Blend) {
        into = rest;
        return;
    }
    if (mode == Mode::Blend) {
        Box ab;
        for (const Piece* p : over) ab.add(p->bb);
        std::vector<const Piece*> bo;
        Solid bother;
        for (const Piece& p : s.pieces) {
            if (p.bb.overlaps(ab, k)) bo.push_back(&p);
            else bother.pieces.push_back(p);
        }
        if (over.empty() || bo.empty()) {
            into = unite(into, s);
            return;
        }
        Piece m = merged(over, sdf_blend(field_of(over), field_of(bo), k));
        for (const Piece* p : bo) m.bb.add(p->bb);
        m.bb.lo = m.bb.lo - V3{k, k, k}, m.bb.hi = m.bb.hi + V3{k, k, k};
        into = rest;
        into.pieces.push_back(std::move(m));
        into = unite(into, bother);
        return;
    }
    Piece m = merged(over, mode == Mode::And ? sdf_and(field_of(over), cutter) : sdf_sub(field_of(over), cutter, mode == Mode::Carve ? k : 0));
    if (mode == Mode::And) {
        m.bb.lo = {std::max(m.bb.lo.x, cb.lo.x), std::max(m.bb.lo.y, cb.lo.y), std::max(m.bb.lo.z, cb.lo.z)};
        m.bb.hi = {std::min(m.bb.hi.x, cb.hi.x), std::min(m.bb.hi.y, cb.hi.y), std::min(m.bb.hi.z, cb.hi.z)};
    }
    into = rest;
    into.pieces.push_back(std::move(m));
}

}  // namespace sg::sculpt::kernel
