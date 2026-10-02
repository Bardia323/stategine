// Stategine - the small slice of linear algebra the renderer needs.
#pragma once

#include <cmath>

namespace sg::spatial::projection {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b);

// Rotate a vector about the vertical axis. Portals are hinged that way: the
// step from one doorway into another is a yaw difference and a translation.
Vec3 rotate_y(const Vec3& v, float a);

Vec3 normalize(const Vec3& v);

// Column-major 4x4, laid out exactly as glUniformMatrix4fv wants it.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }

    static Mat4 translate(const Vec3& t);

    static Mat4 scale(const Vec3& s);

    // Yaw is a heading measured from +x, so this is the rotation that takes
    // (1,0,0) to (cos a, 0, sin a) - the same one sg::compose_pose applies to
    // positions. Keeping those two in step is what lets a pose become a model
    // matrix without a hidden mirror.
    static Mat4 rotate_y(float a);

    // Pitch: tips +x up towards +y, the way sg::forward_of raises a heading.
    // Applied before the yaw, it tilts something about its own sideways axis.
    static Mat4 rotate_z(float a);

    // Roll: turns +y towards +z, spinning something about the way it faces.
    static Mat4 rotate_x(float a);

    static Mat4 perspective(float fov_y_rad, float aspect, float znear, float zfar);

    // A box of view: parallel projection, for a light as far off as the sun.
    static Mat4 ortho(float l, float r, float b, float t, float znear, float zfar);

    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up);

    Mat4 operator*(const Mat4& o) const;

    Vec3 transform_point(const Vec3& p) const;
};

}  // namespace sg::spatial::projection
