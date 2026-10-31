// The inspector (sg/render/Inspect.hpp): a room's drawn triangles measured
// on the processor - what floats, what fights, what is sunk - and seen in
// square-on projections.
#include "sg/render/Inspect.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

#include "sg/core/Cache.hpp"
#include "sg/core/Work.hpp"
#include "sg/domains/Modeler.hpp"
#include "sg/render/ViewPlan.hpp"

namespace sg::render {
namespace {

// --- small geometry ------------------------------------------------------------------
struct V {
    double x = 0, y = 0, z = 0;
    V operator+(const V& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V operator-(const V& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V operator*(double s) const { return {x * s, y * s, z * s}; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
double dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(const V& a, const V& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(const V& a) { return std::sqrt(dot(a, a)); }
V unit(const V& a) {
    const double l = len(a);
    return l > 1e-300 ? a * (1.0 / l) : V{};
}
V vmin(const V& a, const V& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
V vmax(const V& a, const V& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
V of(const Vec3d& v) { return {v.x, v.y, v.z}; }
Vec3d back(const V& v) { return {v.x, v.y, v.z}; }

struct Box {
    V lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    void add(const V& p) { lo = vmin(lo, p), hi = vmax(hi, p); }
    void add(const Box& b) { lo = vmin(lo, b.lo), hi = vmax(hi, b.hi); }
    bool meets(const Box& b, double pad) const {
        return lo.x - pad <= b.hi.x && b.lo.x - pad <= hi.x && lo.y - pad <= b.hi.y && b.lo.y - pad <= hi.y && lo.z - pad <= b.hi.z && b.lo.z - pad <= hi.z;
    }
    bool holds(const V& p, double pad) const {
        return p.x >= lo.x - pad && p.x <= hi.x + pad && p.y >= lo.y - pad && p.y <= hi.y + pad && p.z >= lo.z - pad && p.z <= hi.z + pad;
    }
    bool empty() const { return lo.x > hi.x; }
};

struct Tri {
    V a, b, c, n;  // n: the face's way out (unit)
    int thing = 0;
    int element = 0;
    Box box() const {
        Box o;
        o.add(a), o.add(b), o.add(c);
        return o;
    }
};

// The nearest point of a triangle to p (Ericson's, Real-Time Collision Detection 5.1.5).
V nearest_on(const Tri& t, const V& p) {
    const V ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return t.a;
    const V bp = p - t.b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return t.b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return t.a + ab * (d1 / (d1 - d3));
    const V cp = p - t.c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return t.c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return t.a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return t.a + ab * (vb * den) + ac * (vc * den);
}

// The nearest two points of two segments (Ericson 5.1.9), as their distance squared.
double seg_seg2(const V& p1, const V& q1, const V& p2, const V& q2) {
    const V d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    double s = 0, t = 0;
    if (a <= 1e-18 && e <= 1e-18) return dot(r, r);
    if (a <= 1e-18) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = dot(d1, r);
        if (e <= 1e-18) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = dot(d1, d2), den = a * e - b * b;
            s = den > 1e-18 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) t = 0, s = std::clamp(-c / a, 0.0, 1.0);
            else if (t > 1) t = 1, s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    }
    const V w = (p1 + d1 * s) - (p2 + d2 * t);
    return dot(w, w);
}

// Where a ray from o along d meets a triangle (Moller-Trumbore), or < 0.
double ray_tri(const V& o, const V& d, const Tri& t) {
    const V e1 = t.b - t.a, e2 = t.c - t.a, p = cross(d, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-14) return -1;
    const double inv = 1.0 / det;
    const V s = o - t.a;
    const double u = dot(s, p) * inv;
    if (u < 0 || u > 1) return -1;
    const V q = cross(s, e1);
    const double v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return -1;
    return dot(e2, q) * inv;
}

bool seg_tri(const V& p, const V& q, const Tri& t) {
    const double h = ray_tri(p, q - p, t);
    return h >= 0 && h <= 1;
}

// Whether two triangles come within `tol` of each other.
bool tris_touch(const Tri& a, const Tri& b, double tol) {
    const double t2 = tol * tol;
    for (const V* p : {&a.a, &a.b, &a.c}) {
        const V n = nearest_on(b, *p);
        if (dot(n - *p, n - *p) <= t2) return true;
    }
    for (const V* p : {&b.a, &b.b, &b.c}) {
        const V n = nearest_on(a, *p);
        if (dot(n - *p, n - *p) <= t2) return true;
    }
    const V ea[3][2] = {{a.a, a.b}, {a.b, a.c}, {a.c, a.a}}, eb[3][2] = {{b.a, b.b}, {b.b, b.c}, {b.c, b.a}};
    for (const auto& x : ea)
        for (const auto& y : eb)
            if (seg_seg2(x[0], x[1], y[0], y[1]) <= t2) return true;
    for (const auto& x : ea)
        if (seg_tri(x[0], x[1], b)) return true;
    for (const auto& y : eb)
        if (seg_tri(y[0], y[1], a)) return true;
    return false;
}

// How much of two triangles in (nearly) one plane, facing one way, overlaps:
// b laid in a's plane, a cut by b's edges (Sutherland-Hodgman), its area.
double overlap_area(const Tri& a, const Tri& b, V& centre) {
    const V n = a.n, u = unit(a.b - a.a), w = cross(n, u);
    const auto flat = [&](const V& p) { return std::array<double, 2>{dot(p - a.a, u), dot(p - a.a, w)}; };
    std::vector<std::array<double, 2>> poly = {flat(a.a), flat(a.b), flat(a.c)};
    std::array<std::array<double, 2>, 3> cut = {flat(b.a), flat(b.b), flat(b.c)};
    // (b's corners anticlockwise, as a's are in its own plane.)
    const double turn = (cut[1][0] - cut[0][0]) * (cut[2][1] - cut[0][1]) - (cut[1][1] - cut[0][1]) * (cut[2][0] - cut[0][0]);
    if (turn < 0) std::swap(cut[1], cut[2]);
    for (int k = 0; k < 3 && !poly.empty(); ++k) {
        const auto& p0 = cut[k];
        const auto& p1 = cut[(k + 1) % 3];
        const auto side = [&](const std::array<double, 2>& q) { return (p1[0] - p0[0]) * (q[1] - p0[1]) - (p1[1] - p0[1]) * (q[0] - p0[0]); };
        std::vector<std::array<double, 2>> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const auto& s = poly[i];
            const auto& e = poly[(i + 1) % poly.size()];
            const double ds = side(s), de = side(e);
            if (ds >= 0) out.push_back(s);
            if ((ds >= 0) != (de >= 0)) {
                const double t = ds / (ds - de);
                out.push_back({s[0] + (e[0] - s[0]) * t, s[1] + (e[1] - s[1]) * t});
            }
        }
        poly = std::move(out);
    }
    if (poly.size() < 3) return 0.0;
    double area = 0, cx = 0, cy = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        const double cr = p[0] * q[1] - q[0] * p[1];
        area += cr, cx += (p[0] + q[0]) * cr, cy += (p[1] + q[1]) * cr;
    }
    area *= 0.5;
    if (std::fabs(area) < 1e-12) return 0.0;
    centre = a.a + u * (cx / (6 * area)) + w * (cy / (6 * area));
    return std::fabs(area);
}

// --- a tree of triangles ------------------------------------------------------------------
// (Its own, not spatial::Index: what is asked of it is the nearest hit along
// a ray and pairs of triangles near each other, millions of times, on every hand.)
struct Tree {
    struct Node {
        Box box;
        int left = -1, right = -1, first = 0, count = 0;
    };
    std::vector<Node> nodes;
    std::vector<int> order;  // triangle numbers, a leaf's together
    const std::vector<Tri>* tris = nullptr;

    void build(const std::vector<Tri>& all, std::vector<int> which) {
        tris = &all;
        order = std::move(which);
        nodes.clear();
        if (order.empty()) return;
        nodes.reserve(order.size() * 2 / 3 + 2);
        make(0, static_cast<int>(order.size()));
    }
    int make(int first, int count) {
        const int me = static_cast<int>(nodes.size());
        nodes.push_back({});
        Box b, mids;
        for (int i = first; i < first + count; ++i) {
            const Box tb = (*tris)[order[i]].box();
            b.add(tb);
            mids.add((tb.lo + tb.hi) * 0.5);
        }
        nodes[me].box = b;
        if (count <= 4) {
            nodes[me].first = first, nodes[me].count = count;
            return me;
        }
        const V ext = mids.hi - mids.lo;
        const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
        const int half = count / 2;
        std::nth_element(order.begin() + first, order.begin() + first + half, order.begin() + first + count, [&](int p, int q) {
            const Tri &a = (*tris)[p], &c = (*tris)[q];
            return a.a[axis] + a.b[axis] + a.c[axis] < c.a[axis] + c.b[axis] + c.c[axis];
        });
        const int l = make(first, half);
        const int r = make(first + half, count - half);
        nodes[me].left = l, nodes[me].right = r;
        return me;
    }
    // The nearest triangle along o + t d, t in (from, reach), that `skip` does not refuse.
    template <class Skip>
    int hit(const V& o, const V& d, double from, double& reach, Skip skip) const {
        if (nodes.empty()) return -1;
        const V inv{1.0 / (std::fabs(d.x) > 1e-300 ? d.x : 1e-300), 1.0 / (std::fabs(d.y) > 1e-300 ? d.y : 1e-300),
                    1.0 / (std::fabs(d.z) > 1e-300 ? d.z : 1e-300)};
        int best = -1;
        int stack[96];
        int n = 0;
        stack[n++] = 0;
        while (n > 0) {
            const Node& nd = nodes[stack[--n]];
            double t0 = from, t1 = reach;
            bool in = true;
            for (int k = 0; k < 3 && in; ++k) {
                double a = (nd.box.lo[k] - o[k]) * inv[k], b = (nd.box.hi[k] - o[k]) * inv[k];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a), t1 = std::min(t1, b);
                in = t0 <= t1;
            }
            if (!in) continue;
            if (nd.left < 0) {
                for (int i = nd.first; i < nd.first + nd.count; ++i) {
                    const Tri& t = (*tris)[order[i]];
                    if (skip(t)) continue;
                    const double h = ray_tri(o, d, t);
                    if (h > from && h < reach) reach = h, best = order[i];
                }
            } else if (n + 2 <= 96) {
                stack[n++] = nd.left, stack[n++] = nd.right;
            }
        }
        return best;
    }
    // Every pair of a triangle here and one of `other` whose boxes come within `pad`, until `fn` says stop.
    template <class Fn>
    bool pairs(const Tree& other, double pad, Fn fn) const {
        if (nodes.empty() || other.nodes.empty()) return false;
        std::vector<std::pair<int, int>> stack{{0, 0}};
        while (!stack.empty()) {
            const auto [i, j] = stack.back();
            stack.pop_back();
            const Node &a = nodes[i], &b = other.nodes[j];
            if (!a.box.meets(b.box, pad)) continue;
            if (a.left < 0 && b.left < 0) {
                for (int p = a.first; p < a.first + a.count; ++p)
                    for (int q = b.first; q < b.first + b.count; ++q)
                        if ((*tris)[order[p]].box().meets((*other.tris)[other.order[q]].box(), pad) && fn(order[p], other.order[q])) return true;
            } else if (b.left < 0 || (a.left >= 0 && (a.box.hi.x - a.box.lo.x) + (a.box.hi.y - a.box.lo.y) + (a.box.hi.z - a.box.lo.z) >=
                                                         (b.box.hi.x - b.box.lo.x) + (b.box.hi.y - b.box.lo.y) + (b.box.hi.z - b.box.lo.z))) {
                stack.push_back({a.left, j}), stack.push_back({a.right, j});
            } else {
                stack.push_back({i, b.left}), stack.push_back({i, b.right});
            }
        }
        return false;
    }
    // The nearest point of any triangle here to p, within `reach`: its distance.
    double nearest(const V& p, double reach) const {
        double best = reach * reach;
        if (nodes.empty()) return reach;
        int stack[96];
        int n = 0;
        stack[n++] = 0;
        while (n > 0) {
            const Node& nd = nodes[stack[--n]];
            const V c = vmin(vmax(p, nd.box.lo), nd.box.hi);
            if (dot(c - p, c - p) > best) continue;
            if (nd.left < 0) {
                for (int i = nd.first; i < nd.first + nd.count; ++i) {
                    const V q = nearest_on((*tris)[order[i]], p);
                    best = std::min(best, dot(q - p, q - p));
                }
            } else if (n + 2 <= 96) {
                stack[n++] = nd.left, stack[n++] = nd.right;
            }
        }
        return std::sqrt(best);
    }
    // How many triangles here a ray from p crosses (odd: p is inside).
    int crossings(const V& p, const V& d) const {
        int count = 0;
        if (nodes.empty()) return 0;
        int stack[96];
        int n = 0;
        stack[n++] = 0;
        const V inv{1.0 / d.x, 1.0 / d.y, 1.0 / d.z};
        while (n > 0) {
            const Node& nd = nodes[stack[--n]];
            double t0 = 1e-9, t1 = 1e300;
            bool in = true;
            for (int k = 0; k < 3 && in; ++k) {
                double a = (nd.box.lo[k] - p[k]) * inv[k], b = (nd.box.hi[k] - p[k]) * inv[k];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a), t1 = std::min(t1, b);
                in = t0 <= t1;
            }
            if (!in) continue;
            if (nd.left < 0) {
                for (int i = nd.first; i < nd.first + nd.count; ++i)
                    if (ray_tri(p, d, (*tris)[order[i]]) > 1e-9) ++count;
            } else if (n + 2 <= 96) {
                stack[n++] = nd.left, stack[n++] = nd.right;
            }
        }
        return count;
    }
};

// --- what is drawn --------------------------------------------------------------------------
const std::vector<V>& unit_cube() {
    static const std::vector<V> c = [] {
        std::vector<V> v;
        const auto quad = [&](V a, V b, V cc, V d) { v.insert(v.end(), {a, b, cc, a, cc, d}); };
        const double h = 0.5;
        quad({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h});
        quad({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h});
        quad({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h});
        quad({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
        quad({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h});
        quad({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});
        return v;
    }();
    return c;
}
const std::vector<V>& unit_cylinder() {
    static const std::vector<V> c = [] {
        std::vector<V> v;
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            const double a0 = 2 * M_PI * i / n, a1 = 2 * M_PI * (i + 1) / n;
            const V p0{0.5 * std::cos(a0), 0, 0.5 * std::sin(a0)}, p1{0.5 * std::cos(a1), 0, 0.5 * std::sin(a1)};
            const V lo{0, -0.5, 0}, hi{0, 0.5, 0};
            const V b0 = p0 + lo, b1 = p1 + lo, t0 = p0 + hi, t1 = p1 + hi;
            v.insert(v.end(), {b0, t1, b1, b0, t0, t1, hi, t1, t0, lo, b0, b1});
        }
        return v;
    }();
    return c;
}
const std::vector<V>& unit_sphere() {
    static const std::vector<V> c = [] {
        std::vector<V> v;
        const int n = 16, m = 10;
        const auto at = [&](int i, int j) {
            const double th = M_PI * j / m, ph = 2 * M_PI * i / n;
            return V{0.5 * std::sin(th) * std::cos(ph), 0.5 * std::cos(th), 0.5 * std::sin(th) * std::sin(ph)};
        };
        for (int j = 0; j < m; ++j)
            for (int i = 0; i < n; ++i) v.insert(v.end(), {at(i, j), at(i + 1, j + 1), at(i + 1, j), at(i, j), at(i, j + 1), at(i + 1, j + 1)});
        return v;
    }();
    return c;
}

V apply(const spatial::projection::Mat4& m, const V& p) {
    const float* a = m.m;
    return {a[0] * p.x + a[4] * p.y + a[8] * p.z + a[12], a[1] * p.x + a[5] * p.y + a[9] * p.z + a[13], a[2] * p.x + a[6] * p.y + a[10] * p.z + a[14]};
}

struct Thing {
    std::string name;
    Box box;
    std::vector<int> tris;
    bool ground = false, free = false;
    std::string ok;
};

struct Drawn {
    std::vector<Tri> tris;
    std::vector<Thing> things;
    std::vector<int> owner;  // a thing's whole (union of its name, group, parent)
    std::vector<std::string> element_names;
    // How far the scene's vertex stage draws each element nearer the eye
    // (sg/gl/Shaders.hpp, "No two surfaces fight"): its rank - three for each
    // halving of its size from 16 m, and what it says (`depth_layer`) - and a
    // rank's share of the distance there.
    std::vector<double> rank, share;
};

int find(std::vector<int>& u, int i) {
    while (u[i] != i) i = u[i] = u[u[i]];
    return i;
}
void join(std::vector<int>& u, int a, int b) {
    a = find(u, a), b = find(u, b);
    if (a != b) u[std::max(a, b)] = std::min(a, b);
}

Drawn gather(const Spatial3D& room) {
    Drawn d;
    std::map<std::string, int> thing_of;
    const std::string prefix = room.id().str() + ".";
    static const Key unseen{"unseen"}, taken{"taken"}, hidden{"hidden"}, shape{"shape"}, model{"model"}, skinned{"skinned"}, walk{"walk"},
        laid{"laid"}, group{"group"}, mount{"mount"}, ok_key{"inspect_ok"}, thick{"thick"}, border{"border"}, frame{"frame"};
    // Where a room's walked doorways are: the blanks that fill them are not drawn (as GLWorldView's fills_opening).
    std::vector<V> doors;
    for (const Element& e : room.elements())
        if (e.alive && e.kind == kinds::portal && e.params.num(walk, 0.0) > 0.5) doors.push_back(of(world_position(room, e)));
    std::vector<std::pair<int, std::string>> parents;
    const auto add_thing = [&](const Element& e, const std::string& whole) {
        auto it = thing_of.find(whole);
        if (it == thing_of.end()) {
            it = thing_of.emplace(whole, static_cast<int>(d.things.size())).first;
            d.things.push_back({whole});
        }
        Thing& t = d.things[it->second];
        static const Key building{"building"};
        if (e.kind == kinds::wall || e.params.has(laid) || e.params.num(building, 0.0) > 0.5) t.ground = true;
        if (e.params.get_or<std::string>(mount.str(), "") == "free") t.free = true;
        if (e.params.has(ok_key)) t.ok = e.params.get_or<std::string>(ok_key.str(), "");
        return it->second;
    };
    const auto ranked = [&](const spatial::projection::Mat4& m, double layer) {
        const auto col = [&](int c) { return len(V{m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}); };
        const double size = std::max(std::cbrt(std::max(col(0) * col(1) * col(2), 1e-12)), 1e-4);
        d.rank.push_back(std::clamp(3.0 * (4.0 - std::log2(size)), 0.0, 60.0) + layer);
        d.share.push_back(std::max(5e-6, len(V{m.m[12], m.m[13], m.m[14]}) * 1e-7));
    };
    const auto put = [&](const std::vector<V>& corners, const spatial::projection::Mat4& m, int thing, int element) {
        for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
            Tri t;
            t.a = apply(m, corners[i]), t.b = apply(m, corners[i + 1]), t.c = apply(m, corners[i + 2]);
            const V n = cross(t.b - t.a, t.c - t.a);
            if (len(n) < 1e-12) continue;
            t.n = unit(n), t.thing = thing, t.element = element;
            d.things[thing].tris.push_back(static_cast<int>(d.tris.size()));
            d.things[thing].box.add(t.box());
            d.tris.push_back(t);
        }
    };
    std::vector<V> buf;
    for (const Element& e : room.elements()) {
        if (!e.alive || e.params.num(unseen, 0.0) > 0.5 || e.params.num(taken, 0.0) > 0.5 || e.params.num(hidden, 0.0) > 0.5) continue;
        const bool mesh = e.kind == kinds::mesh, wall = e.kind == kinds::wall;
        // (A portal's own geometry is its frame, where it has one; what it shows - a world, a being stood in it, a
        // picture - is another state's, measured there.)
        const bool panel = e.kind == kinds::portal && e.params.num(walk, 0.0) < 0.5 && e.params.num(frame, 0.0) > 0.5;
        if (!mesh && !wall && !panel) continue;
        std::string local = e.id.str();
        if (local.rfind(prefix, 0) == 0) local = local.substr(prefix.size());
        if (wall && local.size() > 6 && local.compare(local.size() - 6, 6, ".blank") == 0) {
            const V at = of(world_position(room, e));
            bool fills = false;
            for (const V& o : doors) fills = fills || std::hypot(at.x - o.x, at.z - o.z) < 1.2;
            if (fills) continue;
        }
        // (A thing is the first two parts of its name - `b4.door_leaf` of `b4.door_leaf.panel` - or its one.)
        const auto whole_of = [](const std::string& n) {
            const std::size_t a = n.find('.');
            return a == std::string::npos ? n : n.substr(0, n.find('.', a + 1));
        };
        const std::string whole = whole_of(local);
        const int t = add_thing(e, whole);
        const int el = static_cast<int>(d.element_names.size());
        d.element_names.push_back(local);
        static const Key layer_key{"depth_layer"};
        const double layer = e.params.num(layer_key, 0.0);
        const std::string g = e.params.get_or<std::string>(group.str(), "");
        if (!g.empty()) parents.push_back({t, "group:" + g});
        const std::string par = e.params.get_or<std::string>(keys::parent.str(), "");
        if (!par.empty()) {
            std::string pl = par;
            if (pl.rfind(prefix, 0) == 0) pl = pl.substr(prefix.size());
            parents.push_back({t, whole_of(pl)});
        }
        if (panel) {
            // (A picture, a mirror, a screen: a panel, as GLWorldView's portal_frame_model lays it.)
            const Pose p = world_pose(room, e);
            const double w = e.params.num(keys::w, 3.0), h = e.params.num(keys::h, 2.0), b = e.params.num(border, 0.15);
            const bool framed = e.params.num(frame, 0.0) > 0.5;
            using spatial::projection::Mat4;
            using spatial::projection::Vec3;
            const Vec3 size = framed ? Vec3{0.08f, static_cast<float>(h + 2 * b - 0.08), static_cast<float>(w + 2 * b - 0.08)}
                                     : Vec3{static_cast<float>(e.params.num(thick, 0.004)), static_cast<float>(h), static_cast<float>(w)};
            const Mat4 m = Mat4::translate({static_cast<float>(p.position.x), static_cast<float>(p.position.y), static_cast<float>(p.position.z)}) *
                           Mat4::rotate_y(static_cast<float>(p.yaw)) * Mat4::rotate_z(static_cast<float>(p.pitch)) * Mat4::rotate_x(static_cast<float>(p.roll)) *
                           Mat4::scale(size);
            ranked(m, layer);
            put(unit_cube(), m, t, el);
            continue;
        }
        const auto m = box_transform(room, e);
        ranked(m, layer);
        const std::string s = e.params.get_or<std::string>(shape.str(), "");
        if (s == "model") {
            // (A being's skin: as it is posed now - its corners made from where its joints are, as the card makes them.)
            std::vector<float> posed;
            if (e.params.num(skinned, 0.0) > 0.5) posed = room.posed(Key{e.params.get_or<std::string>(model.str(), "")});
            const std::vector<float>* c = e.params.num(skinned, 0.0) > 0.5 ? &posed : room.model(Key{e.params.get_or<std::string>(model.str(), "")});
            if (!c || c->empty()) {
                put(unit_cube(), m, t, el);
                continue;
            }
            buf.clear();
            for (std::size_t i = 0; i + 7 < c->size() + 0; i += 8) buf.push_back({(*c)[i], (*c)[i + 1], (*c)[i + 2]});
            put(buf, m, t, el);
        } else if (s == "cylinder") {
            put(unit_cylinder(), m, t, el);
        } else if (s == "sphere") {
            put(unit_sphere(), m, t, el);
        } else {
            put(unit_cube(), m, t, el);
        }
    }
    // The room's own box, where it draws one (sg::render::enclosure): the ground of all.
    for (const DrawInstance& in : enclosure(room)) {
        auto it = thing_of.find("(shell)");
        if (it == thing_of.end()) {
            it = thing_of.emplace("(shell)", static_cast<int>(d.things.size())).first;
            d.things.push_back({"(shell)"});
            d.things.back().ground = true;
        }
        const int el = static_cast<int>(d.element_names.size());
        d.element_names.push_back("(shell)");
        ranked(in.model, 0.0);
        put(unit_cube(), in.model, it->second, el);
    }
    // A thing's whole: its name, what it is grouped with, what it hangs on.
    d.owner.resize(d.things.size());
    std::iota(d.owner.begin(), d.owner.end(), 0);
    std::map<std::string, int> groups;
    for (const auto& [t, other] : parents) {
        if (other.rfind("group:", 0) == 0) {
            auto [it, fresh] = groups.emplace(other, t);
            if (!fresh) join(d.owner, t, it->second);
        } else if (auto it = thing_of.find(other); it != thing_of.end()) {
            join(d.owner, t, it->second);
        }
    }
    for (std::size_t i = 0; i < d.owner.size(); ++i) d.owner[i] = find(d.owner, static_cast<int>(i));
    return d;
}

std::string mm(double m) {
    char b[32];
    std::snprintf(b, sizeof b, "%.0f mm", m * 1000.0);
    return b;
}
std::string at_text(const V& p) {
    char b[64];
    std::snprintf(b, sizeof b, "%.2f,%.2f,%.2f", p.x, p.y, p.z);
    return b;
}

// --- pictures ------------------------------------------------------------------------------------
// A 3 by 5 font of the digits, for the faults' numbers.
const char* kDigits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
                           "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111"};

struct Picture {
    int w = 0, h = 0;
    std::vector<unsigned char> px;  // rgb, top row first
    void make(int ww, int hh, unsigned char g) { w = ww, h = hh, px.assign(static_cast<std::size_t>(w) * h * 3, g); }
    void set(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        unsigned char* p = &px[(static_cast<std::size_t>(y) * w + x) * 3];
        p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g), p[2] = static_cast<unsigned char>(b);
    }
    void rect(int x0, int y0, int x1, int y1, int r, int g, int b, int thick = 2) {
        for (int k = 0; k < thick; ++k) {
            for (int x = x0; x <= x1; ++x) set(x, y0 + k, r, g, b), set(x, y1 - k, r, g, b);
            for (int y = y0; y <= y1; ++y) set(x0 + k, y, r, g, b), set(x1 - k, y, r, g, b);
        }
    }
    void number(int x, int y, int n, int scale, int r, int g, int b) {
        const std::string s = std::to_string(n);
        for (int i = 0; i < static_cast<int>(s.size()); ++i) {
            const char* glyph = kDigits[s[i] - '0'];
            for (int gy = -1; gy < 6; ++gy)
                for (int gx = -1; gx < 4; ++gx) {
                    const bool on = gx >= 0 && gx < 3 && gy >= 0 && gy < 5 && glyph[gy * 3 + gx] == '1';
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx)
                            if (on) set(x + (i * 4 + gx) * scale + sx, y + gy * scale + sy, r, g, b);
                            else set(x + (i * 4 + gx) * scale + sx, y + gy * scale + sy, 0, 0, 0);
                }
        }
    }
    void blit(const Picture& o, int x0, int y0) {
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                const unsigned char* p = &o.px[(static_cast<std::size_t>(y) * o.w + x) * 3];
                set(x0 + x, y0 + y, p[0], p[1], p[2]);
            }
    }
};

void colour_of(int thing, double& r, double& g, double& b) {
    uint32_t h = static_cast<uint32_t>(thing) * 2654435761u;
    h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
    r = 0.35 + 0.5 * ((h & 255) / 255.0), g = 0.35 + 0.5 * (((h >> 8) & 255) / 255.0), b = 0.35 + 0.5 * (((h >> 16) & 255) / 255.0);
}

// One square-on view: rays along `d` from the plane through `o` spanned by
// `right` and `up` (each a pixel of `px` metres), `reach` deep.
struct View {
    std::string name;
    V o, d, right, up;
    int w = 0, h = 0;
    double px = 0.01, reach = 3.0;
};

}  // namespace

InspectReport inspect(const Spatial3D& room, const InspectOptions& opt) {
    const auto began = std::chrono::steady_clock::now();
    InspectReport rep;
    Drawn d = gather(room);
    rep.triangles = d.tris.size(), rep.things = d.things.size();
    if (d.tris.empty()) return rep;
    const int nt = static_cast<int>(d.things.size());
    std::vector<Tree> trees(nt);
    sg::work::each(static_cast<std::size_t>(nt), [&](std::size_t i) { trees[i].build(d.tris, d.things[i].tris); });
    Tree all;
    {
        std::vector<int> every(d.tris.size());
        std::iota(every.begin(), every.end(), 0);
        all.build(d.tris, std::move(every));
    }
    // Pairs of things whose boxes come near each other (swept along x).
    std::vector<std::pair<int, int>> near;
    {
        std::vector<int> by(nt);
        std::iota(by.begin(), by.end(), 0);
        std::sort(by.begin(), by.end(), [&](int a, int b) { return d.things[a].box.lo.x < d.things[b].box.lo.x; });
        const double pad = std::max(opt.touch, opt.fight);
        for (int i = 0; i < nt; ++i) {
            const Thing& a = d.things[by[i]];
            if (a.box.empty()) continue;
            for (int j = i + 1; j < nt; ++j) {
                const Thing& b = d.things[by[j]];
                if (b.box.lo.x > a.box.hi.x + pad) break;
                if (a.box.meets(b.box, pad)) near.push_back({by[i], by[j]});
            }
        }
    }

    // 1. Floating: what touches what; a whole that touches nothing coming down to the shell.
    std::vector<char> touching(near.size(), 0);
    sg::work::each(near.size(), [&](std::size_t k) {
        const auto [a, b] = near[k];
        touching[k] = trees[a].pairs(trees[b], opt.touch, [&](int p, int q) { return tris_touch(d.tris[p], d.tris[q], opt.touch); }) ? 1 : 0;
    });
    std::vector<int> held(nt);
    for (int i = 0; i < nt; ++i) held[i] = d.owner[i];
    for (std::size_t k = 0; k < near.size(); ++k)
        if (touching[k]) join(held, near[k].first, near[k].second);
    std::vector<char> grounded(nt, 0);
    for (int i = 0; i < nt; ++i)
        if (d.things[i].ground || d.things[i].free || !d.things[i].ok.empty()) grounded[find(held, i)] = 1;
    // (A space with nothing of a building - a void, a sky round planets - has nothing to stand on: nothing floats in it.)
    bool any_building = false;
    for (const Thing& t : d.things) any_building = any_building || t.ground;
    std::map<int, std::vector<int>> floating;
    for (int i = 0; i < nt && any_building; ++i)
        if (!grounded[find(held, i)] && !d.things[i].tris.empty()) floating[find(held, i)].push_back(i);
    for (const auto& [root, members] : floating) {
        Box b;
        std::set<int> mine(members.begin(), members.end());
        std::vector<V> low;
        for (int m : members) {
            b.add(d.things[m].box);
            for (int t : d.things[m].tris) low.insert(low.end(), {d.tris[t].a, d.tris[t].b, d.tris[t].c});
        }
        std::sort(low.begin(), low.end(), [](const V& p, const V& q) { return p.y < q.y; });
        if (low.size() > 64) low.resize(64);
        double gap = 1e300;
        for (const V& p : low) {
            double reach = 50.0;
            if (all.hit(p, V{0, -1, 0}, 1e-6, reach, [&](const Tri& t) { return mine.count(t.thing) > 0; }) >= 0) gap = std::min(gap, reach);
        }
        // (And what it is nearest, and how far: what it was meant to touch, most often.)
        double near_d = 1e300;
        int near_t = -1;
        {
            std::vector<V> pts;
            for (int m : members)
                for (int t : d.things[m].tris) pts.insert(pts.end(), {d.tris[t].a, d.tris[t].b, d.tris[t].c});
            if (pts.size() > 600) {
                std::vector<V> few;
                for (std::size_t i = 0; i < pts.size(); i += pts.size() / 600 + 1) few.push_back(pts[i]);
                pts.swap(few);
            }
            for (int t = 0; t < nt; ++t) {
                if (mine.count(t) || d.things[t].tris.empty() || !d.things[t].box.meets(b, 1.0)) continue;
                for (const V& p : pts) {
                    const double dd = trees[t].nearest(p, std::min(near_d, 1.0));
                    if (dd < near_d) near_d = dd, near_t = t;
                }
            }
        }
        InspectFault f;
        f.kind = "floating";
        std::string names;
        for (std::size_t i = 0; i < members.size() && i < 6; ++i) names += (i ? "+" : "") + d.things[members[i]].name;
        if (members.size() > 6) names += "+" + std::to_string(members.size() - 6) + " more";
        f.thing = names, f.at = back((b.lo + b.hi) * 0.5), f.lo = back(b.lo), f.hi = back(b.hi), f.amount = gap;
        f.text = "touches nothing that stands on the room: " + (gap < 1e299 ? mm(gap) + " above what is under it" : std::string("nothing under it")) +
                 (near_t >= 0 ? "; nearest " + d.things[near_t].name + ", " + mm(near_d) + " off" : std::string());
        if (near_t >= 0) f.other = d.things[near_t].name;
        rep.faults.push_back(f);
    }

    // 2. Fighting: faces of two elements in one plane, facing one way, overlapping.
    // Two faces facing one way, a gap g apart, overlapping: the vertex stage
    // draws the one behind Δ ranks nearer than the one in front (Δ its rank
    // less theirs), a share of the distance a rank - so where Δ > 0 the two
    // tie at g / (Δ share) seen square on, and further seen aslant: from there
    // on, they fight. Δ is known within one (each thing's rank has a part
    // drawn from where it stands, rounded as the card rounds it): two that
    // coincide fight unless their sizes part them by more.
    struct Fight {
        int ea, eb;
        double area, from;  // how much overlaps; the nearest the eye sees them fight (m)
        V centre;
        Box box;
        V n;  // the way their faces look
    };
    std::vector<std::vector<Fight>> fights(near.size());
    const auto fight_pairs = [&](int a, int b, std::vector<Fight>& out) {
        // (By the two and the way their faces look: a board's top and its bottom fighting are two fights, not one with
        // its middle inside the board.)
        std::map<std::tuple<int, int, int>, Fight> by;
        trees[a].pairs(trees[b], opt.fight, [&](int p, int q) {
            const Tri &s = d.tris[p], &t = d.tris[q];
            if (s.element == t.element || dot(s.n, t.n) < 0.9995) return false;
            double sep = 0;
            for (const V* v : {&t.a, &t.b, &t.c}) {
                const double h = dot(*v - s.a, s.n);
                if (std::fabs(h) > opt.fight) return false;
                sep += h / 3.0;
            }
            // (t in front of s where sep > 0.)
            const int front = sep >= 0 ? t.element : s.element, behind = sep >= 0 ? s.element : t.element;
            const double g = std::fabs(sep), delta = d.rank[behind] - d.rank[front];
            double from;
            if (g < 3e-4) {
                if (std::fabs(delta) >= 1.0) return false;
                from = 0.0;
            } else {
                if (delta + 1.0 <= 0.0) return false;
                from = g / ((delta + 1.0) * d.share[behind]);
                if (from > opt.seen) return false;
            }
            V c;
            const double area = overlap_area(s, t, c);
            if (area <= 1e-7) return false;
            const auto two = std::minmax(s.element, t.element);
            const int way = static_cast<int>(std::lround(s.n.x * 2) + 3) * 25 + static_cast<int>(std::lround(s.n.y * 2) + 3) * 5 + static_cast<int>(std::lround(s.n.z * 2) + 3);
            Fight& f = by.try_emplace(std::tuple{two.first, two.second, way}, Fight{two.first, two.second, 0.0, 1e300, {}, {}, {}}).first->second;
            f.n = s.n;
            f.from = std::min(f.from, from);
            f.centre = (f.centre * f.area + c * area) * (1.0 / (f.area + area));
            f.area += area;
            f.box.add(c);
            f.box.add(s.box());
            return false;
        });
        for (auto& [k, f] : by)
            if (f.area >= opt.fight_area) out.push_back(f);
    };
    sg::work::each(near.size(), [&](std::size_t k) { fight_pairs(near[k].first, near[k].second, fights[k]); });
    // (And within one thing, between its own elements.)
    std::vector<std::vector<Fight>> own(nt);
    sg::work::each(static_cast<std::size_t>(nt), [&](std::size_t i) { fight_pairs(static_cast<int>(i), static_cast<int>(i), own[i]); });
    const auto add_fight = [&](const Fight& f) {
        InspectFault x;
        x.kind = "fighting";
        x.thing = d.element_names[f.ea], x.other = d.element_names[f.eb];
        x.at = back(f.centre), x.amount = f.area, x.lo = back(f.box.lo), x.hi = back(f.box.hi);
        char b[160];
        if (f.from <= 0.0) std::snprintf(b, sizeof b, "faces in one plane, of one size: they fight from any distance (%.0f cm2)", f.area * 1e4);
        else std::snprintf(b, sizeof b, "faces a hair apart: they fight seen from %.1f m away and further (%.0f cm2)", f.from, f.area * 1e4);
        x.text = b;
        rep.faults.push_back(x);
    };
    // Only what an eye can see fights: not where the two lie inside a third
    // solid (two walls' ends in a corner), nor facing out of the building
    // (its outside, where no eye stands: a ray out from it meets nothing).
    // (Inside an element - each closed on its own; a thing of many overlapping boxes, the room's own walls, is not.)
    const int ne = static_cast<int>(d.element_names.size());
    std::vector<Tree> parts(ne);
    std::vector<Box> part_box(ne);
    {
        std::vector<std::vector<int>> of_el(ne);
        for (int i = 0; i < static_cast<int>(d.tris.size()); ++i) of_el[d.tris[i].element].push_back(i), part_box[d.tris[i].element].add(d.tris[i].box());
        sg::work::each(static_cast<std::size_t>(ne), [&](std::size_t i) { parts[i].build(d.tris, of_el[i]); });
    }
    const auto inside_any = [&](const V& p) {
        const V dir = unit(V{0.5773, 0.5779, 0.5769});
        for (int e = 0; e < ne; ++e) {
            if (part_box[e].empty() || !part_box[e].holds(p, 0.0)) continue;
            if (parts[e].crossings(p, dir) % 2 == 1 && parts[e].crossings(p, V{-dir.x, dir.y, -dir.z}) % 2 == 1) return true;
        }
        return false;
    };
    const auto seen = [&](const Fight& f) {
        const V n = f.n;
        const V p = f.centre + n * 0.002;
        if (inside_any(p)) return false;
        // (Facing out of the building - two of its own walls' ends at a corner, outside - where a ray out meets nothing:
        // no eye stands there. Under an open sky the rest is seen as it faces.)
        const bool both = [&] {
            int a = -1, b = -1;
            for (const Tri& t : d.tris) {
                if (t.element == f.ea) a = t.thing;
                if (t.element == f.eb) b = t.thing;
                if (a >= 0 && b >= 0) break;
            }
            return a >= 0 && b >= 0 && d.things[a].ground && d.things[b].ground;
        }();
        if (!both) return true;
        double reach = 60.0;
        return all.hit(p, n, 0.0, reach, [](const Tri&) { return false; }) >= 0;
    };
    std::vector<const Fight*> every;
    for (const auto& v : fights)
        for (const Fight& f : v) every.push_back(&f);
    for (const auto& v : own)
        for (const Fight& f : v) every.push_back(&f);
    std::vector<char> shown(every.size(), 0);
    sg::work::each(every.size(), [&](std::size_t i) { shown[i] = seen(*every[i]) ? 1 : 0; });
    for (std::size_t i = 0; i < every.size(); ++i)
        if (shown[i]) add_fight(*every[i]);

    // 3. Sunk: a thing's corners inside another (a closed one), deeper than `sink`.
    std::vector<InspectFault> sunk(near.size());
    std::vector<char> is_sunk(near.size(), 0);
    sg::work::each(near.size(), [&](std::size_t k) {
        const auto [a, b] = near[k];
        // (The building's own pieces - walls into the floor's slab - are laid so: one is never sunk into another.)
        if (d.owner[a] == d.owner[b] || !d.things[a].ok.empty() || !d.things[b].ok.empty() || (d.things[a].ground && d.things[b].ground)) return;
        double worst = 0;
        V where;
        int into = -1;
        for (int pass = 0; pass < 2; ++pass) {
            const int s = pass ? b : a, t = pass ? a : b;
            const Box& tb = d.things[t].box;
            std::vector<V> pts;
            for (int i : d.things[s].tris)
                for (const V* p : {&d.tris[i].a, &d.tris[i].b, &d.tris[i].c})
                    if (tb.holds(*p, -opt.sink)) pts.push_back(*p);
            if (pts.size() > 1500) {
                std::vector<V> few;
                for (std::size_t i = 0; i < pts.size(); i += pts.size() / 1500 + 1) few.push_back(pts[i]);
                pts.swap(few);
            }
            const V dir = unit(V{0.5773, 0.5779, 0.5769});
            for (const V& p : pts) {
                if (trees[t].crossings(p, dir) % 2 == 0) continue;
                if (trees[t].crossings(p, V{-dir.x, dir.y, -dir.z}) % 2 == 0) continue;  // (two rays agree: inside)
                const double depth = trees[t].nearest(p, 1.0);
                if (depth > worst) worst = depth, where = p, into = t;
            }
        }
        if (worst > opt.sink) {
            const int s = into == b ? a : b;
            InspectFault f;
            f.kind = "sunk";
            f.thing = d.things[s].name, f.other = d.things[into].name, f.at = back(where), f.amount = worst;
            Box bx = d.things[s].box;
            f.lo = back(bx.lo), f.hi = back(bx.hi);
            f.text = "is " + mm(worst) + " inside " + d.things[into].name;
            sunk[k] = f, is_sunk[k] = 1;
        }
    });
    for (std::size_t k = 0; k < near.size(); ++k)
        if (is_sunk[k]) rep.faults.push_back(sunk[k]);

    std::stable_sort(rep.faults.begin(), rep.faults.end(), [](const InspectFault& a, const InspectFault& b) {
        const auto rank = [](const std::string& k) { return k == "floating" ? 0 : k == "sunk" ? 1 : 2; };
        return rank(a.kind) < rank(b.kind);
    });

    // 4. The sheet: a plan cut under the ceiling, the walls from the middle each way, in slabs.
    if (!opt.out.empty()) {
        Box rb;
        for (const Thing& t : d.things)
            if (!t.box.empty()) rb.add(t.box);
        const double px = std::max(opt.px, std::max({rb.hi.x - rb.lo.x, rb.hi.z - rb.lo.z, rb.hi.y - rb.lo.y}) / opt.most_px);
        std::vector<View> views;
        {
            View p;
            p.name = "plan";
            p.o = {rb.lo.x, rb.hi.y - 0.04, rb.lo.z};
            p.d = {0, -1, 0}, p.right = {1, 0, 0}, p.up = {0, 0, 1};
            p.w = static_cast<int>((rb.hi.x - rb.lo.x) / px) + 1, p.h = static_cast<int>((rb.hi.z - rb.lo.z) / px) + 1;
            p.px = px, p.reach = rb.hi.y - rb.lo.y + 0.1;
            views.push_back(p);
        }
        // (Slabs 2.5 m deep each way along x and z, as an architect's sections: every wall seen square on from inside.)
        const double slab = 2.5;
        for (int axis = 0; axis < 2; ++axis)
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                const double lo = axis == 0 ? rb.lo.x : rb.lo.z, hi = axis == 0 ? rb.hi.x : rb.hi.z;
                const int n = std::max(1, static_cast<int>(std::ceil((hi - lo) / slab)));
                for (int s = 0; s < n; ++s) {
                    View v;
                    const double cut = sgn > 0 ? lo + s * slab : hi - s * slab;
                    v.name = std::string(axis == 0 ? (sgn > 0 ? "east" : "west") : (sgn > 0 ? "south" : "north")) + " " + std::to_string(s + 1);
                    v.d = axis == 0 ? V{static_cast<double>(sgn), 0, 0} : V{0, 0, static_cast<double>(sgn)};
                    v.up = {0, 1, 0};
                    v.right = cross(v.d, v.up) * -1.0;
                    // (Its origin the corner its `right` and `up` start from.)
                    const V across = axis == 0 ? V{0, 0, 1} : V{1, 0, 0};
                    const double a_lo = axis == 0 ? rb.lo.z : rb.lo.x, a_hi = axis == 0 ? rb.hi.z : rb.hi.x;
                    const bool flip = dot(v.right, across) < 0;
                    V o = axis == 0 ? V{cut, rb.lo.y, flip ? a_hi : a_lo} : V{flip ? a_hi : a_lo, rb.lo.y, cut};
                    v.o = o;
                    v.w = static_cast<int>((a_hi - a_lo) / px) + 1, v.h = static_cast<int>((rb.hi.y - rb.lo.y) / px) + 1;
                    v.px = px, v.reach = slab + 0.3;
                    views.push_back(v);
                }
            }
        // Which things are at fault, by what, and their numbers.
        std::map<std::string, int> numbered;
        std::vector<std::pair<Box, int>> marks;
        for (std::size_t i = 0; i < rep.faults.size(); ++i) {
            Box b;
            if (rep.faults[i].kind == "fighting") {
                // (A fight by where it is, a small square: the box of both faces would cover the room.)
                const V c = of(rep.faults[i].at);
                b.add(c - V{0.04, 0.04, 0.04}), b.add(c + V{0.04, 0.04, 0.04});
            } else {
                b.add(of(rep.faults[i].lo)), b.add(of(rep.faults[i].hi));
            }
            marks.push_back({b, static_cast<int>(i)});
        }
        std::vector<int> fault_of_thing(nt, -1);
        for (std::size_t i = 0; i < rep.faults.size(); ++i) {
            const InspectFault& f = rep.faults[i];
            if (f.kind != "floating" && f.kind != "sunk") continue;
            for (int t = 0; t < nt; ++t) {
                const std::string& n = d.things[t].name;
                if (fault_of_thing[t] < 0 && (f.thing == n || f.thing.rfind(n + "+", 0) == 0 || f.thing.find("+" + n + "+") != std::string::npos ||
                                              (f.thing.size() > n.size() && f.thing.compare(f.thing.size() - n.size() - 1, n.size() + 1, "+" + n) == 0)))
                    fault_of_thing[t] = static_cast<int>(i);
            }
        }
        std::vector<Picture> pics(views.size());
        for (std::size_t vi = 0; vi < views.size(); ++vi) {
            const View& v = views[vi];
            Picture& pic = pics[vi];
            pic.make(v.w, v.h, 24);
            std::vector<int> id(static_cast<std::size_t>(v.w) * v.h, -1);
            std::vector<float> depth(id.size(), 0.f);
            sg::work::each(static_cast<std::size_t>(v.h), [&](std::size_t row) {
                const int y = static_cast<int>(row);
                for (int x = 0; x < v.w; ++x) {
                    const V o = v.o + v.right * ((x + 0.5) * v.px) + v.up * ((v.h - 1 - y + 0.5) * v.px);
                    double reach = v.reach;
                    const int t = all.hit(o, v.d, 0.0, reach, [](const Tri&) { return false; });
                    if (t < 0) continue;
                    const Tri& tr = d.tris[t];
                    const std::size_t at = static_cast<std::size_t>(y) * v.w + x;
                    id[at] = tr.thing, depth[at] = static_cast<float>(reach);
                    double r, g, b;
                    colour_of(tr.thing, r, g, b);
                    const int bad = fault_of_thing[tr.thing];
                    if (bad >= 0) r = 1.0, g = rep.faults[bad].kind == "floating" ? 0.15 : 0.85, b = 0.1;
                    // (Lit from over the left shoulder, facing the eye brightest; further in the slab, a little darker.)
                    const double lit = 0.45 + 0.4 * std::fabs(dot(tr.n, v.d)) + 0.15 * std::max(0.0, dot(tr.n, unit(V{-0.3, 0.8, 0.5})));
                    const double far = 1.0 - 0.35 * std::clamp(reach / v.reach, 0.0, 1.0);
                    pic.set(x, y, static_cast<int>(std::min(255.0, 255 * r * lit * far)), static_cast<int>(std::min(255.0, 255 * g * lit * far)),
                            static_cast<int>(std::min(255.0, 255 * b * lit * far)));
                }
            });
            // (Where one thing gives way to another, or a step in depth: a dark line.)
            for (int y = 1; y < v.h; ++y)
                for (int x = 1; x < v.w; ++x) {
                    const std::size_t a = static_cast<std::size_t>(y) * v.w + x;
                    if (id[a] != id[a - 1] || id[a] != id[a - v.w] || std::fabs(depth[a] - depth[a - 1]) > 0.05 || std::fabs(depth[a] - depth[a - v.w]) > 0.05)
                        pic.set(x, y, 10, 10, 10);
                }
            // The faults seen here, outlined and numbered.
            for (const auto& [b, n] : marks) {
                const InspectFault& f = rep.faults[n];
                // (Its box laid on the picture's plane, if it lies in what the view sees.)
                const V c = (b.lo + b.hi) * 0.5;
                const double along = dot(c - v.o, v.d);
                if (along < -0.05 || along > v.reach + 0.05) continue;
                double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
                for (int k = 0; k < 8; ++k) {
                    const V p{k & 1 ? b.hi.x : b.lo.x, k & 2 ? b.hi.y : b.lo.y, k & 4 ? b.hi.z : b.lo.z};
                    const double px_ = dot(p - v.o, v.right) / v.px, py_ = v.h - 1 - dot(p - v.o, v.up) / v.px;
                    x0 = std::min(x0, px_), x1 = std::max(x1, px_), y0 = std::min(y0, py_), y1 = std::max(y1, py_);
                }
                if (x1 < 0 || y1 < 0 || x0 >= v.w || y0 >= v.h) continue;
                const int pad = 6;
                const int r = 255, g = f.kind == "floating" ? 40 : f.kind == "sunk" ? 220 : 0, bl = f.kind == "fighting" ? 255 : 0;
                pic.rect(static_cast<int>(x0) - pad, static_cast<int>(y0) - pad, static_cast<int>(x1) + pad, static_cast<int>(y1) + pad, r, g, bl, 2);
                pic.number(static_cast<int>(x0) - pad, static_cast<int>(y0) - pad - 18, n + 1, 3, r, g, bl);
            }
        }
        // Laid out: the plan, then each way's slabs in a row, a label over each.
        int W = 0, H = 0;
        std::vector<std::vector<int>> rows{{0}};
        for (std::size_t i = 1; i < views.size(); ++i) {
            const std::string way = views[i].name.substr(0, views[i].name.find(' '));
            if (rows.size() == 1 || views[rows.back().front()].name.substr(0, views[rows.back().front()].name.find(' ')) != way) rows.push_back({});
            rows.back().push_back(static_cast<int>(i));
        }
        const int gap = 30;
        for (const auto& r : rows) {
            int w = 0, h = 0;
            for (int i : r) w += pics[i].w + gap, h = std::max(h, pics[i].h);
            W = std::max(W, w), H += h + gap;
        }
        Picture sheet;
        sheet.make(W + gap, H + gap, 50);
        int y = gap;
        for (const auto& r : rows) {
            int x = gap, h = 0;
            for (std::size_t k = 0; k < r.size(); ++k) {
                const int i = r[k];
                sheet.blit(pics[i], x, y);
                // (Its number in its row: 0 the plan; each way's slabs 1, 2 ... from where that way starts.)
                sheet.number(x, y - 22, &r == &rows.front() ? 0 : static_cast<int>(k + 1), 3, 200, 200, 200);
                x += pics[i].w + gap, h = std::max(h, pics[i].h);
            }
            y += h + gap;
        }
        std::filesystem::create_directories(opt.out);
        const std::string base = opt.out + "/" + room.id().str();
        {
            std::ofstream o(base + ".png", std::ios::binary);
            o << sculpt::png(sheet.px, sheet.w, sheet.h);
        }
        std::ofstream t(base + ".txt");
        t << room.id().str() << ": " << rep.things << " things, " << rep.triangles << " triangles; views: ";
        for (const View& v : views) t << v.name << (&v == &views.back() ? "\n" : ", ");
        const auto lines = inspect_lines(rep);
        for (std::size_t i = 0; i < lines.size(); ++i) t << (i + 1) << ". " << lines[i] << "\n";
    }
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return rep;
}

std::vector<std::string> inspect_faults(const Spatial3D& room, const InspectOptions& options) {
    const Drawn d = gather(room);
    Hasher h;
    // (The inspector's own make: what it measures changes when this does.)
    h.text("inspect 1").number(options.touch).number(options.fight).number(options.seen).number(options.sink).number(options.fight_area);
    for (const Tri& t : d.tris) {
        const double v[9] = {t.a.x, t.a.y, t.a.z, t.b.x, t.b.y, t.b.z, t.c.x, t.c.y, t.c.z};
        h.bytes(v, sizeof v).integer(t.element);
    }
    for (std::size_t i = 0; i < d.element_names.size(); ++i) h.text(d.element_names[i]).number(d.rank[i]);
    for (const Thing& t : d.things) h.text(t.name).integer(t.ground).integer(t.free).text(t.ok);
    for (int o : d.owner) h.integer(o);
    const Digest key = h.digest();
    std::string kept;
    std::vector<std::string> out;
    if (cache::load("inspect", key, kept)) {
        for (std::size_t a = 0; a < kept.size();) {
            const std::size_t b = kept.find('\n', a);
            out.push_back(kept.substr(a, b - a));
            a = b == std::string::npos ? kept.size() : b + 1;
        }
        return out;
    }
    InspectOptions o = options;
    o.out.clear();
    out = inspect_lines(inspect(room, o));
    for (const std::string& l : out) kept += l + "\n";
    if (!kept.empty()) kept.pop_back();
    cache::store("inspect", key, kept);
    return out;
}

double drawn_hit(const Spatial3D& room, const Vec3d& from, const Vec3d& dir, double reach) { return drawn_hits(room, {{from, dir}}, reach).front(); }

double drawn_drop(const Spatial3D& room, const std::string& name) {
    Drawn d = gather(room);
    std::vector<char> mine(d.element_names.size(), 0);
    for (std::size_t i = 0; i < d.element_names.size(); ++i) {
        const std::string& n = d.element_names[i];
        mine[i] = n == name || n.rfind(name + ".", 0) == 0;
    }
    std::vector<V> low;
    for (const Tri& t : d.tris)
        if (mine[t.element]) low.insert(low.end(), {t.a, t.b, t.c});
    if (low.empty()) return -1.0;
    // (How far it can come down before any of it meets what is under it: the least gap under any of its corners -
    // not under its lowest, which may hang past the edge of what it stands on.)
    if (low.size() > 4000) {
        std::vector<V> few;
        for (std::size_t i = 0; i < low.size(); i += low.size() / 4000 + 1) few.push_back(low[i]);
        low.swap(few);
    }
    Tree all;
    std::vector<int> every(d.tris.size());
    std::iota(every.begin(), every.end(), 0);
    all.build(d.tris, std::move(every));
    double best = -1.0;
    for (const V& p : low) {
        double r = 3.0;
        if (all.hit(p, V{0, -1, 0}, 1e-6, r, [&](const Tri& t) { return mine[t.element] != 0; }) >= 0 && (best < 0.0 || r < best)) best = r;
    }
    return best;
}

std::vector<double> drawn_hits(const Spatial3D& room, const std::vector<std::pair<Vec3d, Vec3d>>& rays, double reach) {
    Drawn d = gather(room);
    Tree all;
    std::vector<int> every(d.tris.size());
    std::iota(every.begin(), every.end(), 0);
    all.build(d.tris, std::move(every));
    std::vector<double> out(rays.size(), -1.0);
    for (std::size_t i = 0; i < rays.size(); ++i) {
        double r = reach;
        if (all.hit(of(rays[i].first), unit(of(rays[i].second)), 0.0, r, [](const Tri&) { return false; }) >= 0) out[i] = r;
    }
    return out;
}

std::vector<std::string> inspect_lines(const InspectReport& r) {
    std::vector<std::string> out;
    for (const InspectFault& f : r.faults)
        out.push_back(f.kind + " " + f.thing + (f.other.empty() ? "" : " / " + f.other) + " at " + at_text(of(f.at)) + ": " + f.text);
    return out;
}

}  // namespace sg::render
