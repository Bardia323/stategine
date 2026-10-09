#include "sg/render/Geometry.hpp"
#include "sg/spatial/Projection.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>
namespace sg::render {
namespace {
// A corner by its bits: two that differ in any bit are two corners.
struct Corner {
    uint32_t bits[8];
    bool operator==(const Corner& o) const { return std::memcmp(bits, o.bits, sizeof bits) == 0; }
};
struct CornerHash {
    std::size_t operator()(const Corner& c) const {
        uint64_t h = 1469598103934665603ULL;
        for (uint32_t b : c.bits) h = (h ^ b) * 1099511628211ULL;
        return static_cast<std::size_t>(h ^ (h >> 29));
    }
};
}  // namespace

Indexed indexed(const std::vector<float>& corners) {
    Indexed out;
    const std::size_t n = corners.size() / 8;
    out.index.reserve(n);
    std::unordered_map<Corner, uint32_t, CornerHash> seen;
    seen.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        Corner c;
        std::memcpy(c.bits, corners.data() + i * 8, sizeof c.bits);
        const auto [it, fresh] = seen.emplace(c, static_cast<uint32_t>(out.corners.size() / 8));
        if (fresh) out.corners.insert(out.corners.end(), corners.begin() + static_cast<std::ptrdiff_t>(i * 8),
                                      corners.begin() + static_cast<std::ptrdiff_t>(i * 8 + 8));
        out.index.push_back(it->second);
    }
    return out;
}

using spatial::projection::normalize;
using spatial::projection::Vec3;
std::vector<float> cube_vertices() {
    const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    std::vector<float> v;
    auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, const float *nrm) {
        const Vec3 pts[6] = {a, b, c, a, c, d};
        const float uv[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 6; ++i)
            v.insert(v.end(), {pts[i].x, pts[i].y, pts[i].z, nrm[0], nrm[1], nrm[2], uv[i][0], uv[i][1]});
    };
    const float h = 0.5f;
    face({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, n[0]);
    face({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, n[1]);
    face({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, n[2]);
    face({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, n[3]);
    face({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, n[4]);
    face({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, n[5]);
    return v;
}

std::vector<float> rounded_box_vertices(float sx, float sy, float sz, float r, float taper, int seg) {
    const float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    r = std::max(0.0f, std::min(r, std::min({hx, hy, hz}) * 0.95f));
    const float ix = hx - r, iy = hy - r, iz = hz - r;
    // Where the grid lines run along one axis: round the edge, then straight.
    const auto stops = [&](float h, float inner) {
        std::vector<float> s;
        for (int k = 0; k <= seg; ++k) {
            const float a = 1.5707963f * (1.0f - static_cast<float>(k) / seg);
            s.push_back(-inner - r * std::sin(a));
        }
        for (int k = 0; k <= seg; ++k) {
            const float a = 1.5707963f * static_cast<float>(k) / seg;
            s.push_back(inner + r * std::sin(a));
        }
        (void)h;
        return s;
    };
    const std::vector<float> xs = stops(hx, ix), ys = stops(hy, iy), zs = stops(hz, iz);
    std::vector<float> v;
    // A point on the (unrounded) surface, onto the rounded one: its nearest
    // point on the inner box, and out from there by r.
    const auto put = [&](Vec3 p, Vec3 face_n) {
        const Vec3 q{std::max(-ix, std::min(ix, p.x)), std::max(-iy, std::min(iy, p.y)),
                     std::max(-iz, std::min(iz, p.z))};
        Vec3 d{p.x - q.x, p.y - q.y, p.z - q.z};
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        Vec3 n = len > 1e-7f ? Vec3{d.x / len, d.y / len, d.z / len} : face_n;
        Vec3 at = len > 1e-7f ? Vec3{q.x + n.x * r, q.y + n.y * r, q.z + n.z * r} : p;
        // The taper: narrower towards the top, and the sides lean in with it.
        const float t = (at.y + hy) / std::max(sy, 1e-6f);
        const float k = 1.0f + (taper - 1.0f) * t;
        at.x *= k, at.z *= k;
        if (taper != 1.0f) {
            n.y += (n.x * at.x / std::max(hx, 1e-6f) + n.z * at.z / std::max(hz, 1e-6f)) * (1.0f - taper) * 0.5f *
                   (std::fabs(n.y) < 0.99f ? 1.0f : 0.0f);
        }
        // Into the unit box; the normal pre-divided so the model's scale
        // turns it back the right way (the shader normalises it).
        v.insert(v.end(), {at.x / sx, at.y / sy, at.z / sz, n.x / sx, n.y / sy, n.z / sz, (at.x / sx + 0.5f),
                           (at.y / sy + 0.5f)});
    };
    // Each face: a grid of the stops of its two axes, at +-h on the third.
    const auto face = [&](int axis, float sign) {
        const std::vector<float> &a = axis == 0 ? ys : axis == 1 ? zs : xs;
        const std::vector<float> &b = axis == 0 ? zs : axis == 1 ? xs : ys;
        const float h = axis == 0 ? hx : axis == 1 ? hy : hz;
        const Vec3 fn = axis == 0 ? Vec3{sign, 0, 0} : axis == 1 ? Vec3{0, sign, 0} : Vec3{0, 0, sign};
        const auto P = [&](float u, float w) {
            return axis == 0 ? Vec3{sign * h, u, w} : axis == 1 ? Vec3{w, sign * h, u} : Vec3{u, w, sign * h};
        };
        for (std::size_t i = 0; i + 1 < a.size(); ++i)
            for (std::size_t j = 0; j + 1 < b.size(); ++j) {
                if (a[i + 1] - a[i] < 1e-7f || b[j + 1] - b[j] < 1e-7f)
                    continue;
                Vec3 p00 = P(a[i], b[j]), p10 = P(a[i + 1], b[j]), p11 = P(a[i + 1], b[j + 1]), p01 = P(a[i], b[j + 1]);
                if (sign < 0)
                    std::swap(p10, p01);
                for (const Vec3 &p : {p00, p10, p11, p00, p11, p01})
                    put(p, fn);
            }
    };
    for (int axis = 0; axis < 3; ++axis) {
        face(axis, 1.0f);
        face(axis, -1.0f);
    }
    return v;
}

std::vector<float> cylinder_vertices(int segments, float taper) {
    std::vector<float> v;
    const float h = 0.5f, pi2 = 6.2831853f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = pi2 * i / segments, a1 = pi2 * (i + 1) / segments;
        const float x0 = std::cos(a0) * h, z0 = std::sin(a0) * h, x1 = std::cos(a1) * h, z1 = std::sin(a1) * h;
        const float t = taper; // the top's radius, against the bottom's
        const float u0 = static_cast<float>(i) / segments, u1 = static_cast<float>(i + 1) / segments;
        // A cone's side leans in: its normal tips up by as much.
        const float lean = (1.0f - t) * h, ny = lean / std::sqrt(lean * lean + 1.0f);
        const float nr = 1.0f / std::sqrt(lean * lean + 1.0f);
        const float n0x = std::cos(a0) * nr, n0z = std::sin(a0) * nr, n1x = std::cos(a1) * nr, n1z = std::sin(a1) * nr;
        v.insert(v.end(), {x0,     -h, z0,     n0x, ny, n0z, u0, 1, x1,     -h, z1,     n1x, ny, n1z, u1, 1,
                           x1 * t, h,  z1 * t, n1x, ny, n1z, u1, 0, x0,     -h, z0,     n0x, ny, n0z, u0, 1,
                           x1 * t, h,  z1 * t, n1x, ny, n1z, u1, 0, x0 * t, h,  z0 * t, n0x, ny, n0z, u0, 0});
        v.insert(v.end(), {0,      h, 0,      0, 1, 0, 0.5f,          0.5f,
                           x1 * t, h, z1 * t, 0, 1, 0, 0.5f + x1 * t, 0.5f + z1 * t,
                           x0 * t, h, z0 * t, 0, 1, 0, 0.5f + x0 * t, 0.5f + z0 * t});
        v.insert(v.end(), {0,  -h, 0,         0,         -1, 0,  0.5f, 0.5f, x0, -h, z0,        0,
                           -1, 0,  0.5f + x0, 0.5f + z0, x1, -h, z1,   0,    -1, 0,  0.5f + x1, 0.5f + z1});
    }
    return v;
}

std::vector<float> sphere_vertices(int stacks, int slices) {
    std::vector<float> v;
    const float pi = 3.14159265f;
    auto point = [&](int i, int j) {
        const float t = pi * i / stacks, p = 2 * pi * j / slices;
        const float nx = std::sin(t) * std::cos(p), ny = std::cos(t), nz = std::sin(t) * std::sin(p);
        v.insert(v.end(), {nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz, static_cast<float>(j) / slices,
                           static_cast<float>(i) / stacks});
    };
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            point(i, j);
            point(i + 1, j);
            point(i + 1, j + 1);
            point(i, j);
            point(i + 1, j + 1);
            point(i, j + 1);
        }
    return v;
}

std::vector<float> quad_vertices() {
    const float h = 0.5f;
    return {
        0, -h, h,  1, 0, 0, 0, 1, //
        0, -h, -h, 1, 0, 0, 1, 1, //
        0, h,  -h, 1, 0, 0, 1, 0, //
        0, -h, h,  1, 0, 0, 0, 1, //
        0, h,  -h, 1, 0, 0, 1, 0, //
        0, h,  h,  1, 0, 0, 0, 0, //
    };
}

} // namespace sg::render
