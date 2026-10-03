// Stategine - walking: whoever stands, on whatever ground the space they are
// in says is down.
//
// A space says what pulls by its `field` elements (fields_of) and what stops
// by its solid ones: walls, and meshes that say `solid` (boxes, or spheres -
// `shape` = sphere). A walker is a camera with a velocity (`vx vy vz`) that
// stands on ground of its own (sg::standing): each step it turns that ground
// to stand against the pull where it is, goes along it, falls when nothing
// holds it, lands, and is stopped. On level ground under ordinary gravity it
// is the walker every room already had; on a cube whose faces pull, it walks
// over the edge onto the next face; on a planet, round it.
//
// It is a pure step - of the walker's params, the space, the pull and the
// step's arguments - for a state's own arrow to take (the laws re-run it).
#pragma once

#include <vector>

#include "sg/domains/Spatial.hpp"
#include "sg/physics/Field.hpp"

namespace sg {

// The fields a state declares, each a source posed where its element is (its
// anchors' whole turn with it). A `field` element says:
//   channel        what it is a field of (gravity)
//   field_shape    directional, radial, plane or box
//   strength, exponent, softening, scalar
//   radius         local support: none past this far (absent: everywhere)
//   dx dy dz       a directional field's vector
//   nx ny nz       a plane's normal, in its own frame
//   sx sy sz       a box's size; face_px face_nx face_py face_ny face_pz
//                  face_nz how strong each face is, as a share of strength
// The state's own `g`, if it says one, is ordinary gravity down its -y.
std::vector<field::Source> fields_of(const State& s);

// What a walker means to do this step: how far forward and to the right (-1
// to 1, of `speed` metres a second), how its look turns (radians), and
// whether it jumps.
struct Stride {
    double forward = 0, right = 0, turn = 0, look = 0, speed = 3.0;
    bool jump = false;
    // Off its feet, how hard it pushes the way it looks (m/s/s): a jetpack.
    double thrust = 0;
};

// What stops a walker stops a ray: the first solid along it from `eye`
// within `reach` - where, how far, the way its surface faces there, and which
// element it is.
bool ray(const State& space, const Vec3d& eye, const Vec3d& dir, double reach, Vec3d& hit, Vec3d& normal, double* dist = nullptr,
         Key* what = nullptr);

// One step of `walker` through `space`, pulled by `pull` (fields_of(space),
// solved), `dt` long, ending at `time`. It reads and writes the walker's
// params only: x y z (its eye), vx vy vz, its ground (stand_*), yaw and pitch
// (its look within that ground), and `grounded`. Its `height` (eye above its
// feet, 1.65) and `radius` (0.3) are its own. A doorway (a portal that says
// `walk`) lets through whoever walks into it, wherever it stands: what is
// solid behind its opening does not stop them while they are in it.
void walk(const State& space, const field::Solver& pull, Element& walker, const Stride& in, double dt, double time);

}  // namespace sg
