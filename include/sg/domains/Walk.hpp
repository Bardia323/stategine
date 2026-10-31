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

// A ray as an eye sees along it: through the doorways a seam glues to
// another room (or to elsewhere in its own), on into the room beyond, as far
// as `reach` all told - so a lens focused by it, looking through an open door,
// is focused on what is past it, not on the doorway's plane or the wall a
// doorway hangs on. A doorway is a `portal` that is walked (`walk`); one no
// seam glues is a hole like any other. `where` is the state it stopped in;
// `dist` is the whole way, every hop added. At most `hops` doorways.
class StateGraph;
bool ray_seen(const StateGraph& graph, const State& space, const Vec3d& eye, const Vec3d& dir, double reach, Vec3d& hit, Vec3d& normal,
              double* dist = nullptr, Key* what = nullptr, const State** where = nullptr, int hops = 4);

// Many rays at once: what stops them gathered once for all of them, each
// answered as `ray` answers it alone, in the order asked. A wide batch goes
// across the cores, each ray on its own - the same answers whatever ran
// them. (A ray's procedure, as a device would run it: its inputs the
// state's own, its answer a function of them.)
struct RayAsk {
    Vec3d eye, dir;
    double reach = 1.0;
};
struct RayHit {
    bool hit = false;
    Vec3d at, normal;
    double dist = 0.0;
    Key what;
};
std::vector<RayHit> rays(const State& space, const std::vector<RayAsk>& asks);

// What stops whoever stands at `walker`'s eye in `space`: they are put
// clear of every solid - their feet, their middle and their head each a ball
// `radius` round - and let through what a doorway opens. The one answer to
// "what stops a walker", for every place: the walk below takes it, and so
// does any place that moves its walker its own way.
void stand_clear(const State& space, Element& walker, double radius = 0.3);

// Rooms made one (sg::make_space) whose holes - where a doorway between
// two of them was - cannot be walked through: a walker of `radius` and
// `height` steered through each one's middle, from a pace on one side to a
// pace on the other - stopped by something solid across it (a thing's box
// over the opening, a wall left closed there) is a fault, said with where.
std::vector<std::string> space_faults(const State& space, double radius = 0.3, double height = 1.65);
// And every doorway walked through (a portal that says `walk`, not `blind`):
// a walker steered at its middle from a pace inside the room reaches its
// plane - stopped by something solid before it is a fault, said with where.
std::vector<std::string> doorway_faults(const State& space, double radius = 0.3, double height = 1.65);

// One step of `walker` through `space`, pulled by `pull` (fields_of(space),
// solved), `dt` long, ending at `time`. It reads and writes the walker's
// params only: x y z (its eye), vx vy vz, its ground (stand_*), yaw and pitch
// (its look within that ground), and `grounded` - which stays a moment
// (`grace`, 0.12 s) after its feet leave the ground, unless it jumped (`air`:
// how long they have) - and `sliding`: on land steeper than `max_slope` (0.8
// rad) it cannot walk up it, and slides down it. Its `height` (eye above its
// feet, 1.65) and `radius` (0.3) are its own. A doorway (a portal that says
// `walk`) lets through whoever walks into it, wherever it stands: what is
// solid behind its opening does not stop them while they are in it.
void walk(const State& space, const field::Solver& pull, Element& walker, const Stride& in, double dt, double time);

}  // namespace sg
