// A model with its joints moved: each part carried by its joint (`moves`) and
// by every joint that one rides on - a leaf on its hinge, the second fold of
// a folding door on the first, a drawer on its runners.
#include <algorithm>
#include <cmath>
#include <functional>

#include "sg/domains/Modeler.hpp"

namespace sg::sculpt {

namespace {
// A rigid move: p -> r p + t.
struct Rigid {
    double r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3d t;
    Vec3d turn(const Vec3d& v) const {
        return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z, r[6] * v.x + r[7] * v.y + r[8] * v.z};
    }
    Vec3d apply(const Vec3d& p) const { return turn(p) + t; }
};
Rigid then(const Rigid& a, const Rigid& b) {  // b first, then a
    Rigid c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.r[i * 3 + j] = a.r[i * 3] * b.r[j] + a.r[i * 3 + 1] * b.r[3 + j] + a.r[i * 3 + 2] * b.r[6 + j];
    c.t = a.apply(b.t);
    return c;
}
// One joint at a value: a turn about its axis through `at` (Rodrigues), or a slide along it.
// A point `s` along a track (beyond its ends, on along its first or last way).
Vec3d along(const std::vector<Vec3d>& path, double s) {
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const Vec3d d = path[i + 1] - path[i];
        const double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (l < 1e-12) continue;
        if (s <= l || i + 2 == path.size() || (i == 0 && s < 0)) return path[i] + d * (s / l);
        s -= l;
    }
    return path.empty() ? Vec3d{} : path.back();
}
Rigid turn_about(const Vec3d& axis, double a, const Vec3d& at);
Rigid at_value(const Joint& j, double v) {
    Rigid m;
    if (!j.path.empty()) {
        // Riding the track: its two points carried v further along, the
        // part turned about the axis as the chord between them turns.
        const Vec3d p0 = along(j.path, j.from), p1 = along(j.path, j.from + j.span);
        const Vec3d q0 = along(j.path, j.from + v), q1 = along(j.path, j.from + v + j.span);
        const Vec3d d1 = p1 - p0, d2 = q1 - q0, a = j.axis;
        const Vec3d c{d1.y * d2.z - d1.z * d2.y, d1.z * d2.x - d1.x * d2.z, d1.x * d2.y - d1.y * d2.x};
        const double turned = std::atan2(c.x * a.x + c.y * a.y + c.z * a.z, d1.x * d2.x + d1.y * d2.y + d1.z * d2.z);
        m = turn_about(a, turned * 180 / 3.14159265358979323846, p0);
        m.t = m.t + (q0 - p0);
        return m;
    }
    if (j.slide) {
        m.t = j.axis * v;
        return m;
    }
    return turn_about(j.axis, v, j.at);
}
// A turn of `deg` degrees about `axis` (right-handed) through `at`.
Rigid turn_about(const Vec3d& axis, double deg, const Vec3d& at) {
    Rigid m;
    const double a = deg * 3.14159265358979323846 / 180, c = std::cos(a), s = std::sin(a), k = 1 - c;
    const double x = axis.x, y = axis.y, z = axis.z;
    const double r[9] = {c + x * x * k, x * y * k - z * s, x * z * k + y * s, y * x * k + z * s, c + y * y * k,
                         y * z * k - x * s, z * x * k - y * s, z * y * k + x * s, c + z * z * k};
    std::copy(r, r + 9, m.r);
    m.t = at - m.turn(at);
    return m;
}
}  // namespace

std::vector<Moved> moves_of(const Model& m, const std::vector<std::pair<std::string, double>>& values) {
    const std::size_t n = m.joints.size();
    std::vector<double> value(n, 0);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& [name, v] : values)
            if (m.joints[i].name == name) value[i] = std::clamp(v, m.joints[i].lo, m.joints[i].hi);
    // Each joint's move, with the moves of those it rides on: a parent first.
    std::vector<Rigid> move(n);
    std::vector<int> done(n, 0);
    const std::function<void(std::size_t)> make = [&](std::size_t i) {
        if (done[i]) return;
        done[i] = 1;
        const Joint& j = m.joints[i];
        const bool rides = j.parent >= 0 && std::size_t(j.parent) < n && std::size_t(j.parent) != i;
        if (rides) make(std::size_t(j.parent));
        const bool goes_with = j.with >= 0 && std::size_t(j.with) < n && std::size_t(j.with) != i;
        if (goes_with) make(std::size_t(j.with)), value[i] = value[std::size_t(j.with)] * j.follow;
        else if (rides && j.follow != 0) value[i] = value[std::size_t(j.parent)] * j.follow;
        move[i] = rides ? then(move[std::size_t(j.parent)], at_value(j, value[i])) : at_value(j, value[i]);
    };
    for (std::size_t i = 0; i < n; ++i) make(i);
    std::vector<Moved> out(n);
    for (std::size_t i = 0; i < n; ++i) std::copy(move[i].r, move[i].r + 9, out[i].r), out[i].t = move[i].t;
    return out;
}

Model pose(const Model& m, const std::vector<std::pair<std::string, double>>& values) {
    const std::size_t n = m.joints.size();
    const std::vector<Moved> moved = moves_of(m, values);
    std::vector<Rigid> move(n);
    for (std::size_t i = 0; i < n; ++i) std::copy(moved[i].r, moved[i].r + 9, move[i].r), move[i].t = moved[i].t;
    Model out = m;
    bool any = false;
    for (Part& p : out.parts) {
        std::size_t i = 0;
        while (i < n && m.joints[i].name != p.joint) ++i;
        if (p.joint.empty() || i == n) continue;
        any = true;
        for (std::size_t c = 0; c + 7 < p.corners.size(); c += 8) {
            const Vec3d q = move[i].apply({p.corners[c], p.corners[c + 1], p.corners[c + 2]});
            const Vec3d nn = move[i].turn({p.corners[c + 3], p.corners[c + 4], p.corners[c + 5]});
            p.corners[c] = float(q.x), p.corners[c + 1] = float(q.y), p.corners[c + 2] = float(q.z);
            p.corners[c + 3] = float(nn.x), p.corners[c + 4] = float(nn.y), p.corners[c + 5] = float(nn.z);
        }
    }
    if (!any) return out;
    // The box round it, as it now stands.
    out.lo = {1e30, 1e30, 1e30}, out.hi = {-1e30, -1e30, -1e30};
    for (const Part& p : out.parts)
        for (std::size_t c = 0; c + 7 < p.corners.size(); c += 8) {
            out.lo = {std::min(out.lo.x, double(p.corners[c])), std::min(out.lo.y, double(p.corners[c + 1])), std::min(out.lo.z, double(p.corners[c + 2]))};
            out.hi = {std::max(out.hi.x, double(p.corners[c])), std::max(out.hi.y, double(p.corners[c + 1])), std::max(out.hi.z, double(p.corners[c + 2]))};
        }
    return out;
}

Model opened(const Model& m, double open) {
    std::vector<std::pair<std::string, double>> values;
    for (const Joint& j : m.joints) {
        // From where it is as made (0) toward the far end of its way.
        const double far = std::abs(j.hi) >= std::abs(j.lo) ? j.hi : j.lo;
        values.push_back({j.name, far * open});
    }
    return pose(m, values);
}

}  // namespace sg::sculpt
