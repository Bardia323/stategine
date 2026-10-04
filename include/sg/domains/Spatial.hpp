// Stategine - the spatial domain: things with a position that integrate.
//
// One class covers both the 2D and the 3D case, because the only difference is
// how many axes the integrator touches. Nothing here draws: a spatial state is
// data plus arrows, and a renderer (terminal, OpenGL, anything) is a separate
// object that reads it. That separation is what lets the same state show up as
// a map on a wall, a debug view in the console and a lit room at once.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

class SpatialState : public State {
public:
    SpatialState(Key id, int dims);

    Key kind() const override { return dims_ == 3 ? Key{"space3d"} : Key{"space2d"}; }

    int dims() const { return dims_; }
    Key step_event() const { return step_event_; }
    static Key camera_id() { return Key{"camera"}; }

    // A body: position, velocity, colour, glyph - the vocabulary every renderer
    // and every functor in the engine already understands.
    Element& body(Key id, Vec3d pos, Key kind = Key{}, char glyph = '*');

    // Endomorphism on one body: velocity integrates into position. Registered
    // per element so the arrow really is an arrow of this category, visible in
    // the DOT output and checkable by validate().
    // Declared (sg/core/Declared.hpp): x <- x + vx dt, and so on - so the
    // laws can check it without running it (sg/algebra).
    const Morphism& add_integrator(Key id);

    void on_update(const Tick& t) override;

    // Off for states that are edited rather than simulated (a map, a menu).
    void set_integrating(bool on) { integrate_ = on; }
    bool integrating() const { return integrate_; }

    Element& camera() { return element(camera_id()); }
    const Element& camera() const { return element(camera_id()); }

    // Who crosses a space's doorways is its eye: through one that is walked
    // (`walk`, and not only a way in - `leave` 0), front to back.
    Params passage() const override;
    bool passed(const Params& before, Key boundary) const override;
    // At a doorway: whether it is walked, which way its pull is and where its
    // ground is, just inside it (State::overlap).
    Params overlap(Key boundary) const override;

protected:
    virtual Key default_kind() const { return dims_ == 3 ? kinds::mesh : kinds::sprite; }

private:
    int dims_;
    Key step_event_;
    bool integrate_ = true;
};

// --- the two everyday shapes -------------------------------------------------
class Spatial2D : public SpatialState {
public:
    explicit Spatial2D(Key id, int cols = 40, int rows = 16);

    // Convenience with the 2D reading of the axes.
    Element& sprite(Key id, double x, double y, char glyph = '*');

    int cols() const { return static_cast<int>(params().num(keys::w, 40)); }
    int rows() const { return static_cast<int>(params().num(keys::h, 16)); }
};

class Spatial3D : public SpatialState {
public:
    explicit Spatial3D(Key id);

    Element& mesh(Key id, double x, double y, double z, char glyph = '#');

    // A mesh that stays where it is put: architecture, a leaf of a tree, a
    // part hung off an anchor. No velocity, so no arrow of its own - a world
    // built of thousands of them costs its laws, and its frames, nothing.
    // (Move it through its anchor, or by setting where it is.)
    Element& fixture(Key id, double x, double y, double z);

    // A light is an element like any other, so morphisms can move it.
    Element& light(Key id, Vec3d pos, double r = 1.0, double g = 0.93, double b = 0.82);

    // Level geometry: a solid box that blocks movement and takes plaster.
    // Walls are elements like everything else, so they can be anchored, moved
    // by a morphism, or edited from an interface.
    Element& wall(Key id, Vec3d centre, double sx, double sy, double sz, double yaw = 0.0);

    // A frame other elements hang off. Move it and the whole group moves.
    Element& anchor(Key id, Vec3d pos, double yaw = 0.0);

    // A portal: the element an embedded state is displayed on.
    Element& portal(Key id, Vec3d pos, double width, double height, double yaw = 0.0);

    // --- models ------------------------------------------------------------------
    // What its things may be drawn as beyond boxes, cylinders and spheres
    // (sg/domains/Shapes.hpp): triangles in the unit box a mesh is sized
    // from, kept by name. An element is drawn as one with `shape` = "model"
    // and `model` = its name, sized by sx, sy, sz as a box is. Made as the
    // state is built, as its fixtures are - what it looks like, not what it
    // is doing.
    void model(Key name, std::vector<float> corners);
    const std::vector<float>* model(Key name) const;

    // --- pictures -----------------------------------------------------------------
    // What its things may wear: RGBA pixels, row 0 at the top, kept by name.
    // A box with `skin` = a picture's name wears it tiled across its faces,
    // one picture every `tile` metres of the world (pixel art: sampled
    // nearest, not smoothed). A thing with `shape` = "sprite" and `picture`
    // = its name is a flat picture turned to whoever looks at it, `sx` wide
    // and `sy` tall - a Doom thing - showing cell `frame` of `frames` laid
    // side by side; clear pixels (alpha under a half) are not drawn. With
    // `face` it turns wholly to the eye, tilting as it does (a thing held
    // before the eye); without, it stands upright and only turns round.
    // Painted again, a picture keeps its name and is shown anew.
    struct Picture {
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;  // w * h * 4
        uint64_t revision = 0;
    };
    void picture(Key name, int w, int h, std::vector<unsigned char> rgba);
    const Picture* picture(Key name) const;

private:
    std::map<std::string, std::shared_ptr<const std::vector<float>>> models_;
    std::map<std::string, std::shared_ptr<Picture>> pictures_;
    uint64_t picture_revisions_ = 0;
};

// --- camera queries ----------------------------------------------------------
// Gameplay asks these ("am I close enough to use the map?"), so they live with
// the domain rather than in a renderer.
Vec3d forward_of(const Element& camera);

double distance(const Vec3d& a, const Vec3d& b);

// Where a thing is and which way it is turned: what an anchor, a doorway, a
// camera or a whole room seen from another carries. The turn is the engine's
// one rotation - yaw, then pitch, then roll (R = Ry Rz Rx, as a renderer turns
// a mesh and as spatial::from_euler builds it) - so a pose faces along R x,
// its up is R y and its across is R z. Upright (pitch and roll nothing) it is
// a heading and nothing more, and every rule below reduces to the heading's.
struct Pose {
    Vec3d position;
    double yaw = 0.0, pitch = 0.0, roll = 0.0;
};

inline bool upright(const Pose& p) { return p.pitch == 0.0 && p.roll == 0.0; }
// `v` turned as the pose turns, and back again.
Vec3d turn(const Pose& p, const Vec3d& v);
Vec3d unturn(const Pose& p, const Vec3d& v);
// The way it faces, its up, and its across (to the left of its facing).
inline Vec3d facing(const Pose& p) { return turn(p, {1.0, 0.0, 0.0}); }
inline Vec3d up_of(const Pose& p) { return turn(p, {0.0, 1.0, 0.0}); }
inline Vec3d across_of(const Pose& p) { return turn(p, {0.0, 0.0, 1.0}); }
// A point given in the pose's own frame, in the frame the pose is in - and back.
Vec3d place_in(const Pose& p, const Vec3d& local);
Vec3d local_of(const Pose& p, const Vec3d& at);
// The frame the pose is in, seen from the pose: compose_pose(p, inverse(p))
// is where you started.
Pose inverse(const Pose& p);
// The turn that takes `from` to `to`, smallest first: what a standing frame is
// carried by as the ground it stands on tilts.
Pose pose_between(const Vec3d& from, const Vec3d& to);

// --- standing: the ground a viewer's look is measured from -------------------
// A camera looks by its yaw, pitch and roll, measured from the ground it
// stands on - level ground, unless it says otherwise. `stand_w/x/y/z` is that
// ground's turn, a unit quaternion (absent: level). On the wall of a cube, on
// the far side of a planet, out of a portal in the floor, the look is the same
// look; the ground under it has turned.
Pose standing(const Element& camera);
void set_standing(Element& camera, const Pose& ground);
// Its whole turn, as the eye sees: the ground's, then the look's.
Pose eye_pose(const Element& camera);
Vec3d up_of(const Element& camera);

// --- anchors: a group of elements with a frame of its own -----------------------
// An element may carry `parent`, naming another element it is placed relative
// to. Its stored pose is then local to that anchor, and moving the anchor moves
// the whole group - a wing of a building, a vehicle and its contents, a room
// that can slide around inside a larger space. Renderers and collision both go
// through world_pose, so nothing has to know which elements are anchored.
Pose local_pose(const Element& e);

Pose compose_pose(const Pose& parent, const Pose& local);

// A box (a mesh, a wall) is placed by its base and turns about its middle:
// its frame is where its base would be were the turn taken about its middle.
// Upright, that is its position.
//
// The pose of an element in the state's own coordinates, following the parent
// chain as far as it goes. A chain that comes back on itself stops where it
// would go round again, so a cycle cannot hang the frame - and a long chain is
// followed to its end.
Pose world_pose(const State& s, const Element& e);

inline Vec3d world_position(const State& s, const Element& e) { return world_pose(s, e).position; }

// Attach an element to an anchor. Its current pose is read as local from then on.
Element& attach_to(Element& e, Key anchor);

// --- camera queries, continued ---------------------------------------------------
// These go through world_pose: "am I close enough to use that panel" has to be
// asked about where the panel actually is, not where it sits inside its group.
double distance_to(const SpatialState& s, Key element_id);

// True when the camera is near the element and pointed at it.
bool looking_at(const SpatialState& s, Key element_id, double max_dist = 3.0,
                       double min_facing = 0.8);

// A room together with where it sits, in some chosen room's coordinates. The
// pose is not a property of the room - it is the answer to "seen from where?".
//
// `doorways` are the portal elements that glue it to its neighbours. Each one
// is the boundary between two charts, and a room owns only its own side of
// it: the wall two rooms both build on the glue plane is split there, so no
// point of the glued space belongs to - or is drawn by - both.
struct PlacedRoom {
    const Spatial3D* room = nullptr;
    Pose pose;
    std::vector<Key> doorways;
    // A copy of a room across its own period (a space that wraps): drawn
    // where it is seen, lighting nothing and casting nothing, its doorways
    // only frames - the light and the ways are the room's own, once.
    bool image = false;
};

// --- a space that wraps -------------------------------------------------------
// A space may be a torus in any of its axes: `period_x`, `period_y`,
// `period_z` - go that far one way and you are back where you were, a space
// with no edge and no end. Everything that reads where things are reads it
// the same way: a walker is kept in its cell round the origin, the pulls and
// the solids round a point are the cell's and its neighbours', and a view
// draws every copy it can see. One fact, and every part of the engine that
// meets it agrees.
Vec3d period_of(const State& s);
// Where the copies of the space are, as offsets, within `reach` of `at`
// (its own first: no offset).
std::vector<Vec3d> images(const State& s, const Vec3d& at, double reach);
// `p` put back in the cell round the origin.
Vec3d wrapped(const State& s, const Vec3d& p);

// A half-space: the points where `at(p) >= 0`.
struct HalfSpace {
    Vec3d normal;
    double offset = 0.0;
    double at(const Vec3d& p) const;
};

// The side of `portal` its room is on, in the frame `placed` puts the room in.
// A portal faces into its own room (walk in against its facing, out along the
// other's), so the room is the half-space ahead of it. Derived from the portal
// every time it is asked for, so it moves with the doorway and cannot drift.
HalfSpace room_side(const State& room, const Element& portal, const Pose& placed);

// --- walls -----------------------------------------------------------------------
// Push `mover` out of any wall element it has ended up inside. Walls are boxes
// with a pose, so anchored ones are handled without knowing they are anchored.
//
// A wall stands on its base, so anything whose base is at or above head height
// is walked under, not into: that is what makes a lintel over a doorway a
// doorway rather than a blocked wall.
void resolve_wall_collisions(const State& s, Element& mover, double radius,
                                    double head = 1.9, const Pose& frame = Pose{});

// --- portals between spaces ---------------------------------------------------
// Two doorways, one in each room, joined back to back. Because each room is its
// own state with its own coordinates, everything about "the same place in the
// other room" is this one transform: rotate by the difference between the two
// doorway yaws (plus half a turn, since they face each other), then translate
// into the far doorway's frame.
// The turn that takes you from one doorway to the other. A viewer walks into
// `here` against its facing (-n_here) and comes out of `there` along its facing
// (+n_there); with facing n = (sin yaw, cos yaw), that works out to:
// A doorway faces the way its yaw points, exactly like a camera does. You walk
// into `here` against its facing and come out of `there` along it, so the turn
// between the two frames is:
inline double portal_delta(double here_yaw, double there_yaw) {
    return there_yaw - here_yaw + 3.14159265358979;
}

double portal_delta(const Element& here, const Element& there);

// One rotation, applied to the position and the turn alike - getting those
// two out of step is what makes a portal look subtly wrong. A doorway is the
// same doorway seen from either side: carried across, a pose keeps where it
// is relative to `here` - turned half round about the doorway's up, since it
// leaves `there` along its facing - relative to `there`. Upright doorways
// carry height as height above the doorway, so a door in a floor at one level
// opens onto ground at another; a doorway in a floor, a wall or a ceiling
// carries the whole turn.
Pose through_portal(const Pose& here, const Pose& there, const Pose& p);
Pose through_portal(const Pose& here, const Pose& there, const Vec3d& pos, double yaw);

inline Pose through_portal(const Element& here, const Element& there, const Vec3d& pos,
                           double yaw) {
    return through_portal(local_pose(here), local_pose(there), pos, yaw);
}

// Carry a camera across: its ground turned with it - unless the turn is about
// the up of level ground, which is a heading and nothing more.
void carry_camera(const Pose& here, const Pose& there, const Element& src, Element& dst);

// The same transform as a transport, ready to hang on a functor: it carries a
// camera (or anything with a pose) from one room's frame into the other's.
// Used twice in a portal - once by the View embedding that aims the window's
// virtual camera, once by the transition taken when you walk through.
std::function<void(const Element&, Element&)> portal_carry(const Element& here,
                                                                  const Element& there);

// The doorway itself, carried to the other side: where the near doorway is,
// the far one is - facing back, since a doorway faces into its own room and
// the same doorway seen from the other room faces the other way. Its size goes
// with it. This is the glue of a seam, not travel: carrying the near doorway
// lands exactly on the far one, and the seam law checks that it does.
std::function<void(const Element&, Element&)> seam_carry(const Element& here, const Element& there);

// Anything else that hangs in a seam - a door on its hinge - carried across
// as it is: its place and its heading, in the other side's frame.
std::function<void(const Element&, Element&)> pose_carry(const Element& here, const Element& there);

// Did the step from `from` to `to` pass through the doorway's opening, front to
// back? Used to fire the transition that actually changes state. A doorway
// that is a ball (`ball` = its radius) is crossed going into it - or, if it
// says `ball_out`, going out of it: the boundary of a world inside another
// (a planet in its system), seen from outside and from within.
bool crossed_portal(const Element& portal, const Vec3d& from, const Vec3d& to);

// Is the doorway a way through from `at`? A doorway that is walked (`walk`)
// is crossed only front to back, and a `oneway` one is only that way: from
// behind, either is only its frame - what is seen through it is what one
// walks into. Any other portal, and a ball, from anywhere.
bool opens_from(const Element& portal, const Vec3d& at);

// How far before its own plane a portal's picture is drawn (`inset`): a
// doorway walked through is drawn where it is crossed (0), so nothing lies
// between the picture and the crossing; a window, a little proud (0.06).
double portal_inset(const Element& portal);

// A doorway made `w` wide and `h` high, its foot where it stood: a door
// stands on its ground whatever its size, so two sides glued (which must be
// one size) still stand on theirs.
void size_opening(Element& door, double w, double h);

// A direction as a doorway's overlap gives it (State::overlap): out through
// the doorway, its up, and across - the frame both sides of a seam agree in.
Vec3d out_through(const Pose& door, const Vec3d& v);

// An overlap's account (`out`, from State::overlap) of a doorway `door`, `h`
// high, standing on level ground whose height at (x, z) is `ground`: what it
// does not already say of its pull and its floor. A space whose own walker
// keeps level ground (a room, a desert, a corridor) gives its account so.
void on_level_ground(Params& out, const Pose& door, double h, double (*ground)(const void*, double, double), const void* of);

}  // namespace sg
