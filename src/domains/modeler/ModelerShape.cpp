// The modeller's faces: exact shapes, shading, a Wavefront reader, and how
// solids are joined.
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <mutex>
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

Path along(const std::vector<V3>& in) {
    Path p;
    for (const V3& q : in)
        if (p.points.empty() || len(q - p.points.back()) > 1e-9) p.points.push_back(q);
    // A path that ends where it began goes round: no ends, its last corner
    // mitred like the others.
    if (p.points.size() >= 4 && len(p.points.front() - p.points.back()) < 1e-6) p.points.pop_back(), p.closed = true;
    const std::size_t N = p.points.size();
    const std::size_t S = p.closed ? N : N - 1;  // segments
    if (N < 2) return p;
    const auto seg = [&](std::size_t k) { return unit(p.points[(k + 1) % N] - p.points[k]); };
    // Each segment's frame turned from the last by the least turn that takes
    // the one direction to the next: across it, nothing twists.
    p.frames.resize(S);
    p.frames[0].t = seg(0);
    p.frames[0].n = perpendicular(p.frames[0].t);
    p.frames[0].b = cross(p.frames[0].t, p.frames[0].n);
    const auto turn = [](V3 v, V3 a, V3 b) {
        const V3 ax = cross(a, b);
        const double c = dot(a, b);
        if (c < -0.999999) {
            // Back the way it came: a half turn about something across it.
            const V3 k = perpendicular(a);
            return k * (2 * dot(k, v)) - v;
        }
        return v + cross(ax, v) + cross(ax, cross(ax, v)) * (1 / (1 + c));
    };
    for (std::size_t k = 1; k <= S; ++k) {
        const Frame& f = p.frames[k - 1];
        const V3 t = seg(k == S ? 0 : k);
        if (k == S) {
            if (!p.closed) break;
            // Round a closed path, the frame comes back turned about its own
            // direction by however the path twists in space: that turn,
            // shared out along the way, so it closes.
            const V3 n_back = unit(turn(f.n, f.t, t));
            const Frame& f0 = p.frames[0];
            const double twist = std::atan2(dot(cross(n_back, f0.n), f0.t), dot(n_back, f0.n));
            double total = 0;
            for (std::size_t q = 0; q < S; ++q) total += len(p.points[(q + 1) % N] - p.points[q]);
            double run = 0;
            for (std::size_t q = 0; q < S; ++q) {
                Frame& g = p.frames[q];
                const double a = twist * run / std::max(total, 1e-12);
                const V3 n = g.n * std::cos(a) + g.b * std::sin(a);
                g.n = unit(n), g.b = cross(g.t, g.n);
                run += len(p.points[(q + 1) % N] - p.points[q]);
            }
            break;
        }
        Frame g;
        g.t = t;
        g.n = unit(turn(f.n, f.t, t));
        g.n = unit(g.n - t * dot(g.n, t));
        g.b = cross(t, g.n);
        p.frames[k] = g;
    }
    return p;
}

std::vector<V3> Path::section(std::size_t i, const std::vector<P2>& prof) const {
    const std::size_t N = points.size(), S = frames.size();
    std::vector<V3> out;
    // The frame it comes in by, and the plane of the corner (half way between
    // the way in and the way out): where the outline carried in along the way
    // in meets that plane - the mitre, which the outline carried out meets too.
    const bool first = !closed && i == 0, last = !closed && i + 1 == N;
    const Frame& in = frames[first ? 0 : (i + S - 1) % S];
    const V3 out_t = last ? in.t : frames[i % S].t;
    V3 m = unit(in.t + out_t);
    if (first || last) m = in.t;
    const double c = std::max(0.25, dot(in.t, m));  // a corner past 150 degrees is mitred no further
    for (const P2& q : prof) {
        const V3 off = in.n * q.x + in.b * q.y;
        out.push_back(points[i] + off - in.t * (dot(off, m) / c));
    }
    return out;
}

std::vector<V3> rounded_path(const std::vector<V3>& path, double r, int sides) {
    if (r <= 0 || path.size() < 3) return path;
    const bool closed = path.size() >= 4 && len(path.front() - path.back()) < 1e-6;
    const std::size_t N = closed ? path.size() - 1 : path.size();
    std::vector<V3> out;
    if (!closed) out.push_back(path[0]);
    for (std::size_t i = closed ? 0 : 1; i < (closed ? N : N - 1); ++i) {
        const V3 p = path[i], a = path[(i + N - 1) % N], b = path[(i + 1) % N];
        const V3 ti = unit(p - a), to = unit(b - p);
        const double turn = std::acos(std::clamp(dot(ti, to), -1.0, 1.0));
        if (turn < 1e-3) {
            out.push_back(p);
            continue;
        }
        // Cut back each way as far as a circle of radius r touching both
        // lines needs - no further than half of either side.
        const double d = std::min({r * std::tan(turn / 2), len(p - a) * 0.5, len(b - p) * 0.5});
        const V3 s = p - ti * d, e = p + to * d;
        const int k = std::max(2, int(std::ceil(turn / (2 * kPi) * std::max(8, sides))));
        // A curve from s to e that leaves along the way in and arrives along
        // the way out (a quadratic one: near enough a circle's arc).
        for (int j = 0; j <= k; ++j) {
            const double u = double(j) / k;
            out.push_back(s * ((1 - u) * (1 - u)) + p * (2 * u * (1 - u)) + e * (u * u));
        }
    }
    if (closed) out.push_back(out.front());
    else out.push_back(path.back());
    return out;
}

Geom g_sweep(const std::vector<P2>& prof, const std::vector<V3>& path_in, bool close_ends) {
    Geom g;
    const Path path = along(path_in);
    const std::size_t N = path.points.size(), n = prof.size();
    if (N < 2 || n < 2) return g;
    std::vector<std::vector<int>> id(N);
    for (std::size_t i = 0; i < N; ++i)
        for (const V3& q : path.section(i, prof)) id[i].push_back(add(g, q));
    // Which way round the outline runs says which way its faces look.
    double area = 0;
    for (std::size_t j = 0; j < n; ++j) area += prof[j].x * prof[(j + 1) % n].y - prof[(j + 1) % n].x * prof[j].y;
    const auto tri = [&](int a, int b, int c) {
        if (len(cross(g.p[std::size_t(b)] - g.p[std::size_t(a)], g.p[std::size_t(c)] - g.p[std::size_t(a)])) < 1e-14) return;
        if (area >= 0) g.t.insert(g.t.end(), {a, b, c});
        else g.t.insert(g.t.end(), {a, c, b});
    };
    const std::size_t rings = path.closed ? N : N - 1;
    for (std::size_t i = 0; i < rings; ++i) {
        const std::size_t i2 = (i + 1) % N;
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t j2 = (j + 1) % n;
            tri(id[i][j], id[i][j2], id[i2][j2]);
            tri(id[i][j], id[i2][j2], id[i2][j]);
        }
    }
    if (close_ends && n >= 3 && !path.closed) {
        cap(g, id.front(), path.frames.front().t * -1);
        cap(g, id.back(), path.frames.back().t);
    }
    return g;
}

void transform(Geom& g, const Mat& m) {
    for (V3& p : g.p) p = apply(m, p);
    if (det(m) < 0) {
        const bool uv = g.uv.size() == g.t.size() * 2;
        for (std::size_t i = 0; i + 2 < g.t.size(); i += 3) {
            std::swap(g.t[i + 1], g.t[i + 2]);
            // A corner's place on its picture goes with the corner.
            if (uv) std::swap(g.uv[(i + 1) * 2], g.uv[(i + 2) * 2]), std::swap(g.uv[(i + 1) * 2 + 1], g.uv[(i + 2) * 2 + 1]);
        }
    }
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
    const bool has_uv = g.uv.size() == g.t.size() * 2;
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
            double u = ay >= ax && ay >= az ? p.x : ax >= az ? p.z : p.x, v = ay >= ax && ay >= az ? p.z : p.y;
            // Faces that came with their place on a picture keep it.
            if (has_uv) u = g.uv[(f * 3 + std::size_t(c)) * 2], v = g.uv[(f * 3 + std::size_t(c)) * 2 + 1];
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
    std::vector<std::pair<float, float>> vt;
    std::vector<float> uv;
    bool any_uv = false;
    int cur = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string w;
        ls >> w;
        if (w == "v") {
            V3 p;
            ls >> p.x >> p.y >> p.z;
            v.push_back(p);
        } else if (w == "vt") {
            float a = 0, b = 0;
            ls >> a >> b;
            vt.emplace_back(a, 1.0f - b);  // a picture's rows run down
        } else if (w == "mtllib") {
            std::getline(ls >> std::ws, o.mtllib);
            while (!o.mtllib.empty() && (o.mtllib.back() == '\r' || o.mtllib.back() == ' ')) o.mtllib.pop_back();
        } else if (w == "usemtl") {
            std::string name;
            ls >> name;
            auto it = std::find(o.materials.begin(), o.materials.end(), name);
            cur = int(it - o.materials.begin());
            if (it == o.materials.end()) o.materials.push_back(name);
        } else if (w == "f") {
            std::vector<int> idx;
            std::vector<std::pair<float, float>> at;
            std::string tok;
            while (ls >> tok) {
                // v, v/vt, v//vn or v/vt/vn
                const long k = std::strtol(tok.c_str(), nullptr, 10);
                const long i = k < 0 ? long(v.size()) + k : k - 1;
                if (i < 0 || std::size_t(i) >= v.size()) continue;
                idx.push_back(int(i));
                std::pair<float, float> here{0.0f, 0.0f};
                const std::size_t slash = tok.find('/');
                if (slash != std::string::npos && slash + 1 < tok.size() && tok[slash + 1] != '/') {
                    const long q = std::strtol(tok.c_str() + slash + 1, nullptr, 10);
                    const long j = q < 0 ? long(vt.size()) + q : q - 1;
                    if (j >= 0 && std::size_t(j) < vt.size()) here = vt[std::size_t(j)], any_uv = true;
                }
                at.push_back(here);
            }
            for (std::size_t k = 1; k + 1 < idx.size(); ++k) {
                o.geom.t.insert(o.geom.t.end(), {idx[0], idx[k], idx[k + 1]});
                o.geom.mat.push_back(cur);
                for (std::size_t c : {std::size_t(0), k, k + 1}) uv.push_back(at[c].first), uv.push_back(at[c].second);
            }
        }
    }
    o.geom.p = v;
    if (any_uv) o.geom.uv = std::move(uv);
    o.ok = !v.empty() && !o.geom.t.empty();
    return o;
}

std::map<std::string, MtlMaps> read_mtl(const std::string& text) {
    std::map<std::string, MtlMaps> out;
    std::istringstream in(text);
    std::string line, cur;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string w;
        ls >> w;
        if (w == "newmtl") ls >> cur;
        else if (!cur.empty() && (w == "map_Kd" || w == "norm" || w == "map_Bump" || w == "bump" || w == "map_ORM" || w == "map_d")) {
            // The file is the last word: options (-s, -o, -bm ...) come before it.
            std::string word, last;
            while (ls >> word) last = word;
            if (last.empty()) continue;
            MtlMaps& m = out[cur];
            if (w == "map_Kd") m.colour = last;
            else if (w == "norm") m.normal = last;
            else if (w == "map_Bump" || w == "bump") {
                if (m.normal.empty()) m.normal = last;
            } else if (w == "map_ORM") m.surface = last;
            else m.cutout = true;
        }
    }
    return out;
}

// --- solids -------------------------------------------------------------------------

namespace {
// How closely what a field makes is kept when it is made fewer: a share of
// its cell (simplify's tolerance).
constexpr double kKeep = 0.06;

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
// The finest cell any of them asks for, or the modeller's.
double finest(const std::vector<const Piece*>& a, const std::vector<const Piece*>& b, double cell) {
    double r = 0;
    for (const auto* list : {&a, &b})
        for (const Piece* p : *list)
            if (p->res > 0 && (r == 0 || p->res < r)) r = p->res;
    return r > 0 ? r : cell;
}
Box grown(Box b, double pad) {
    if (b.empty()) return b;
    b.lo = b.lo - V3{pad, pad, pad}, b.hi = b.hi + V3{pad, pad, pad};
    return b;
}
Box meet(const Box& a, const Box& b) {
    Box r;
    if (a.empty() || b.empty()) return r;
    r.lo = {std::max(a.lo.x, b.lo.x), std::max(a.lo.y, b.lo.y), std::max(a.lo.z, b.lo.z)};
    r.hi = {std::min(a.hi.x, b.hi.x), std::min(a.hi.y, b.hi.y), std::min(a.hi.z, b.hi.z)};
    if (r.lo.x > r.hi.x || r.lo.y > r.hi.y || r.lo.z > r.hi.z) return Box{};
    return r;
}
double volume_of(const Box& b) { return b.empty() ? 0 : (b.hi.x - b.lo.x) * (b.hi.y - b.lo.y) * (b.hi.z - b.lo.z); }

// The cell a field is meshed at in a region, as surface_in takes it.
double cell_in(const Box& region, double cell, int max_grid) {
    const V3 e = region.hi - region.lo;
    const double mx = std::max(e.x, std::max(e.y, e.z)) + cell;
    return mx / cell > max_grid ? mx / max_grid : cell;
}

// Pieces made one where `region` is: their exact faces kept outside it,
// `field` meshed inside it, the two zipped along its face and made fewer
// where flat (what was exact stays exact). Null if they will not zip.
std::shared_ptr<const Geom> local(const std::vector<Piece>& ps, const Sdf& field, const Box& region, double cell, double crease, const Mesher& m) {
    std::vector<std::shared_ptr<const Geom>> faces;
    for (const Piece& p : ps) {
        faces.push_back(p.exact.get());
        if (!faces.back()) return nullptr;
    }
    Geom inner;
    const Box grid = surface_in(field, region, cell, m.max_grid, inner, m.warn);
    const double h = cell_in(region, cell, m.max_grid);
    Geom outer;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const Geom c = clip_outside(*faces[i], grid, ps[i].material);
        const int base = int(outer.p.size());
        outer.p.insert(outer.p.end(), c.p.begin(), c.p.end());
        for (int v : c.t) outer.t.push_back(v + base);
        outer.mat.insert(outer.mat.end(), c.mat.begin(), c.mat.end());
    }
    auto out = std::make_shared<Geom>();
    if (!zip(outer, inner, grid, h, *out)) return nullptr;
    std::vector<char> locked(out->p.size(), 0);
    std::fill(locked.begin(), locked.begin() + long(outer.p.size()), char(1));
    out->crease = crease;
    simplify(*out, kKeep * h, 0, &locked);
    return out;
}

// Pieces whose boxes overlap (padded), together: each such cluster cuts
// where it is, apart from the others.
std::vector<std::vector<const Piece*>> clusters(const Solid& s, double pad) {
    const std::size_t n = s.pieces.size();
    std::vector<int> of(n);
    for (std::size_t i = 0; i < n; ++i) of[i] = int(i);
    const std::function<int(int)> root = [&](int i) { return of[std::size_t(i)] == i ? i : of[std::size_t(i)] = root(of[std::size_t(i)]); };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            if (s.pieces[i].bb.overlaps(s.pieces[j].bb, pad)) of[std::size_t(root(int(i)))] = root(int(j));
    std::vector<std::vector<const Piece*>> out;
    std::vector<int> slot(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        const int r = root(int(i));
        if (slot[std::size_t(r)] < 0) slot[std::size_t(r)] = int(out.size()), out.emplace_back();
        out[std::size_t(slot[std::size_t(r)])].push_back(&s.pieces[i]);
    }
    return out;
}
}  // namespace

struct Faces::Lazy {
    std::function<std::shared_ptr<const Geom>()> make;
    std::once_flag once;
    std::shared_ptr<const Geom> made;
};

Faces::Faces(std::shared_ptr<const Geom> g) {
    if (!g) return;
    lazy_ = std::make_shared<Lazy>();
    std::call_once(lazy_->once, [&] { lazy_->made = std::move(g); });
}

Faces Faces::later(std::function<std::shared_ptr<const Geom>()> make) {
    Faces f;
    f.lazy_ = std::make_shared<Lazy>();
    f.lazy_->make = std::move(make);
    return f;
}

std::shared_ptr<const Geom> Faces::get() const {
    if (!lazy_) return nullptr;
    std::call_once(lazy_->once, [this] { lazy_->made = lazy_->make(), lazy_->make = nullptr; });
    return lazy_->made;
}

Faces Faces::moved(const Mat& m) const {
    if (!lazy_) return {};
    return later([was = *this, m] {
        const std::shared_ptr<const Geom> g = was.get();
        if (!g) return std::shared_ptr<const Geom>();
        auto out = std::make_shared<Geom>(*g);
        transform(*out, m);
        return std::shared_ptr<const Geom>(out);
    });
}

std::shared_ptr<const Geom> mesh_whole(const Sdf& f, double cell, double crease, const Mesher& m) {
    auto g = std::make_shared<Geom>();
    surface(f, cell, m.max_grid, *g, m.warn);
    g->crease = crease;
    simplify(*g, kKeep * cell, 0, nullptr);
    return g;
}

Solid moved(const Solid& s, const Mat& m) {
    Solid r;
    for (const Piece& p : s.pieces) {
        Piece q = p;
        q.exact = p.exact.moved(m);
        q.field = sdf_xform(p.field, m);
        q.bb = p.bb.moved(m);
        r.pieces.push_back(std::move(q));
    }
    const V3 o = apply(m, {0, 0, 0});
    for (Hole h : s.holes) {
        h.at = apply(m, h.at);
        h.across = apply(m, h.across) - o, h.up = apply(m, h.up) - o, h.facing = apply(m, h.facing) - o;
        r.holes.push_back(std::move(h));
    }
    // A turn's axis goes with the solid, and a mirror turns it the other way
    // round (what turns right-handed in a mirror turns left-handed).
    const bool flips = det(m) < 0;
    for (Hinge j : s.joints) {
        j.at = apply(m, j.at);
        const V3 ax = apply(m, j.axis) - o;
        if (j.slide) j.lo *= len(ax), j.hi *= len(ax);
        j.axis = unit(ax) * (flips && !j.slide ? -1.0 : 1.0);
        for (V3& p : j.path) p = apply(m, p);
        const double scale = std::cbrt(std::abs(det(m)));
        j.from *= scale, j.span *= scale;
        if (!j.path.empty()) j.lo *= scale, j.hi *= scale;
        r.joints.push_back(std::move(j));
    }
    return r;
}

Solid unite(const Solid& a, const Solid& b) {
    Solid r = a;
    r.pieces.insert(r.pieces.end(), b.pieces.begin(), b.pieces.end());
    r.holes.insert(r.holes.end(), b.holes.begin(), b.holes.end());
    r.joints.insert(r.joints.end(), b.joints.begin(), b.joints.end());
    return r;
}

void join(Solid& into, const Solid& s, Mode mode, double k, const Mesher& mm) {
    if (mode == Mode::Add) {
        into = unite(into, s);
        return;
    }
    if (s.empty() && mode != Mode::And) return;
    if (mode == Mode::Sub || mode == Mode::Carve) {
        // Each cluster of the cutter cuts where it is: the pieces it touches
        // keep their exact faces but in a box round it, and are meshed from
        // their field only in that box.
        const double kk = mode == Mode::Carve ? k : 0;
        for (const std::vector<const Piece*>& cut : clusters(s, kk + 2 * mm.cell)) {
            Box cb;
            for (const Piece* p : cut) cb.add(p->bb);
            const SdfP cutter = field_of(cut);
            Solid rest;
            rest.holes = into.holes;
            rest.joints = into.joints;
            std::vector<const Piece*> fields;
            for (const Piece& p : into.pieces) {
                if (!p.bb.overlaps(cb, kk)) {
                    rest.pieces.push_back(p);
                    continue;
                }
                if (!p.exact.known()) {
                    fields.push_back(&p);
                    continue;
                }
                const double h = finest({&p}, cut, mm.cell);
                Piece q = p;
                q.field = sdf_sub(p.field, cutter, kk);
                // Its cell is the cut's: what will not zip is meshed whole at it.
                q.res = h;
                const Box whole = grown(p.bb, 3 * h), region = meet(grown(cb, kk + 2 * h), whole);
                q.exact = Faces{};
                if (volume_of(region) < 0.6 * volume_of(whole)) {
                    const SdfP field = q.field;
                    const double crease = p.crease;
                    q.exact = Faces::later([was = p, field, region, h, crease, mm] { return local({was}, *field, region, h, crease, mm); });
                }
                rest.pieces.push_back(std::move(q));
            }
            if (!fields.empty()) {
                // Pieces that were a field already: one field, as they were.
                Piece m = merged(fields, sdf_sub(field_of(fields), cutter, kk));
                rest.pieces.push_back(std::move(m));
            }
            into = std::move(rest);
        }
        return;
    }
    Box cb;
    std::vector<const Piece*> cut;
    for (const Piece& p : s.pieces) cut.push_back(&p), cb.add(p.bb);
    const SdfP cutter = s.empty() ? nullptr : field_of(cut);
    std::vector<const Piece*> over;
    Solid rest;
    rest.holes = into.holes;
    rest.joints = into.joints;
    for (const Piece& p : into.pieces) {
        if (mode == Mode::And ? p.bb.overlaps(cb, 0) : p.bb.overlaps(cb, k)) over.push_back(&p);
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
        Box bb;
        for (const Piece* p : bo) bb.add(p->bb);
        Piece m = merged(over, sdf_blend(field_of(over), field_of(bo), k));
        for (const Piece* p : bo) m.bb.add(p->bb);
        m.bb.lo = m.bb.lo - V3{k, k, k}, m.bb.hi = m.bb.hi + V3{k, k, k};
        // Where the two meet, and only there, the smooth union is meshed:
        // apart from there it is the plain union of their exact faces.
        bool exact = true;
        for (const auto* list : {&over, &bo})
            for (const Piece* p : *list) exact = exact && p->exact.known();
        m.res = finest(over, bo, mm.cell);
        if (exact) {
            const double h = m.res;
            std::vector<Piece> all;
            for (const auto* list : {&over, &bo})
                for (const Piece* p : *list) all.push_back(*p);
            const Box whole = grown(m.bb, 3 * h), region = meet(grown(meet(grown(ab, k), grown(bb, k)), 2 * h), whole);
            if (!region.empty() && volume_of(region) < 0.6 * volume_of(whole)) {
                const SdfP field = m.field;
                const double crease = m.crease;
                m.exact = Faces::later([all, field, region, h, crease, mm] { return local(all, *field, region, h, crease, mm); });
            }
        }
        into = rest;
        into.pieces.push_back(std::move(m));
        into = unite(into, bother);
        into.holes.insert(into.holes.end(), s.holes.begin(), s.holes.end());
        into.joints.insert(into.joints.end(), s.joints.begin(), s.joints.end());
        return;
    }
    Piece m = merged(over, sdf_and(field_of(over), cutter));
    m.bb.lo = {std::max(m.bb.lo.x, cb.lo.x), std::max(m.bb.lo.y, cb.lo.y), std::max(m.bb.lo.z, cb.lo.z)};
    m.bb.hi = {std::min(m.bb.hi.x, cb.hi.x), std::min(m.bb.hi.y, cb.hi.y), std::min(m.bb.hi.z, cb.hi.z)};
    into = rest;
    into.pieces.push_back(std::move(m));
}

}  // namespace sg::sculpt::kernel
