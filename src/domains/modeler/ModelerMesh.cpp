// The field of a mesh: how far a point is from its faces, and whether it is
// inside - by the winding number along a ray, so meshes lying in one another
// (a heap of shapes, as an imported scene often is) are one solid where any of
// them is. Faces in a tree of boxes, so both are a walk down it.
#include <algorithm>
#include <array>
#include <cmath>

#include "ModelerKernel.hpp"

namespace sg::sculpt::kernel {

namespace {

struct Node {
    Box bb;
    int left = -1, right = -1, first = 0, count = 0;
};

double box_dist2(const Box& b, V3 p) {
    const double dx = std::max({b.lo.x - p.x, 0.0, p.x - b.hi.x}), dy = std::max({b.lo.y - p.y, 0.0, p.y - b.hi.y}),
                 dz = std::max({b.lo.z - p.z, 0.0, p.z - b.hi.z});
    return dx * dx + dy * dy + dz * dz;
}

// The nearest point of a triangle to p (Ericson, Real-Time Collision Detection 5.1.5).
V3 closest(V3 p, V3 a, V3 b, V3 c) {
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const V3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const V3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

struct MeshSdf : Sdf {
    std::vector<std::array<V3, 3>> tri;
    std::vector<int> mat;
    std::vector<Node> nodes;

    int build(int first, int count) {
        Node n;
        n.first = first, n.count = count;
        for (int i = first; i < first + count; ++i)
            for (const V3& q : tri[std::size_t(i)]) n.bb.grow(q);
        const int at = int(nodes.size());
        nodes.push_back(n);
        if (count <= 4) return at;
        const V3 e = n.bb.hi - n.bb.lo;
        const int axis = e.x > e.y && e.x > e.z ? 0 : e.y > e.z ? 1 : 2;
        const auto key = [axis](const std::array<V3, 3>& t) {
            const V3 c = (t[0] + t[1] + t[2]) * (1.0 / 3);
            return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
        };
        const int mid = first + count / 2;
        std::vector<int> order(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) order[std::size_t(i)] = first + i;
        std::nth_element(order.begin(), order.begin() + count / 2, order.end(),
                         [&](int a, int b) { return key(tri[std::size_t(a)]) < key(tri[std::size_t(b)]); });
        std::vector<std::array<V3, 3>> t2;
        std::vector<int> m2;
        for (int i : order) {
            t2.push_back(tri[std::size_t(i)]);
            if (!mat.empty()) m2.push_back(mat[std::size_t(i)]);
        }
        std::copy(t2.begin(), t2.end(), tri.begin() + first);
        if (!mat.empty()) std::copy(m2.begin(), m2.end(), mat.begin() + first);
        const int l = build(first, mid - first), r = build(mid, first + count - mid);
        nodes[std::size_t(at)].left = l, nodes[std::size_t(at)].right = r, nodes[std::size_t(at)].count = 0;
        return at;
    }

    // The nearest face, and how far.
    double nearest(V3 p, int& face) const {
        double best = 1e30;
        face = -1;
        int stack[96], top = 0;
        stack[top++] = 0;
        while (top) {
            const Node& n = nodes[std::size_t(stack[--top])];
            if (box_dist2(n.bb, p) >= best) continue;
            if (n.count) {
                for (int i = n.first; i < n.first + n.count; ++i) {
                    const auto& t = tri[std::size_t(i)];
                    const V3 q = closest(p, t[0], t[1], t[2]);
                    const double d2 = dot(p - q, p - q);
                    if (d2 < best) best = d2, face = i;
                }
                continue;
            }
            const Node& a = nodes[std::size_t(n.left)];
            const Node& b = nodes[std::size_t(n.right)];
            const bool a_first = box_dist2(a.bb, p) < box_dist2(b.bb, p);
            if (top + 2 > 96) continue;
            stack[top++] = a_first ? n.right : n.left;
            stack[top++] = a_first ? n.left : n.right;
        }
        return std::sqrt(best);
    }

    // The winding number along a ray: each face crossed, +1 going out, -1 in.
    int winding(V3 p) const {
        static const V3 dir = unit(V3{0.5773502691, 0.5776502691, 0.5770502691});
        const V3 inv{1.0 / dir.x, 1.0 / dir.y, 1.0 / dir.z};
        int w = 0;
        int stack[96], top = 0;
        stack[top++] = 0;
        while (top) {
            const Node& n = nodes[std::size_t(stack[--top])];
            double t0 = 0, t1 = 1e30;
            const double lo[3] = {n.bb.lo.x, n.bb.lo.y, n.bb.lo.z}, hi[3] = {n.bb.hi.x, n.bb.hi.y, n.bb.hi.z}, o[3] = {p.x, p.y, p.z},
                         iv[3] = {inv.x, inv.y, inv.z};
            for (int k = 0; k < 3; ++k) {
                double a = (lo[k] - o[k]) * iv[k], b = (hi[k] - o[k]) * iv[k];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a), t1 = std::min(t1, b);
            }
            if (t0 > t1) continue;
            if (n.count) {
                for (int i = n.first; i < n.first + n.count; ++i) {
                    const auto& t = tri[std::size_t(i)];
                    const V3 e1 = t[1] - t[0], e2 = t[2] - t[0], h = cross(dir, e2);
                    const double a = dot(e1, h);
                    if (std::fabs(a) < 1e-14) continue;
                    const double f = 1.0 / a;
                    const V3 s = p - t[0];
                    const double u = f * dot(s, h);
                    if (u < 0 || u > 1) continue;
                    const V3 q = cross(s, e1);
                    const double v = f * dot(dir, q);
                    if (v < 0 || u + v > 1) continue;
                    if (f * dot(e2, q) <= 0) continue;
                    w += a > 0 ? 1 : -1;
                }
                continue;
            }
            if (top + 2 > 96) continue;
            stack[top++] = n.left;
            stack[top++] = n.right;
        }
        return w;
    }

    double d(V3 p) const override {
        if (nodes.empty()) return 1e9;
        int face;
        const double far = nearest(p, face);
        return winding(p) != 0 ? -far : far;
    }
    int pick(V3 p) const override {
        if (mat.empty() || nodes.empty()) return material;
        int face;
        nearest(p, face);
        return face >= 0 ? mat[std::size_t(face)] : material;
    }
};

}  // namespace

SdfP sdf_mesh(const Geom& g) {
    auto s = std::make_shared<MeshSdf>();
    const std::size_t n = g.t.size() / 3;
    for (std::size_t f = 0; f < n; ++f) {
        s->tri.push_back({g.p[std::size_t(g.t[f * 3])], g.p[std::size_t(g.t[f * 3 + 1])], g.p[std::size_t(g.t[f * 3 + 2])]});
        if (g.mat.size() == n) s->mat.push_back(g.mat[f]);
    }
    if (!s->tri.empty()) s->build(0, int(s->tri.size()));
    s->bb = s->nodes.empty() ? Box{} : s->nodes[0].bb;
    if (!s->mat.empty()) s->material = s->mat[0];
    return s;
}

}  // namespace sg::sculpt::kernel
