#include "sg/spatial/Math.hpp"
namespace sg::spatial {






M3 operator+(const M3& p, const M3& q) {
    M3 c;
    for (std::size_t i = 0; i < 9; ++i) c.a[i] = p.a[i] + q.a[i];
    return c;
}

M3 operator*(const M3& p, double k) {
    M3 c;
    for (std::size_t i = 0; i < 9; ++i) c.a[i] = p.a[i] * k;
    return c;
}



M3 outer(V3 u, V3 v) {
    M3 m;
    m.a = {u.x * v.x, u.x * v.y, u.x * v.z, u.y * v.x, u.y * v.y, u.y * v.z, u.z * v.x, u.z * v.y, u.z * v.z};
    return m;
}

M3 skew(V3 v) {
    M3 m;
    m.a = {0, -v.z, v.y, v.z, 0, -v.x, -v.y, v.x, 0};
    return m;
}

M3 inverse(const M3& m) {
    const double c00 = m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1), c01 = m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2),
                 c02 = m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0);
    const double det = m(0, 0) * c00 + m(0, 1) * c01 + m(0, 2) * c02;
    if (std::fabs(det) < 1e-18) return zero3();
    const double k = 1.0 / det;
    M3 r;
    r.a = {c00 * k,
           (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * k,
           (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * k,
           c01 * k,
           (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * k,
           (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) * k,
           c02 * k,
           (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) * k,
           (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * k};
    return r;
}

M3 axis_angle(V3 k, double angle) {
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    M3 q;
    q.a = {t * k.x * k.x + c,       t * k.x * k.y - s * k.z, t * k.x * k.z + s * k.y,
           t * k.x * k.y + s * k.z, t * k.y * k.y + c,       t * k.y * k.z - s * k.x,
           t * k.x * k.z - s * k.y, t * k.y * k.z + s * k.x, t * k.z * k.z + c};
    return q;
}

M3 orthonormal(const M3& m) {
    V3 x = normalize(m.col(0)), y = m.col(1);
    y = normalize(y - x * dot(x, y));
    const V3 z = cross(x, y);
    M3 r;
    r.a = {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
    return r;
}

V3 log_map(const M3& r) {
    const double c = std::clamp((r(0, 0) + r(1, 1) + r(2, 2) - 1.0) * 0.5, -1.0, 1.0);
    const double angle = std::acos(c);
    const V3 v{r(2, 1) - r(1, 2), r(0, 2) - r(2, 0), r(1, 0) - r(0, 1)};
    if (angle < 1e-6) return v * 0.5;
    if (angle > 3.14159265358979 - 1e-4) {
        // Half a turn: the axis is the column of r + I that is longest.
        M3 p = r + M3{};
        V3 best = p.col(0);
        for (int i = 1; i < 3; ++i)
            if (length(p.col(i)) > length(best)) best = p.col(i);
        return normalize(best) * angle;
    }
    return v * (angle / (2.0 * std::sin(angle)));
}

M3 from_euler(double yaw, double pitch, double roll) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll),
                 sr = std::sin(roll);
    M3 ry, rz, rx;
    ry.a = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
    rz.a = {cp, -sp, 0, sp, cp, 0, 0, 0, 1};
    rx.a = {1, 0, 0, 0, cr, -sr, 0, sr, cr};
    return ry * (rz * rx);
}

void to_euler(const M3& m, double& yaw, double& pitch, double& roll) {
    pitch = std::asin(std::clamp(m(1, 0), -1.0, 1.0));
    if (std::fabs(std::cos(pitch)) > 1e-6) {
        roll = std::atan2(-m(1, 2), m(1, 1));
        yaw = std::atan2(m(2, 0), m(0, 0));
    } else {
        roll = 0.0;
        yaw = std::atan2(-m(0, 2), m(2, 2));
    }
}


} // namespace sg::spatial
