// Reusable geometry. Coordinates and bounds have no domain meaning here.
#pragma once
#include "sg/spatial/Math.hpp"
#include <limits>
#include <vector>

namespace sg::spatial {

// Full affine 3D transform: translation, rotation, scale and shear.
struct Transform {
    M3 linear;
    V3 translation;
    V3 point(V3 p) const { return translation + linear * p; }
    V3 vector(V3 v) const { return linear * v; }
    Transform inverse() const;
    Transform operator*(const Transform& other) const;
};

struct Aabb {
    V3 lo{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    V3 hi{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    bool empty() const;
    void include(V3 p);
    void include(const Aabb& b);
    bool contains(V3 p) const;
    bool overlaps(const Aabb& b) const;
    Aabb expanded(double margin) const;
    Aabb transformed(const Transform& t) const;
};

struct Ray {
    V3 origin, direction;
    double near = 0, far = std::numeric_limits<double>::infinity();
};
bool intersects(const Aabb& box, const Ray& ray, double* first = nullptr);

// Positive side is inside. Normal need not be a unit vector.
struct HalfSpace {
    V3 normal;
    double offset = 0;
    double at(V3 p) const { return dot(normal, p) + offset; }
    HalfSpace transformed(const Transform& t) const;
};

struct ConvexVolume {
    std::vector<HalfSpace> planes;
    bool contains(V3 p) const;
    bool intersects(const Aabb& b) const;
    bool intersects_sphere(V3 centre, double radius) const;
    ConvexVolume transformed(const Transform& t) const;
    // Column-major clip matrix; OpenGL's -w..w clip convention.
    static ConvexVolume clip(const std::array<double, 16>& matrix);
};

// A perspective cone (+z in its local coordinates) and a finite plane.
struct Projector {
    Transform pose;
    double half_x = 0.5, half_y = 0.5; // tangents of the half angles
    double near = 0.01, far = 100;
    ConvexVolume volume() const;
};
struct Surface {
    V3 centre, u{1, 0, 0}, v{0, 1, 0}; // orthonormal axes
    double half_u = 1, half_v = 1;
};
struct Projection {
    std::vector<V3> polygon;
    double u0 = 0, v0 = 0, u1 = 0, v1 = 0; // normalized surface crop
};
// Geometry only: the caller's state arrow decides what this means.
Projection project(const Projector& projector, const Surface& surface);

} // namespace sg::spatial
