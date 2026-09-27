// Stategine - Shapes: models a state's things can be drawn as, beyond boxes,
// cylinders and spheres.
//
// A model is triangles, as a renderer takes them - each corner its position,
// its normal and its uv, eight numbers - flat shaded (every face its own
// normal), so what is made of few faces looks cut, not blurred: low poly that
// is meant to be. Nothing here knows about GL; a state keeps the models its
// things are drawn as (Spatial3D::model), and an element says which it is
// (`shape` = "model", `model` = its name).
//
//   extrude   an outline, in x (along) and y (up), pushed out `depth` along
//             z, its edges cut back by `chamfer` - the side view of a gun, a
//             bracket, a sign
//   lathe     a profile, radius against height, turned about y in `sides`
//             faces - a barrel, a turret's body, a lamp
//   fit       one or more of them, into the unit box a mesh is sized from
//             (-0.5..0.5 each way, stood on its base as a box is): its size,
//             to give the element as sx, sy, sz, so it is drawn as made
//
//   auto body = shapes::extrude({{0, 0}, {0.3, 0}, {0.3, 0.1}, {0, 0.1}}, 0.04, 0.006);
//   Vec3d size;
//   room.model("gun", shapes::fit(body, size));
//   room.mesh("gun1", x, y, z).params.set("shape", "model").set("model", "gun")
//       .set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z);
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "sg/core/Core.hpp"

namespace sg::shapes {

struct P2 {
    double x = 0, y = 0;
};

namespace detail {
inline void corner(std::vector<float>& out, const Vec3d& p, const Vec3d& n, double u, double v) {
    out.insert(out.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), static_cast<float>(n.x),
                           static_cast<float>(n.y), static_cast<float>(n.z), static_cast<float>(u), static_cast<float>(v)});
}
inline Vec3d cross3(const Vec3d& a, const Vec3d& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3d unit(const Vec3d& a) {
    const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1e-12 ? Vec3d{a.x / l, a.y / l, a.z / l} : Vec3d{0, 1, 0};
}
// A face of its own, flat: its normal from its corners (counter-clockwise
// seen from outside).
inline void tri(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c) {
    const Vec3d n = unit(cross3(b - a, c - a));
    corner(out, a, n, 0, 0);
    corner(out, b, n, 1, 0);
    corner(out, c, n, 1, 1);
}
inline void quad(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d) {
    tri(out, a, b, c);
    tri(out, a, c, d);
}
inline double area(const std::vector<P2>& p) {
    double s = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const P2& a = p[i];
        const P2& b = p[(i + 1) % p.size()];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}
// An outline cut into triangles, ear by ear (it may be concave): indices.
inline std::vector<int> ears(const std::vector<P2>& p) {
    std::vector<int> left(p.size()), out;
    for (std::size_t i = 0; i < p.size(); ++i) left[i] = static_cast<int>(i);
    const auto inside = [](const P2& q, const P2& a, const P2& b, const P2& c) {
        const auto side = [](const P2& o, const P2& u, const P2& v) { return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x); };
        const double d1 = side(a, b, q), d2 = side(b, c, q), d3 = side(c, a, q);
        return d1 > 1e-12 && d2 > 1e-12 && d3 > 1e-12;
    };
    int guard = 0;
    while (left.size() > 3 && guard++ < 10000) {
        bool cut = false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            const int ia = left[(i + left.size() - 1) % left.size()], ib = left[i], ic = left[(i + 1) % left.size()];
            const P2 &a = p[static_cast<std::size_t>(ia)], &b = p[static_cast<std::size_t>(ib)], &c = p[static_cast<std::size_t>(ic)];
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 1e-12) continue;  // not convex here
            bool clear = true;
            for (int k : left)
                if (k != ia && k != ib && k != ic && inside(p[static_cast<std::size_t>(k)], a, b, c)) clear = false;
            if (!clear) continue;
            out.insert(out.end(), {ia, ib, ic});
            left.erase(left.begin() + static_cast<std::ptrdiff_t>(i));
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (left.size() == 3) out.insert(out.end(), {left[0], left[1], left[2]});
    return out;
}
// The outline pulled in by `by` all round (each corner along its bisector).
inline std::vector<P2> inset(const std::vector<P2>& p, double by) {
    std::vector<P2> out(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        const P2& a = p[(i + p.size() - 1) % p.size()];
        const P2& b = p[i];
        const P2& c = p[(i + 1) % p.size()];
        const auto in_of = [](const P2& u, const P2& v) {  // inward normal of edge u->v, counter-clockwise outline
            const double dx = v.x - u.x, dy = v.y - u.y, l = std::hypot(dx, dy);
            return l > 1e-12 ? P2{-dy / l, dx / l} : P2{0, 0};
        };
        const P2 n1 = in_of(a, b), n2 = in_of(b, c);
        P2 m{n1.x + n2.x, n1.y + n2.y};
        const double ml = std::hypot(m.x, m.y);
        if (ml < 1e-9) m = n1;
        else m = {m.x / ml, m.y / ml};
        const double cosh = std::max(0.35, m.x * n1.x + m.y * n1.y);  // (sharp corners kept from shooting off)
        out[i] = {b.x + m.x * by / cosh, b.y + m.y * by / cosh};
    }
    return out;
}
}  // namespace detail

// `outline` (counter-clockwise or not) pushed out to `depth` along z, centred
// on z = 0, the edges round both faces cut back by `chamfer`.
inline std::vector<float> extrude(std::vector<P2> outline, double depth, double chamfer = 0.0) {
    using namespace detail;
    std::vector<float> out;
    if (outline.size() < 3) return out;
    if (area(outline) < 0) std::reverse(outline.begin(), outline.end());
    const std::size_t n = outline.size();
    chamfer = std::clamp(chamfer, 0.0, depth * 0.45);
    const double zo = depth * 0.5 - chamfer, zf = depth * 0.5;
    const std::vector<P2> face = chamfer > 0 ? inset(outline, chamfer) : outline;
    const std::vector<int> tris = ears(face);
    // The two faces, front (+z) and back (-z).
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
        const P2 &a = face[static_cast<std::size_t>(tris[t])], &b = face[static_cast<std::size_t>(tris[t + 1])],
                 &c = face[static_cast<std::size_t>(tris[t + 2])];
        tri(out, {a.x, a.y, zf}, {b.x, b.y, zf}, {c.x, c.y, zf});
        tri(out, {a.x, a.y, -zf}, {c.x, c.y, -zf}, {b.x, b.y, -zf});
    }
    for (std::size_t i = 0; i < n; ++i) {
        const P2& a = outline[i];
        const P2& b = outline[(i + 1) % n];
        // The side, all round.
        quad(out, {a.x, a.y, -zo}, {b.x, b.y, -zo}, {b.x, b.y, zo}, {a.x, a.y, zo});
        if (chamfer > 0) {
            // And the cut edges, between the side and each face.
            const P2& fa = face[i];
            const P2& fb = face[(i + 1) % n];
            quad(out, {a.x, a.y, zo}, {b.x, b.y, zo}, {fb.x, fb.y, zf}, {fa.x, fa.y, zf});
            quad(out, {a.x, a.y, -zo}, {fa.x, fa.y, -zf}, {fb.x, fb.y, -zf}, {b.x, b.y, -zo});
        }
    }
    return out;
}

// `profile` - (radius, height) from the bottom up - turned about y in `sides`
// faces; closed at either end where its radius there is not nothing.
inline std::vector<float> lathe(const std::vector<P2>& profile, int sides) {
    using namespace detail;
    std::vector<float> out;
    sides = std::max(3, sides);
    const auto at = [&](const P2& p, int k) {
        const double a = 2.0 * 3.14159265358979 * k / sides;
        return Vec3d{p.x * std::cos(a), p.y, p.x * std::sin(a)};
    };
    // A face, unless it has come to nothing (where the radius is 0).
    const auto face = [&](const Vec3d& a, const Vec3d& b, const Vec3d& c) {
        const Vec3d n = cross3(b - a, c - a);
        if (n.x * n.x + n.y * n.y + n.z * n.z > 1e-20) tri(out, a, b, c);
    };
    for (std::size_t i = 0; i + 1 < profile.size(); ++i)
        for (int k = 0; k < sides; ++k) {
            const Vec3d a = at(profile[i], k), b = at(profile[i], k + 1), c = at(profile[i + 1], k + 1), d = at(profile[i + 1], k);
            face(a, d, c);
            face(a, c, b);
        }
    // The ends: the bottom facing down, the top up.
    const P2& lo = profile.front();
    const P2& hi = profile.back();
    for (int k = 0; k < sides; ++k) {
        if (lo.x > 1e-9) face({0, lo.y, 0}, at(lo, k), at(lo, k + 1));
        if (hi.x > 1e-9) face({0, hi.y, 0}, at(hi, k + 1), at(hi, k));
    }
    return out;
}

// Triangles moved by `by` and turned `yaw` about y then `pitch` about z (to
// stand a lathed barrel along x, say) - to put several together as one model.
inline std::vector<float> placed(std::vector<float> v, const Vec3d& by, double yaw = 0.0, double pitch = 0.0) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const auto turn = [&](double& x, double& y, double& z) {
        // about z (pitch), then about y (yaw)
        const double x1 = x * cp - y * sp, y1 = x * sp + y * cp;
        const double x2 = x1 * cy + z * sy, z2 = -x1 * sy + z * cy;
        x = x2, y = y1, z = z2;
    };
    for (std::size_t i = 0; i + 8 <= v.size(); i += 8) {
        double x = v[i], y = v[i + 1], z = v[i + 2], nx = v[i + 3], ny = v[i + 4], nz = v[i + 5];
        turn(x, y, z);
        turn(nx, ny, nz);
        v[i] = static_cast<float>(x + by.x), v[i + 1] = static_cast<float>(y + by.y), v[i + 2] = static_cast<float>(z + by.z);
        v[i + 3] = static_cast<float>(nx), v[i + 4] = static_cast<float>(ny), v[i + 5] = static_cast<float>(nz);
    }
    return v;
}
inline std::vector<float> joined(std::initializer_list<std::vector<float>> parts) {
    std::vector<float> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// Into the unit box a mesh is sized from - centred across, its base at
// -0.5 - and its size, for the element's sx, sy, sz: drawn at that size it
// is as made. (Its normals are set so that stretched back to that size they
// come out true.)
// The box round some triangles: its low and high corners.
inline void bounds(const std::vector<float>& v, Vec3d& lo, Vec3d& hi) {
    lo = {1e18, 1e18, 1e18}, hi = {-1e18, -1e18, -1e18};
    for (std::size_t i = 0; i + 8 <= v.size(); i += 8) {
        lo = {std::min(lo.x, static_cast<double>(v[i])), std::min(lo.y, static_cast<double>(v[i + 1])), std::min(lo.z, static_cast<double>(v[i + 2]))};
        hi = {std::max(hi.x, static_cast<double>(v[i])), std::max(hi.y, static_cast<double>(v[i + 1])), std::max(hi.z, static_cast<double>(v[i + 2]))};
    }
}
// As fit, but into the box round `within` (made with others, to be drawn
// at one place and size and so fit together - the parts of a gun, each its
// own colour).
inline std::vector<float> fit(std::vector<float> v, Vec3d& size, const Vec3d& within_lo, const Vec3d& within_hi);
inline std::vector<float> fit(std::vector<float> v, Vec3d& size) {
    Vec3d lo, hi;
    bounds(v, lo, hi);
    return fit(std::move(v), size, lo, hi);
}
inline std::vector<float> fit(std::vector<float> v, Vec3d& size, const Vec3d& within_lo, const Vec3d& within_hi) {
    const double lo[3] = {within_lo.x, within_lo.y, within_lo.z}, hi[3] = {within_hi.x, within_hi.y, within_hi.z};
    double s[3];
    for (int k = 0; k < 3; ++k) s[k] = std::max(hi[k] - lo[k], 1e-6);
    size = {s[0], s[1], s[2]};
    for (std::size_t i = 0; i + 8 <= v.size(); i += 8) {
        v[i] = static_cast<float>((v[i] - (lo[0] + hi[0]) * 0.5) / s[0]);
        v[i + 1] = static_cast<float>((v[i + 1] - lo[1]) / s[1] - 0.5);
        v[i + 2] = static_cast<float>((v[i + 2] - (lo[2] + hi[2]) * 0.5) / s[2]);
        // Stretched by s again, a normal n comes out as s n; what is wanted
        // is n / s: so it is kept as n / s^2.
        const Vec3d n = detail::unit({v[i + 3] / (s[0] * s[0]), v[i + 4] / (s[1] * s[1]), v[i + 5] / (s[2] * s[2])});
        v[i + 3] = static_cast<float>(n.x), v[i + 4] = static_cast<float>(n.y), v[i + 5] = static_cast<float>(n.z);
    }
    return v;
}

}  // namespace sg::shapes
