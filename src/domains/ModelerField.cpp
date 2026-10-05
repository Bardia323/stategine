// The modeller's fields: the distance to a shape, and the surface of a field.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "ModelerKernel.hpp"

namespace sg::sculpt::kernel {

// --- points and moves ------------------------------------------------------------

V3 apply(const Mat& a, V3 p) {
    return {a.m[0] * p.x + a.m[1] * p.y + a.m[2] * p.z + a.t.x, a.m[3] * p.x + a.m[4] * p.y + a.m[5] * p.z + a.t.y,
            a.m[6] * p.x + a.m[7] * p.y + a.m[8] * p.z + a.t.z};
}

Mat operator*(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    r.t = apply(a, b.t);
    return r;
}

double det(const Mat& a) {
    const double* m = a.m;
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

Mat inverse(const Mat& a) {
    const double* m = a.m;
    const double d = det(a);
    const double id = std::abs(d) > 1e-300 ? 1 / d : 0;
    Mat r;
    r.m[0] = (m[4] * m[8] - m[5] * m[7]) * id;
    r.m[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    r.m[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    r.m[3] = (m[5] * m[6] - m[3] * m[8]) * id;
    r.m[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    r.m[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    r.m[6] = (m[3] * m[7] - m[4] * m[6]) * id;
    r.m[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    r.m[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    const V3 t = apply(r, a.t);
    r.t = {-t.x, -t.y, -t.z};
    // apply(r, a.t) already included r.t == 0 at this point
    return r;
}

double min_scale(const Mat& a) {
    double s = 1e30;
    for (int c = 0; c < 3; ++c) s = std::min(s, std::sqrt(a.m[c] * a.m[c] + a.m[3 + c] * a.m[3 + c] + a.m[6 + c] * a.m[6 + c]));
    return s;
}

Mat translate(V3 t) {
    Mat r;
    r.t = t;
    return r;
}

Mat scaling(V3 s) {
    Mat r;
    r.m[0] = s.x, r.m[4] = s.y, r.m[8] = s.z;
    return r;
}

Mat rotation(double rx, double ry, double rz) {
    const double d = 3.14159265358979323846 / 180;
    const double cx = std::cos(rx * d), sx = std::sin(rx * d), cy = std::cos(ry * d), sy = std::sin(ry * d), cz = std::cos(rz * d),
                 sz = std::sin(rz * d);
    Mat X, Y, Z;
    X.m[4] = cx, X.m[5] = -sx, X.m[7] = sx, X.m[8] = cx;
    Y.m[0] = cy, Y.m[2] = sy, Y.m[6] = -sy, Y.m[8] = cy;
    Z.m[0] = cz, Z.m[1] = -sz, Z.m[3] = sz, Z.m[4] = cz;
    return Z * Y * X;
}

void Box::grow(V3 p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}
void Box::add(const Box& b) {
    if (b.empty()) return;
    grow(b.lo);
    grow(b.hi);
}
bool Box::overlaps(const Box& b, double pad) const {
    return !(empty() || b.empty() || lo.x > b.hi.x + pad || hi.x < b.lo.x - pad || lo.y > b.hi.y + pad || hi.y < b.lo.y - pad ||
             lo.z > b.hi.z + pad || hi.z < b.lo.z - pad);
}
Box Box::moved(const Mat& a) const {
    Box r;
    if (empty()) return r;
    for (int i = 0; i < 8; ++i) r.grow(apply(a, {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}));
    return r;
}

// --- distances -------------------------------------------------------------------

namespace {

struct Fn : Sdf {
    std::function<double(V3)> f;
    double d(V3 p) const override { return f(p); }
};

SdfP make(Box bb, std::function<double(V3)> f) {
    auto s = std::make_shared<Fn>();
    s->bb = bb;
    s->f = std::move(f);
    return s;
}

double clampd(double v, double a, double b) { return std::max(a, std::min(b, v)); }

double box_d(V3 p, V3 b) {
    const V3 q{std::abs(p.x) - b.x, std::abs(p.y) - b.y, std::abs(p.z) - b.z};
    const V3 o{std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)};
    return len(o) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0);
}

double cyl_d(V3 p, double r, double hh) {
    const double a = std::hypot(p.x, p.z) - r, b = std::abs(p.y) - hh;
    return std::min(std::max(a, b), 0.0) + std::hypot(std::max(a, 0.0), std::max(b, 0.0));
}

// The distance to an outline, negative inside (either way round).
double poly_d(const std::vector<P2>& v, double px, double py) {
    const std::size_t n = v.size();
    double d = (px - v[0].x) * (px - v[0].x) + (py - v[0].y) * (py - v[0].y), s = 1;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double ex = v[j].x - v[i].x, ey = v[j].y - v[i].y, wx = px - v[i].x, wy = py - v[i].y;
        const double ee = ex * ex + ey * ey;
        const double t = ee > 0 ? clampd((wx * ex + wy * ey) / ee, 0, 1) : 0;
        const double bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c1 = py >= v[i].y, c2 = py < v[j].y, c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * std::sqrt(d);
}

Box box_of(V3 lo, V3 hi) {
    Box b;
    b.lo = lo, b.hi = hi;
    return b;
}

struct Xf : Sdf {
    SdfP child;
    Mat inv;
    double k = 1;
    double d(V3 p) const override { return child->d(apply(inv, p)) * k; }
    int pick(V3 p) const override { return child->pick(apply(inv, p)); }
};

struct Un : Sdf {
    std::vector<SdfP> c;
    double d(V3 p) const override {
        double m = 1e30;
        for (const auto& s : c) m = std::min(m, s->d(p));
        return m;
    }
    int pick(V3 p) const override {
        double m = 1e30;
        int mat = 0;
        for (const auto& s : c) {
            const double v = s->d(p);
            if (v < m) m = v, mat = s->pick(p);
        }
        return mat;
    }
};

struct Op : Sdf {
    SdfP a, b;
    int kind = 0;  // 0 sub, 1 and, 2 blend
    double k = 0;
    static double smin(double x, double y, double k) {
        const double h = std::max(k - std::abs(x - y), 0.0) / k;
        return std::min(x, y) - h * h * k * 0.25;
    }
    double d(V3 p) const override {
        const double x = a->d(p), y = b->d(p);
        if (kind == 1) return std::max(x, y);
        if (kind == 2) return k > 0 ? smin(x, y, k) : std::min(x, y);
        return k > 0 ? -smin(-x, y, k) : std::max(x, -y);
    }
    int pick(V3 p) const override { return kind == 2 && b->d(p) < a->d(p) ? b->pick(p) : a->pick(p); }
};

struct Mt : Sdf {
    SdfP s;
    double d(V3 p) const override { return s->d(p); }
};

}  // namespace

Mat basis(V3 o, V3 x, V3 y, V3 z) {
    Mat r;
    r.m[0] = x.x, r.m[3] = x.y, r.m[6] = x.z;
    r.m[1] = y.x, r.m[4] = y.y, r.m[7] = y.z;
    r.m[2] = z.x, r.m[5] = z.y, r.m[8] = z.z;
    r.t = o;
    return r;
}

V3 perpendicular(V3 d) {
    const V3 a = std::abs(d.y) < 0.9 ? V3{0, 1, 0} : V3{1, 0, 0};
    return unit(cross(a, d));
}

SdfP sdf_box(V3 h, double round) {
    const double r = std::min(round, std::min(h.x, std::min(h.y, h.z)));
    const V3 c{h.x - r, h.y - r, h.z - r};
    return make(box_of(h * -1, h), [c, r](V3 p) { return box_d(p, c) - r; });
}
SdfP sdf_sphere(double r) { return make(box_of({-r, -r, -r}, {r, r, r}), [r](V3 p) { return len(p) - r; }); }
SdfP sdf_cyl(double r, double hh, double round) {
    const double q = std::min(round, std::min(r, hh));
    return make(box_of({-r, -hh, -r}, {r, hh, r}), [r, hh, q](V3 p) { return cyl_d(p, r - q, hh - q) - q; });
}
SdfP sdf_cone(double r1, double r2, double h) {
    const double rm = std::max(r1, r2);
    return make(box_of({-rm, -h, -rm}, {rm, h, rm}), [=](V3 p) {
        const double qx = std::hypot(p.x, p.z), qy = p.y;
        const double k1x = r2, k1y = h, k2x = r2 - r1, k2y = 2 * h;
        const double cax = qx - std::min(qx, qy < 0 ? r1 : r2), cay = std::abs(qy) - h;
        const double dd = k2x * k2x + k2y * k2y;
        const double t = dd > 0 ? clampd(((k1x - qx) * k2x + (k1y - qy) * k2y) / dd, 0, 1) : 0;
        const double cbx = qx - k1x + k2x * t, cby = qy - k1y + k2y * t;
        const double s = (cbx < 0 && cay < 0) ? -1 : 1;
        return s * std::sqrt(std::min(cax * cax + cay * cay, cbx * cbx + cby * cby));
    });
}
SdfP sdf_torus(double R, double r) {
    return make(box_of({-R - r, -r, -R - r}, {R + r, r, R + r}), [R, r](V3 p) { return std::hypot(std::hypot(p.x, p.z) - R, p.y) - r; });
}
SdfP sdf_capsule(double r, double a) {
    return make(box_of({-r, -a - r, -r}, {r, a + r, r}), [r, a](V3 p) { return len({p.x, p.y - clampd(p.y, -a, a), p.z}) - r; });
}
SdfP sdf_lathe(const std::vector<P2>& prof) {
    std::vector<P2> poly = prof;
    poly.push_back({0, prof.back().y});
    poly.push_back({0, prof.front().y});
    Box b;
    for (const P2& q : prof) b.grow(V3{-q.x, q.y, -q.x}), b.grow(V3{q.x, q.y, q.x});
    return make(b, [poly](V3 p) { return poly_d(poly, std::hypot(p.x, p.z), p.y); });
}
SdfP sdf_extrude(const std::vector<P2>& o, double hd, double chamfer) {
    Box b;
    for (const P2& q : o) b.grow(V3{q.x, q.y, -hd}), b.grow(V3{q.x, q.y, hd});
    return make(b, [o, hd, chamfer](V3 p) {
        const double a = poly_d(o, p.x, p.y), c = std::abs(p.z) - hd;
        double d = std::min(std::max(a, c), 0.0) + std::hypot(std::max(a, 0.0), std::max(c, 0.0));
        if (chamfer > 0) d = std::max(d, (a + c + chamfer) * 0.70710678);
        return d;
    });
}
SdfP sdf_loft(const std::vector<std::vector<P2>>& rings, double h) {
    const std::size_t R = rings.size();
    Box b;
    double slope = 0;
    const double seg = R > 1 ? h / double(R - 1) : h;
    for (std::size_t i = 0; i < R; ++i) {
        for (const P2& q : rings[i]) b.grow(V3{q.x, 0, q.y}), b.grow(V3{q.x, h, q.y});
        if (i + 1 < R)
            for (std::size_t k = 0; k < rings[i].size() && k < rings[i + 1].size(); ++k)
                slope = std::max(slope, std::hypot(rings[i + 1][k].x - rings[i][k].x, rings[i + 1][k].y - rings[i][k].y) / seg);
    }
    const double f = 1 / std::sqrt(1 + slope * slope);
    return make(b, [=](V3 p) {
        const double t = clampd(p.y / seg, 0, double(R - 1));
        const std::size_t s = std::min<std::size_t>(R - 2 > R ? 0 : R - 2, std::size_t(t));
        const double u = t - double(s);
        std::vector<P2> poly(rings[s].size());
        for (std::size_t k = 0; k < poly.size(); ++k)
            poly[k] = {rings[s][k].x * (1 - u) + rings[s + 1][k].x * u, rings[s][k].y * (1 - u) + rings[s + 1][k].y * u};
        const double a = poly_d(poly, p.x, p.z) * f, c = std::max(-p.y, p.y - h);
        return std::min(std::max(a, c), 0.0) + std::hypot(std::max(a, 0.0), std::max(c, 0.0));
    });
}

SdfP sdf_tube(const std::vector<V3>& path, double r) {
    std::vector<SdfP> parts;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const V3 d = path[i + 1] - path[i];
        const double l = len(d);
        if (l < 1e-9) continue;
        const V3 y = d * (1 / l), x = perpendicular(y), z = cross(x, y);
        parts.push_back(sdf_xform(sdf_cyl(r, l / 2, 0), basis((path[i] + path[i + 1]) * 0.5, x, y, z)));
        if (i > 0) parts.push_back(sdf_xform(sdf_sphere(r), translate(path[i])));
    }
    return sdf_union(parts);
}

SdfP sdf_sweep(const std::vector<P2>& prof, const std::vector<V3>& points) {
    // Each segment the outline pushed along it in the segment's own frame (the
    // path's, which does not twist), long enough to reach past its corners,
    // and cut at the corners' mitre planes: so the segments meet face to face.
    const Path path = along(points);
    const std::size_t N = path.points.size(), S = path.frames.size();
    double reach = 0;
    for (const P2& q : prof) reach = std::max(reach, std::hypot(q.x, q.y));
    const auto mitre = [&](std::size_t i) {
        if (!path.closed && i == 0) return path.frames.front().t;
        if (!path.closed && i + 1 == N) return path.frames.back().t;
        return unit(path.frames[(i + S - 1) % S].t + path.frames[i % S].t);
    };
    std::vector<SdfP> parts;
    for (std::size_t k = 0; k < S; ++k) {
        const V3 a = path.points[k], b = path.points[(k + 1) % N];
        const double l = len(b - a);
        const Frame& f = path.frames[k];
        // A corner past 150 degrees is mitred no further (as the exact faces are).
        const double over = reach * 4;
        const SdfP bar = sdf_xform(sdf_extrude(prof, l / 2 + over, 0), basis((a + b) * 0.5, f.n, f.b, f.t));
        const V3 ma = mitre(k), mb = mitre((k + 1) % N);
        const auto cut = std::make_shared<Fn>();
        cut->bb = bar->bb;
        cut->f = [bar, a, b, ma, mb](V3 p) { return std::max({bar->d(p), -dot(p - a, ma), dot(p - b, mb)}); };
        parts.push_back(cut);
    }
    return sdf_union(parts);
}

SdfP sdf_xform(SdfP child, const Mat& m) {
    auto s = std::make_shared<Xf>();
    s->child = std::move(child);
    s->inv = inverse(m);
    s->k = min_scale(m);
    s->bb = s->child->bb.moved(m);
    s->material = s->child->material;
    return s;
}
SdfP sdf_union(std::vector<SdfP> c) {
    if (c.size() == 1) return c[0];
    auto s = std::make_shared<Un>();
    for (const auto& x : c) s->bb.add(x->bb);
    s->c = std::move(c);
    return s;
}
SdfP sdf_sub(SdfP a, SdfP b, double k) {
    auto s = std::make_shared<Op>();
    s->bb = a->bb;
    s->a = std::move(a), s->b = std::move(b), s->kind = 0, s->k = k;
    return s;
}
SdfP sdf_and(SdfP a, SdfP b) {
    auto s = std::make_shared<Op>();
    s->bb = a->bb;
    s->a = std::move(a), s->b = std::move(b), s->kind = 1;
    return s;
}
SdfP sdf_blend(SdfP a, SdfP b, double k) {
    auto s = std::make_shared<Op>();
    s->bb = a->bb;
    s->bb.add(b->bb);
    s->bb.lo = s->bb.lo - V3{k, k, k}, s->bb.hi = s->bb.hi + V3{k, k, k};
    s->a = std::move(a), s->b = std::move(b), s->kind = 2, s->k = k;
    return s;
}
SdfP with_material(SdfP f, int material) {
    struct M : Sdf {
        SdfP s;
        double d(V3 p) const override { return s->d(p); }
    };
    auto m = std::make_shared<M>();
    m->bb = f->bb;
    m->material = material;
    m->s = std::move(f);
    return m;
}

// --- the surface of a field: dual contouring -----------------------------------------

namespace {

struct Hermite {
    V3 p, n;
};

// The point in a cell that best lies on every plane through a crossing with
// its normal; the mass point where the planes say little.
V3 best_point(const std::vector<Hermite>& hs, V3 c0, double h) {
    V3 m;
    for (const auto& e : hs) m = m + e.p;
    m = m * (1.0 / double(hs.size()));
    double A[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double b[3] = {0, 0, 0};
    for (const auto& e : hs) {
        const double n[3] = {e.n.x, e.n.y, e.n.z};
        const double w = dot(e.n, e.p - m);
        for (int i = 0; i < 3; ++i) {
            b[i] += n[i] * w;
            for (int j = 0; j < 3; ++j) A[i][j] += n[i] * n[j];
        }
    }
    double V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 10; ++sweep) {
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::abs(A[p][q]) < 1e-14) continue;
                const double th = (A[q][q] - A[p][p]) / (2 * A[p][q]);
                const double t = (th >= 0 ? 1 : -1) / (std::abs(th) + std::sqrt(th * th + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 3; ++k) {
                    const double akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq, A[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c * apk - s * aqk, A[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq, V[k][q] = s * vkp + c * vkq;
                }
            }
    }
    const double lam[3] = {A[0][0], A[1][1], A[2][2]};
    const double top = std::max(lam[0], std::max(lam[1], lam[2]));
    double y[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        if (lam[k] < 0.1 * top || lam[k] < 1e-9) continue;
        const double s = (V[0][k] * b[0] + V[1][k] * b[1] + V[2][k] * b[2]) / lam[k];
        for (int i = 0; i < 3; ++i) y[i] += s * V[i][k];
    }
    // Out of its cell, the planes said too little: the mass point, which is in it (two cells clamped to one corner would be a pinch).
    const V3 x{m.x + y[0], m.y + y[1], m.z + y[2]};
    const double e = 1e-6 * h;
    if (x.x < c0.x - e || x.x > c0.x + h + e || x.y < c0.y - e || x.y > c0.y + h + e || x.z < c0.z - e || x.z > c0.z + h + e) return m;
    return x;
}

}  // namespace

void surface(const Sdf& f, double cell, int max_grid, Geom& out, std::string* warn) {
    const Box bb = f.bb;
    if (bb.empty()) return;
    double h = cell;
    const V3 ext = bb.hi - bb.lo;
    const double mx = std::max(ext.x, std::max(ext.y, ext.z)) + 4 * h;
    if (mx / h > max_grid) {
        h = mx / max_grid;
        if (warn) *warn += "a field was meshed coarser (" + std::to_string(h) + " m) to keep its grid down\n";
    }
    // Off by half a cell, so a face on a round number never lies on the nodes themselves.
    const V3 lo = bb.lo - V3{2.5 * h, 2.5 * h, 2.5 * h};
    const int nx = int(std::ceil((bb.hi.x + 2 * h - lo.x) / h)), ny = int(std::ceil((bb.hi.y + 2 * h - lo.y) / h)),
              nz = int(std::ceil((bb.hi.z + 2 * h - lo.z) / h));
    contour(f, lo, h, nx, ny, nz, out);
}

Box surface_in(const Sdf& f, const Box& region, double cell, int max_grid, Geom& out, std::string* warn) {
    double h = cell;
    const V3 ext = region.hi - region.lo;
    const double mx = std::max(ext.x, std::max(ext.y, ext.z)) + h;
    if (mx / h > max_grid) {
        h = mx / max_grid;
        if (warn) *warn += "a cut was meshed coarser (" + std::to_string(h) + " m) to keep its grid down\n";
    }
    // Off by a little over a third of a cell, as above: no node on a round number.
    const V3 lo = region.lo - V3{0.37 * h, 0.37 * h, 0.37 * h};
    const int nx = std::max(1, int(std::ceil((region.hi.x - lo.x) / h))), ny = std::max(1, int(std::ceil((region.hi.y - lo.y) / h))),
              nz = std::max(1, int(std::ceil((region.hi.z - lo.z) / h)));
    contour(f, lo, h, nx, ny, nz, out);
    Box grid;
    grid.lo = lo, grid.hi = lo + V3{nx * h, ny * h, nz * h};
    return grid;
}

void contour(const Sdf& f, V3 lo, double h, int nx, int ny, int nz, Geom& out) {
    const std::size_t sx = std::size_t(nx) + 1, sy = std::size_t(ny) + 1, sz = std::size_t(nz) + 1;
    const auto node = [&](int i, int j, int k) { return (std::size_t(k) * sy + std::size_t(j)) * sx + std::size_t(i); };
    const auto at = [&](int i, int j, int k) { return lo + V3{i * h, j * h, k * h}; };
    std::vector<float> val(sx * sy * sz, 1e9f);
    std::vector<unsigned char> done(sx * sy * sz, 0);
    constexpr int B = 8;
    const int bx = (nx + B - 1) / B, by = (ny + B - 1) / B, bz = (nz + B - 1) / B;
    std::vector<unsigned char> live(std::size_t(bx) * by * bz, 0);
    const double rad = 0.5 * B * h * 1.7320508 * 1.05;
    std::vector<float> centre_sign(live.size(), 1);
    for (int k = 0; k < bz; ++k)
        for (int j = 0; j < by; ++j)
            for (int i = 0; i < bx; ++i) {
                const std::size_t b = (std::size_t(k) * by + std::size_t(j)) * bx + std::size_t(i);
                const double d = f.d(at(i * B, j * B, k * B) + V3{B * h / 2, B * h / 2, B * h / 2});
                live[b] = std::abs(d) <= rad;
                centre_sign[b] = d < 0 ? -1.f : 1.f;
            }
    const auto each = [&](int i, int j, int k, auto&& fn) {
        for (int c = k * B; c <= std::min(k * B + B, nz); ++c)
            for (int b = j * B; b <= std::min(j * B + B, ny); ++b)
                for (int a = i * B; a <= std::min(i * B + B, nx); ++a) fn(a, b, c);
    };
    for (int k = 0; k < bz; ++k)
        for (int j = 0; j < by; ++j)
            for (int i = 0; i < bx; ++i)
                if (live[(std::size_t(k) * by + std::size_t(j)) * bx + std::size_t(i)])
                    each(i, j, k, [&](int a, int b, int c) {
                        const std::size_t n = node(a, b, c);
                        if (done[n]) return;
                        done[n] = 1;
                        val[n] = float(f.d(at(a, b, c)));
                    });
    for (int k = 0; k < bz; ++k)
        for (int j = 0; j < by; ++j)
            for (int i = 0; i < bx; ++i) {
                const std::size_t b = (std::size_t(k) * by + std::size_t(j)) * bx + std::size_t(i);
                if (!live[b])
                    each(i, j, k, [&](int a, int bb2, int c) {
                        const std::size_t n = node(a, bb2, c);
                        if (!done[n]) val[n] = centre_sign[b] * 1e9f;
                    });
            }

    std::unordered_map<uint64_t, Hermite> edges;
    const auto crossing = [&](int i, int j, int k, int axis) -> const Hermite& {
        const uint64_t key = uint64_t(node(i, j, k)) * 3 + uint64_t(axis);
        auto it = edges.find(key);
        if (it != edges.end()) return it->second;
        const V3 dir = axis == 0 ? V3{1, 0, 0} : axis == 1 ? V3{0, 1, 0} : V3{0, 0, 1};
        const V3 pa = at(i, j, k);
        double va = val[node(i, j, k)], vb = val[node(i + (axis == 0), j + (axis == 1), k + (axis == 2))];
        double ta = 0, tb = 1;
        double t = va / (va - vb);
        for (int it2 = 0; it2 < 6; ++it2) {
            t = clampd(ta + (tb - ta) * va / (va - vb), ta, tb);
            const double v = f.d(pa + dir * (t * h));
            if ((v < 0) == (va < 0)) ta = t, va = v;
            else tb = t, vb = v;
            if (std::abs(v) < 1e-7 * h) break;
        }
        const V3 p = pa + dir * (t * h);
        const double e = 0.1 * h;
        const V3 g{f.d({p.x + e, p.y, p.z}) - f.d({p.x - e, p.y, p.z}), f.d({p.x, p.y + e, p.z}) - f.d({p.x, p.y - e, p.z}),
                   f.d({p.x, p.y, p.z + e}) - f.d({p.x, p.y, p.z - e})};
        return edges.emplace(key, Hermite{p, unit(g)}).first->second;
    };

    static const int ce[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::unordered_map<uint64_t, int> verts;
    const auto cell_key = [&](int i, int j, int k) { return (uint64_t(k) * uint64_t(ny) + uint64_t(j)) * uint64_t(nx) + uint64_t(i); };
    for (int bk = 0; bk < bz; ++bk)
        for (int bj = 0; bj < by; ++bj)
            for (int bi = 0; bi < bx; ++bi) {
                if (!live[(std::size_t(bk) * by + std::size_t(bj)) * bx + std::size_t(bi)]) continue;
                for (int k = bk * B; k < std::min(bk * B + B, nz); ++k)
                    for (int j = bj * B; j < std::min(bj * B + B, ny); ++j)
                        for (int i = bi * B; i < std::min(bi * B + B, nx); ++i) {
                            float v[8];
                            int inside = 0;
                            for (int c = 0; c < 8; ++c) {
                                v[c] = val[node(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
                                inside += v[c] < 0;
                            }
                            if (inside == 0 || inside == 8) continue;
                            std::vector<Hermite> hs;
                            for (int e = 0; e < 12; ++e) {
                                const int a = ce[e][0], b = ce[e][1];
                                if ((v[a] < 0) == (v[b] < 0)) continue;
                                const int axis = e < 4 ? 0 : e < 8 ? 1 : 2;
                                hs.push_back(crossing(i + (a & 1), j + ((a >> 1) & 1), k + ((a >> 2) & 1), axis));
                            }
                            verts[cell_key(i, j, k)] = int(out.p.size());
                            out.p.push_back(best_point(hs, at(i, j, k), h));
                        }
            }
    const auto vert = [&](int i, int j, int k) {
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return -1;
        auto it = verts.find(cell_key(i, j, k));
        return it == verts.end() ? -1 : it->second;
    };
    // The four cells round an edge, wound by which end of the edge is inside -
    // never by the quad's own shape, which on a ridge is too thin to tell.
    const auto quad = [&](int a, int b, int c, int d, bool flip) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        const auto P = [&](int q) { return out.p[std::size_t(q)]; };
        if (flip) std::swap(b, d);
        const bool first = len(P(c) - P(a)) <= len(P(d) - P(b));
        const int tri[2][3] = {{a, b, c}, {a, c, d}}, alt[2][3] = {{a, b, d}, {b, c, d}};
        for (int q = 0; q < 2; ++q) {
            const int* t = first ? tri[q] : alt[q];
            out.t.insert(out.t.end(), {t[0], t[1], t[2]});
            const V3 cen = (P(t[0]) + P(t[1]) + P(t[2])) * (1.0 / 3);
            out.mat.push_back(f.pick(cen));
        }
    };
    for (int bk = 0; bk < bz; ++bk)
        for (int bj = 0; bj < by; ++bj)
            for (int bi = 0; bi < bx; ++bi) {
                if (!live[(std::size_t(bk) * by + std::size_t(bj)) * bx + std::size_t(bi)]) continue;
                for (int k = bk * B; k < std::min(bk * B + B, nz); ++k)
                    for (int j = bj * B; j < std::min(bj * B + B, ny); ++j)
                        for (int i = bi * B; i < std::min(bi * B + B, nx); ++i) {
                            const float v0 = val[node(i, j, k)];
                            for (int axis = 0; axis < 3; ++axis) {
                                const float v1 = val[node(i + (axis == 0), j + (axis == 1), k + (axis == 2))];
                                if ((v0 < 0) == (v1 < 0)) continue;
                                const bool in_low = v0 < 0;
                                if (axis == 0) quad(vert(i, j - 1, k - 1), vert(i, j, k - 1), vert(i, j, k), vert(i, j - 1, k), !in_low);
                                else if (axis == 1) quad(vert(i - 1, j, k - 1), vert(i, j, k - 1), vert(i, j, k), vert(i - 1, j, k), in_low);
                                else quad(vert(i - 1, j - 1, k), vert(i, j - 1, k), vert(i, j, k), vert(i - 1, j, k), !in_low);
                            }
                        }
            }
}

}  // namespace sg::sculpt::kernel
