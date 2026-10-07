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
//             faces - a barrel, a turret's body, a lamp (lathe_smooth: polished)
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
void corner(std::vector<float>& out, const Vec3d& p, const Vec3d& n, double u, double v);
// A face of its own, flat: its normal from its corners (counter-clockwise
// seen from outside).
void tri(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c);
void quad(std::vector<float>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d);
double area(const std::vector<P2>& p);
// An outline cut into triangles, ear by ear (it may be concave): indices.
std::vector<int> ears(const std::vector<P2>& p);
// The outline pulled in by `by` all round (each corner along its bisector).
std::vector<P2> inset(const std::vector<P2>& p, double by);
}  // namespace detail

// `outline` (counter-clockwise or not) pushed out to `depth` along z, centred
// on z = 0, the edges round both faces cut back by `chamfer`.
std::vector<float> extrude(std::vector<P2> outline, double depth, double chamfer = 0.0);

// `profile` - (radius, height) from the bottom up - turned about y in `sides`
// faces; closed at either end where its radius there is not nothing.
std::vector<float> lathe(const std::vector<P2>& profile, int sides);
// As lathe, but turned smooth: round the axis and along the profile its
// normals run on from face to face - a thing turned on a lathe, polished -
// except where the profile turns sharper than `crease` (radians), which is
// left an edge (a rim, a step).
std::vector<float> lathe_smooth(const std::vector<P2>& profile, int sides, double crease = 0.7);

// Triangles moved by `by` and turned `yaw` about y then `pitch` about z (to
// stand a lathed barrel along x, say) - to put several together as one model.
std::vector<float> placed(std::vector<float> v, const Vec3d& by, double yaw = 0.0, double pitch = 0.0);
std::vector<float> joined(std::initializer_list<std::vector<float>> parts);

// Into the unit box a mesh is sized from - centred across, its base at
// -0.5 - and its size, for the element's sx, sy, sz: drawn at that size it
// is as made. (Its normals are set so that stretched back to that size they
// come out true.)
// The box round some triangles: its low and high corners.
void bounds(const std::vector<float>& v, Vec3d& lo, Vec3d& hi);
// As fit, but into the box round `within` (made with others, to be drawn
// at one place and size and so fit together - the parts of a gun, each its
// own colour).
std::vector<float> fit(std::vector<float> v, Vec3d& size, const Vec3d& within_lo, const Vec3d& within_hi);
std::vector<float> fit(std::vector<float> v, Vec3d& size);
std::vector<float> fit(std::vector<float> v, Vec3d& size, const Vec3d& within_lo, const Vec3d& within_hi);

}  // namespace sg::shapes
