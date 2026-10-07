#include "sg/domains/Shapes.hpp"

namespace sg::shapes::detail {

void corner(std::vector<float>& out, const Vec3d& p, const Vec3d& n, double u, double v) {
    out.insert(out.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), static_cast<float>(n.x),
                           static_cast<float>(n.y), static_cast<float>(n.z), static_cast<float>(u), static_cast<float>(v)});
}

void tri(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c) {
    const Vec3d n = unit(cross(b - a, c - a));
    corner(out, a, n, 0, 0);
    corner(out, b, n, 1, 0);
    corner(out, c, n, 1, 1);
}

void quad(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d) {
    tri(out, a, b, c);
    tri(out, a, c, d);
}

double area(const std::vector<P2>& p) {
    double s = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const P2& a = p[i];
        const P2& b = p[(i + 1) % p.size()];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

std::vector<int> ears(const std::vector<P2>& p) {
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

std::vector<P2> inset(const std::vector<P2>& p, double by) {
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

}  // namespace sg::shapes::detail

namespace sg::shapes {

std::vector<float> extrude(std::vector<P2> outline, double depth, double chamfer) {
    using namespace detail;
    std::vector<float> out;
    // A corner said twice is one corner: an arch with no legs puts its
    // springing where its foot is, and a corner of no size is never an ear.
    std::vector<P2> once;
    for (const P2& q : outline)
        if (once.empty() || std::hypot(q.x - once.back().x, q.y - once.back().y) > 1e-9) once.push_back(q);
    while (once.size() > 1 && std::hypot(once.front().x - once.back().x, once.front().y - once.back().y) <= 1e-9) once.pop_back();
    outline.swap(once);
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

std::vector<float> lathe(const std::vector<P2>& profile, int sides) {
    using namespace detail;
    std::vector<float> out;
    sides = std::max(3, sides);
    const auto at = [&](const P2& p, int k) {
        const double a = 2.0 * 3.14159265358979 * k / sides;
        return Vec3d{p.x * std::cos(a), p.y, p.x * std::sin(a)};
    };
    // A face, unless it has come to nothing (where the radius is 0).
    const auto face = [&](const Vec3d& a, const Vec3d& b, const Vec3d& c) {
        const Vec3d n = cross(b - a, c - a);
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

std::vector<float> lathe_smooth(const std::vector<P2>& profile, int sides, double crease) {
    using namespace detail;
    std::vector<float> out;
    sides = std::max(3, sides);
    const std::size_t n = profile.size();
    if (n < 2) return out;
    // Each step of the profile's own normal, outward: (rise, -run).
    std::vector<P2> along(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double dx = profile[i + 1].x - profile[i].x, dy = profile[i + 1].y - profile[i].y, l = std::hypot(dx, dy);
        along[i] = l > 1e-12 ? P2{dy / l, -dx / l} : P2{1, 0};
    }
    // At a point of the profile, the normal a step uses there: shared with
    // the step beside it, unless the two meet at an edge.
    const auto at_point = [&](std::size_t step, std::size_t point) {
        const std::size_t other = point == step ? step - 1 : step + 1;
        if ((point == step && step == 0) || (point != step && step + 1 >= along.size())) return along[step];
        const P2 a = along[step], b = along[other];
        if (std::acos(std::clamp(a.x * b.x + a.y * b.y, -1.0, 1.0)) > crease) return a;
        const double l = std::hypot(a.x + b.x, a.y + b.y);
        return l > 1e-12 ? P2{(a.x + b.x) / l, (a.y + b.y) / l} : a;
    };
    const auto ring = [&](const P2& p, int k) {
        const double a = 2.0 * 3.14159265358979 * k / sides;
        return Vec3d{p.x * std::cos(a), p.y, p.x * std::sin(a)};
    };
    const auto turn = [&](const P2& m, int k) {
        const double a = 2.0 * 3.14159265358979 * k / sides;
        return unit(Vec3d{m.x * std::cos(a), m.y, m.x * std::sin(a)});
    };
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const P2 m0 = at_point(i, i), m1 = at_point(i, i + 1);
        for (int k = 0; k < sides; ++k) {
            const Vec3d a = ring(profile[i], k), b = ring(profile[i], k + 1), c = ring(profile[i + 1], k + 1), d = ring(profile[i + 1], k);
            const Vec3d na = turn(m0, k), nb = turn(m0, k + 1), nc = turn(m1, k + 1), nd = turn(m1, k);
            const double u0 = double(k) / sides, u1 = double(k + 1) / sides, v0 = double(i) / (n - 1), v1 = double(i + 1) / (n - 1);
            const auto face = [&](const Vec3d& p, const Vec3d& np, double up, double vp, const Vec3d& q, const Vec3d& nq, double uq, double vq,
                                  const Vec3d& r, const Vec3d& nr, double ur, double vr) {
                const Vec3d f = cross(q - p, r - p);
                if (f.x * f.x + f.y * f.y + f.z * f.z <= 1e-20) return;
                corner(out, p, np, up, vp), corner(out, q, nq, uq, vq), corner(out, r, nr, ur, vr);
            };
            face(a, na, u0, v0, d, nd, u0, v1, c, nc, u1, v1);
            face(a, na, u0, v0, c, nc, u1, v1, b, nb, u1, v0);
        }
    }
    // The ends, flat: the bottom facing down, the top up.
    const P2& lo = profile.front();
    const P2& hi = profile.back();
    for (int k = 0; k < sides; ++k) {
        if (lo.x > 1e-9) tri(out, {0, lo.y, 0}, ring(lo, k), ring(lo, k + 1));
        if (hi.x > 1e-9) tri(out, {0, hi.y, 0}, ring(hi, k + 1), ring(hi, k));
    }
    return out;
}

std::vector<float> placed(std::vector<float> v, const Vec3d& by, double yaw, double pitch) {
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

std::vector<float> joined(std::initializer_list<std::vector<float>> parts) {
    std::vector<float> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

void bounds(const std::vector<float>& v, Vec3d& lo, Vec3d& hi) {
    lo = {1e18, 1e18, 1e18}, hi = {-1e18, -1e18, -1e18};
    for (std::size_t i = 0; i + 8 <= v.size(); i += 8) {
        lo = {std::min(lo.x, static_cast<double>(v[i])), std::min(lo.y, static_cast<double>(v[i + 1])), std::min(lo.z, static_cast<double>(v[i + 2]))};
        hi = {std::max(hi.x, static_cast<double>(v[i])), std::max(hi.y, static_cast<double>(v[i + 1])), std::max(hi.z, static_cast<double>(v[i + 2]))};
    }
}

std::vector<float> fit(std::vector<float> v, Vec3d& size) {
    Vec3d lo, hi;
    bounds(v, lo, hi);
    return fit(std::move(v), size, lo, hi);
}

std::vector<float> fit(std::vector<float> v, Vec3d& size, const Vec3d& within_lo, const Vec3d& within_hi) {
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
        const Vec3d n = unit({v[i + 3] / (s[0] * s[0]), v[i + 4] / (s[1] * s[1]), v[i + 5] / (s[2] * s[2])});
        v[i + 3] = static_cast<float>(n.x), v[i + 4] = static_cast<float>(n.y), v[i + 5] = static_cast<float>(n.z);
    }
    return v;
}

}  // namespace sg::shapes
