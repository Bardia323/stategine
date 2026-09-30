// Geometry only: no state, graph, ownership or time.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace sg::spatial {
// --- vectors and turns ---------------------------------------------------------------
struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline V3 operator*(double k, V3 a) { return a * k; }
inline V3& operator+=(V3& a, V3 b) { return a = a + b; }
inline V3& operator-=(V3& a, V3 b) { return a = a - b; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
// inline: collision and solving evaluate this kernel per contact.
inline V3 normalize(V3 a) {
    const double l = length(a);
    return l > 1e-12 ? a * (1.0 / l) : V3{0, 1, 0};
}

// Row-major 3x3.
struct M3 {
    std::array<double, 9> a{1, 0, 0, 0, 1, 0, 0, 0, 1};
    double operator()(int r, int c) const { return a[static_cast<std::size_t>(r * 3 + c)]; }
    double& operator()(int r, int c) { return a[static_cast<std::size_t>(r * 3 + c)]; }
    V3 col(int c) const { return {(*this)(0, c), (*this)(1, c), (*this)(2, c)}; }
};
// inline: collision and solving evaluate this kernel per contact.
inline V3 operator*(const M3& m, V3 v) {
    return {m.a[0] * v.x + m.a[1] * v.y + m.a[2] * v.z, m.a[3] * v.x + m.a[4] * v.y + m.a[5] * v.z,
            m.a[6] * v.x + m.a[7] * v.y + m.a[8] * v.z};
}
// inline: collision and solving evaluate this kernel per contact.
inline M3 operator*(const M3& p, const M3& q) {
    M3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c(i, j) = p(i, 0) * q(0, j) + p(i, 1) * q(1, j) + p(i, 2) * q(2, j);
    return c;
}
M3 operator+(const M3& p, const M3& q);
M3 operator*(const M3& p, double k);
// inline: collision and solving evaluate this kernel per contact.
inline M3 transpose(const M3& m) {
    M3 t;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t(i, j) = m(j, i);
    return t;
}
inline M3 zero3() { M3 z; z.a.fill(0.0); return z; }
M3 outer(V3 u, V3 v);
M3 skew(V3 v);
M3 inverse(const M3& m);
// A turn of `angle` about the unit `axis` (Rodrigues).
M3 axis_angle(V3 k, double angle);
// Back to a rotation, after many small turns have been multiplied in.
M3 orthonormal(const M3& m);
// The turn that takes the identity to `r`, as axis times angle.
V3 log_map(const M3& r);

// The renderer's turn: R = Ry(-yaw) Rz(pitch) Rx(roll), as a mesh is turned.
M3 from_euler(double yaw, double pitch, double roll);
void to_euler(const M3& m, double& yaw, double& pitch, double& roll);

} // namespace sg::spatial
