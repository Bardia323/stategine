// The wear a mesh's own shape asks for, corner by corner (render::wear).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "sg/render/Geometry.hpp"

namespace sg::render {

namespace {

struct V {
    double x = 0, y = 0, z = 0;
};
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(V a) { return std::sqrt(dot(a, a)); }
V unit(V a) {
    const double l = len(a);
    return l > 1e-20 ? a * (1 / l) : V{0, 1, 0};
}

// A tree of boxes over the faces, for the rays that ask what a corner sees.
struct Tree {
    struct Node {
        V lo, hi;
        int left = -1, right = -1, first = 0, count = 0;
    };
    std::vector<Node> nodes;
    std::vector<int> faces;
    const std::vector<std::array<V, 3>>* tris = nullptr;

    int build(int first, int count, const std::vector<V>& mid) {
        Node n;
        n.lo = {1e30, 1e30, 1e30}, n.hi = {-1e30, -1e30, -1e30};
        for (int i = first; i < first + count; ++i)
            for (const V& p : (*tris)[static_cast<std::size_t>(faces[static_cast<std::size_t>(i)])]) {
                n.lo = {std::min(n.lo.x, p.x), std::min(n.lo.y, p.y), std::min(n.lo.z, p.z)};
                n.hi = {std::max(n.hi.x, p.x), std::max(n.hi.y, p.y), std::max(n.hi.z, p.z)};
            }
        const int at = static_cast<int>(nodes.size());
        nodes.push_back(n);
        if (count <= 6) {
            nodes[static_cast<std::size_t>(at)].first = first, nodes[static_cast<std::size_t>(at)].count = count;
            return at;
        }
        // (Split along its longest way at the middle face.)
        const V e = n.hi - n.lo;
        const int axis = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
        const auto key = [&](int f) { const V& m = mid[static_cast<std::size_t>(f)]; return axis == 0 ? m.x : axis == 1 ? m.y : m.z; };
        std::nth_element(faces.begin() + first, faces.begin() + first + count / 2, faces.begin() + first + count, [&](int a, int b) { return key(a) < key(b); });
        const int l = build(first, count / 2, mid), r = build(first + count / 2, count - count / 2, mid);
        nodes[static_cast<std::size_t>(at)].left = l, nodes[static_cast<std::size_t>(at)].right = r;
        return at;
    }
    static bool box_hit(const Node& n, V o, V inv, double far) {
        double t0 = 0, t1 = far;
        const double lo[3] = {n.lo.x, n.lo.y, n.lo.z}, hi[3] = {n.hi.x, n.hi.y, n.hi.z}, oo[3] = {o.x, o.y, o.z}, ii[3] = {inv.x, inv.y, inv.z};
        for (int k = 0; k < 3; ++k) {
            double a = (lo[k] - oo[k]) * ii[k], b = (hi[k] - oo[k]) * ii[k];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a), t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
        return true;
    }
    // Whether a ray from `o` the way `d` meets a face within `far`.
    bool hits(V o, V d, double far) const {
        const V inv{1 / (std::abs(d.x) > 1e-12 ? d.x : 1e-12), 1 / (std::abs(d.y) > 1e-12 ? d.y : 1e-12), 1 / (std::abs(d.z) > 1e-12 ? d.z : 1e-12)};
        int stack[64], top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Node& n = nodes[static_cast<std::size_t>(stack[--top])];
            if (!box_hit(n, o, inv, far)) continue;
            if (n.count > 0) {
                for (int i = n.first; i < n.first + n.count; ++i) {
                    const auto& t = (*tris)[static_cast<std::size_t>(faces[static_cast<std::size_t>(i)])];
                    // (Moller and Trumbore's.)
                    const V e1 = t[1] - t[0], e2 = t[2] - t[0], p = cross(d, e2);
                    const double det = dot(e1, p);
                    if (std::abs(det) < 1e-14) continue;
                    const double inv_det = 1 / det;
                    const V s = o - t[0];
                    const double u = dot(s, p) * inv_det;
                    if (u < 0 || u > 1) continue;
                    const V q = cross(s, e1);
                    const double v = dot(d, q) * inv_det;
                    if (v < 0 || u + v > 1) continue;
                    const double tt = dot(e2, q) * inv_det;
                    if (tt > 1e-6 && tt < far) return true;
                }
            } else if (top < 62) {
                stack[top++] = n.left, stack[top++] = n.right;
            }
        }
        return false;
    }
};

}  // namespace

std::vector<float> wear(const std::vector<float>& corners, int rays, double spread, const float* size_of) {
    const std::size_t n = corners.size() / 8;
    std::vector<float> out(n * 4, 0.0f);
    if (n < 3) return out;
    // The corners welded where they stand (a crease's two corners are one
    // point of the surface), each point's way out the faces' round it,
    // weighed by their areas.
    std::vector<V> pos(n);
    const V by = size_of ? V{size_of[0], size_of[1], size_of[2]} : V{1, 1, 1};
    for (std::size_t i = 0; i < n; ++i) pos[i] = {corners[i * 8] * by.x, corners[i * 8 + 1] * by.y, corners[i * 8 + 2] * by.z};
    V lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const V& p : pos) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    const double size = std::max(len(hi - lo), 1e-6), q = size * 1e-5;
    std::unordered_map<uint64_t, uint32_t> at;
    std::vector<uint32_t> point(n);
    std::vector<V> where;
    for (std::size_t i = 0; i < n; ++i) {
        const auto k = [&](double v) { return static_cast<uint64_t>(static_cast<int64_t>(std::llround(v / q)) & 0x1fffff); };
        const uint64_t key = k(pos[i].x) | (k(pos[i].y) << 21) | (k(pos[i].z) << 42);
        const auto [it, fresh] = at.emplace(key, static_cast<uint32_t>(where.size()));
        if (fresh) where.push_back(pos[i]);
        point[i] = it->second;
    }
    const std::size_t m = where.size();
    std::vector<V> normal(m);
    std::vector<std::array<V, 3>> tris(n / 3);
    std::vector<V> mid(n / 3);
    for (std::size_t f = 0; f < n / 3; ++f) {
        const V a = pos[f * 3], b = pos[f * 3 + 1], c = pos[f * 3 + 2];
        const V w = cross(b - a, c - a);
        for (int k = 0; k < 3; ++k) normal[point[f * 3 + k]] = normal[point[f * 3 + k]] + w;
        tris[f] = {a, b, c}, mid[f] = (a + b + c) * (1.0 / 3.0);
    }
    for (V& v : normal) v = unit(v);
    // How it bends there: along each edge from it, how much the way out
    // turns over the length of the edge, out (convex, an edge to be worn) or
    // in (concave, a crease where dirt keeps) - a dimensionless share, the
    // turn over the length times the edge's length, so a box's edge reads
    // as the edge it is however finely it is made.
    std::vector<double> bend(m, 0.0), weight(m, 0.0);
    for (std::size_t f = 0; f < n / 3; ++f)
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = point[f * 3 + k], j = point[f * 3 + (k + 1) % 3];
            if (i == j) continue;
            const V e = where[i] - where[j];
            const double l2 = std::max(dot(e, e), 1e-20);
            const double c = dot(normal[i] - normal[j], e) / l2 * std::sqrt(l2);
            bend[i] += c, weight[i] += 1, bend[j] += c, weight[j] += 1;
        }
    for (std::size_t i = 0; i < m; ++i) bend[i] /= std::max(weight[i], 1.0);
    // (Diffused over the surface round each point, about `spread` metres:
    // a weathered face's lumps turn out and in by turns and cancel; an edge
    // turns one way all along and stays - wear is the edge's, not a lump's.)
    {
        std::vector<std::vector<uint32_t>> near(m);
        double edge_len = 0;
        std::size_t edges = 0;
        for (std::size_t f = 0; f < n / 3; ++f)
            for (int k = 0; k < 3; ++k) {
                const uint32_t i = point[f * 3 + k], j = point[f * 3 + (k + 1) % 3];
                if (i == j) continue;
                near[i].push_back(j);
                edge_len += len(where[i] - where[j]), ++edges;
            }
        const double step = edges ? edge_len / static_cast<double>(edges) : size;
        // (Each pass spreads it about one edge: passes enough for `spread`,
        // as a walk's reach goes as the root of its steps.)
        const int passes = std::clamp(static_cast<int>(std::lround(std::pow(spread / std::max(step, 1e-9), 2.0))), 0, 200);
        std::vector<double> next(m);
        for (int p = 0; p < passes; ++p) {
            for (std::size_t i = 0; i < m; ++i) {
                double s = bend[i];
                for (uint32_t j : near[i]) s += bend[j];
                next[i] = s / static_cast<double>(near[i].size() + 1);
            }
            bend.swap(next);
        }
        // (Spread out, an edge's turn is shared by its neighbours: brought
        // back up by how far it was spread.)
        const double gain = 1.0 + 0.5 * std::sqrt(static_cast<double>(passes));
        for (double& b : bend) b *= gain;
    }
    // How shut each point is from the sky round it: rays out over its
    // hemisphere, as much as light comes (cosine-weighed), each that meets
    // the mesh again within a third of its size one shut.
    Tree tree;
    tree.tris = &tris;
    tree.faces.resize(tris.size());
    for (std::size_t f = 0; f < tris.size(); ++f) tree.faces[f] = static_cast<int>(f);
    tree.build(0, static_cast<int>(tris.size()), mid);
    std::vector<double> shut(m, 0.0);
    const double far = size / 3, lift = size * 1e-4;
    for (std::size_t i = 0; i < m; ++i) {
        const V nn = normal[i];
        const V t = unit(std::abs(nn.x) < 0.9 ? cross(nn, V{1, 0, 0}) : cross(nn, V{0, 1, 0})), b = cross(nn, t);
        int closed = 0;
        for (int r = 0; r < rays; ++r) {
            // (A Hammersley point over the disc, lifted: cosine-weighed.)
            uint32_t bits = static_cast<uint32_t>(r);
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            const double u1 = (r + 0.5) / rays, u2 = std::fmod(bits * 2.3283064365386963e-10 + 0.618 * static_cast<double>(i % 97), 1.0);
            const double rr = std::sqrt(u1), phi = 6.283185307179586 * u2;
            const V d = unit(t * (rr * std::cos(phi)) + b * (rr * std::sin(phi)) + nn * std::sqrt(std::max(0.0, 1 - u1)));
            // (Or the ground it stands on: a thing's foot is on it, so a ray
            // down past its lowest point meets it within half a metre - what
            // rain splashes and soil reaches: a wall's foot is shut.)
            const V o = where[i] + nn * lift;
            const bool ground = d.y < -1e-6 && (lo.y - o.y) / d.y < std::min(far, 0.5);
            if (ground || tree.hits(o, d, far)) ++closed;
        }
        shut[i] = static_cast<double>(closed) / rays;
    }
    for (std::size_t c = 0; c < n; ++c) {
        const uint32_t i = point[c];
        out[c * 4 + 0] = static_cast<float>(std::clamp(bend[i], 0.0, 1.0));
        out[c * 4 + 1] = static_cast<float>(std::clamp(-bend[i], 0.0, 1.0));
        out[c * 4 + 2] = static_cast<float>(shut[i]);
        out[c * 4 + 3] = 0.0f;  // (made: a mesh without one reads 1 here)
    }
    return out;
}

}  // namespace sg::render
