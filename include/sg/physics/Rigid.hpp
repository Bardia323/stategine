// Rigid bodies: things that fall, tumble, stack, slide, tip over and are
// picked up - a mug, a book, a chair, a table turned on its back.
//
// A body is one or more convex hulls (boxes, and cylinders as prisms of many
// sides) fixed together, with its mass spread through their volume. Things
// that do not move - the floor, the walls, a bookcase - are bodies too, of no
// mass. Each step:
//
//   collide   every pair that can touch, by their boxes - found by sweeping
//             along x (sweep and prune: only boxes whose spans along x meet
//             are compared, and a thing at rest is never compared with
//             another at rest); then, hull against
//             hull, the axis they are least deep along (faces of either, or
//             an edge of each: the separating axis test), and where they
//             touch - the face of one clipped by the other's, up to four
//             points (a manifold). Kept from step to step, so what was
//             pushed last time is pushed again at once (warm starting).
//   solve     in substeps: field responses; impulses at every point of contact, over
//             and over, each one's total kept within its limits - never
//             pulling, friction within mu of the push - with soft springs
//             to undo what has sunk in; the bodies moved; the same impulses
//             again without the springs, so undoing a sink adds no energy
//             (a soft step, as Box2D v3 takes it); bounces last.
//   sweep     something that went far this step for its size - a stone
//             thrown, a pebble flicked spinning - is swept along its way,
//             turning as it went, against what does not move, and stopped
//             where it first met it: never let through a thin wall
//             (conservative advancement, by the gap between the hulls).
//   sleep     bodies touching each other are an island; an island that has
//             lain still a moment sleeps, costs nothing, and wakes when
//             something moving touches it.
//
// Joints hold two bodies together (or one to the room): at a point (a
// ball), and turning only about an axis (a hinge - a door, a wheel), with
// limits to how far, a motor to drive it and a spring to draw it back; or
// two points drawn to a length by a spring. They are solved with the
// contacts, softly as they are, and join islands: what is joined sleeps and
// wakes together.
//
// A body can be driven (World::drive): moved by the game to where it should
// be by the end of the next step - furniture hauled, a lift, a board pulled
// by its stand - at the speed that takes. For that step it is as heavy as
// the room: what it meets is pushed out of its way, what lies on it goes
// with it by friction, and what sleeps against it wakes.
//
// A sensor is a body nothing bumps into: things pass through it, and the
// world keeps what is inside each one (World::inside, entered, left) - a
// doorway that notices who walks through, a pressure plate. A hull can be
// cast along a line (World::cast): what it would meet first, and how far.
//
// A walker (World::walk) is someone on their feet among it all: an upright
// body that is not a rigid one - it slides along what it walks into, steps
// up a stair and down again, stands on what is gentle enough and not on
// what is too steep, falls off an edge, rides what it stands on (a moving
// platform, a lift, a boat) and shoves what is light enough aside.
//
// A hand holds a body by a soft spring at a point of it, as strong as the
// arm: a mug comes up whole and is held square to the eye; a table taken by
// its edge lifts that edge and turns over on the other one.
//
// Coordinates are the world's: y up, metres, seconds, kilograms. Turns are
// the renderer's (from_euler): yaw about y, then pitch, then roll.
#pragma once
#include "sg/spatial/Math.hpp"
#include "sg/physics/Field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace sg::rigid {

// Compatibility names; the implementation belongs to shared spatial math.
using spatial::V3;
using spatial::M3;
using spatial::dot;
using spatial::cross;
using spatial::length;
using spatial::normalize;
using spatial::transpose;
using spatial::zero3;
using spatial::outer;
using spatial::skew;
using spatial::inverse;
using spatial::axis_angle;
using spatial::orthonormal;
using spatial::log_map;
using spatial::from_euler;
using spatial::to_euler;
using spatial::operator*;
using spatial::operator+;
using spatial::operator-;

// --- hulls -------------------------------------------------------------------------
// A convex solid: its corners, its faces (each a plane and its corners in
// order round it, seen from outside), its edges (each between two faces).
struct Hull {
    struct Face {
        V3 n;                 // outward
        std::vector<int> vi;  // counter-clockwise, seen from outside
    };
    struct Edge {
        int a, b, fa, fb;
    };
    std::vector<V3> v;
    std::vector<Face> f;
    std::vector<Edge> e;
    V3 centre;      // of its volume
    double volume = 0;
    double friction = -1;  // its own, where it differs from its body's (castors roll; a tray grips)
    M3 cov;         // second moment about its centre, per unit density

    // A solid of `sides` corners round (`sides` 4 is a box), `half` wide
    // across x and z and high in y, its base at `centre.y - half.y`; the top
    // `taper` times as wide as the bottom. Turned by `turn`, then moved to
    // `centre`.
    static Hull prism(V3 centre, V3 half, int sides, const M3& turn = M3{}, double taper = 1.0);
    static Hull box(V3 centre, V3 half, const M3& turn = M3{}) { return prism(centre, half, 4, turn); }

    // Normals, orders, edges and mass, from the corners and faces.
    void finish();
};

// --- bodies ------------------------------------------------------------------------
struct Body {
    std::string id;
    // Independent capabilities. Sources are in this body's local frame.
    std::vector<field::Source> fields;
    std::vector<field::Receiver> receives{{"gravity", field::Response::Acceleration, 1}};
    std::vector<Hull> hulls;  // in the body's own frame
    V3 x;                     // where its frame is
    M3 r;                     // how its frame is turned
    V3 v, w;                  // its centre's velocity; its spin, in the room
    double mass = 0;          // 0: it does not move
    double friction = 0.55, restitution = 0.2;
    bool awake = true;
    double idle = 0;          // how long it has been still
    double hit = 0;           // the hardest knock since it was last asked
    bool grabbed = false;
    double radius = -1;       // how far any of it is from its frame; worked out when first asked
    bool sensor = false;      // nothing bumps into it: what is inside it is only noted
    bool driven = false;      // this step, moved to `drive_x`, `drive_r` by the game
    V3 drive_x;
    M3 drive_r;
    int contacts = 0;         // how many things it touches, this step

    // Derived: where its mass is, and how it resists turning.
    V3 com_local;
    M3 inv_inertia_local = zero3();
    double inv_mass = 0;

    // Where everything is now, in the room.
    struct Placed {
        std::vector<V3> v, n;
        std::vector<double> d;
        V3 centre, lo, hi;
    };
    std::vector<Placed> world;
    V3 lo, hi;

    bool dynamic() const { return inv_mass > 0; }
    V3 com() const { return x + r * com_local; }
    M3 inv_inertia() const { return r * inv_inertia_local * transpose(r); }

    // Mass `m` spread evenly through the hulls; 0 makes it fixed.
    void set_mass(double m);

    void place();
};

// --- where two hulls touch -------------------------------------------------------------
struct Touch {
    V3 p;         // the point, halfway between the surfaces
    double sep;   // how far apart there (below 0: how deep)
    uint32_t id;  // which corners and sides made it: the same point next step has the same id
};

namespace detail {

bool minkowski_face(V3 a, V3 b, V3 c, V3 d);

// The closest points of two segments.
void closest(V3 p1, V3 q1, V3 p2, V3 q2, V3& c1, V3& c2);

}  // namespace detail

// Where hull `ha` (placed `pa`) touches hull `hb` (placed `pb`), or would
// within `margin`: the normal from a to b, and up to four points. None if
// they are farther apart than that.
int touch(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb, double margin, V3& normal,
                 std::array<Touch, 4>& out);

// How far apart two hulls are at least: the widest gap along any axis that
// could part them - a face of either, or across an edge of each. Never more
// than they truly are apart; below 0, they may touch.
double apart(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb);

// Where a ray first meets a hull, or -1; and the face it meets there.
// Which of a thing's parts make its shape. A thing made of many parts (a
// chair: seat, back, legs, five castors, a knob) is too many hulls to collide
// cheaply, and the small ones mostly do not matter - but the outermost always
// do: they are what meets the floor, a wall, another thing, however it lies.
// So: every part of any size (at least `small` of the biggest's volume), and
// every smaller one that reaches the outside of the whole, on any side, within
// `margin` - every castor and foot, the top rail of a chair's back - the
// lowest first, up to `most` in all. Each part as its box in the thing's own
// frame (`lo`, `hi`) and its volume; the answer is their indices.
struct PartBox {
    V3 lo, hi;
    double volume = 0;
};
std::vector<std::size_t> outline(const std::vector<PartBox>& parts, std::size_t most = 28, double margin = 0.01,
                                        double small = 0.01);

double ray_hull(const Body::Placed& p, V3 o, V3 d, double reach, V3* normal = nullptr);

// How high the surface of `b` is at (x, z), no higher than `below`; -1 if it
// is not there. A face steeper than a sheet stays put on is nothing to lie on.
double surface_at(const Body& b, double x, double z, double below);

// Something light lying on a body that moves goes with it - friction, as
// cheaply as it can be had: stuck to it while the surface tilts less than it
// would slide at and speeds up or slows down less than its grip holds, let
// go when it does more. `ride` works out the one step: the body's pose and
// velocity before (`x0`, `r0`, `v0`) and now, a thing at `p` turned `face`
// (its up along `up` in its own frame). True if it stays on, moved.
bool ride(const Body& b, V3 x0, const M3& r0, V3 v0, double dt, V3& p, M3& face, V3 up_local, V3* let_go_v = nullptr);

// --- joints ---------------------------------------------------------------------------
// Two bodies held together at a point, `b` empty for the room itself. Made
// by World::ball, hinge and spring, from where things are now.
struct Joint {
    enum Kind { Ball, Hinge, Spring };
    Kind kind = Ball;
    std::string a, b;   // b empty: the room
    V3 la, lb;          // the point, in each one's frame (the room's, for the room)
    V3 axis_a, axis_b;  // a hinge's axis, in each frame
    V3 ref_a, ref_b;    // across the axis, in each frame: its angle is between them
    // A hinge turns only between `lower` and `upper` (radians) if `limit`;
    // a motor drives it at `speed` with at most `torque`; a spring draws it
    // to `target`, `hertz` stiff, `damping` damped (1: just no overshoot).
    bool limit = false;
    double lower = 0, upper = 0;
    bool motor = false;
    double speed = 0, torque = 0;
    bool spring = false;
    double target = 0, hertz = 2, damping = 1;
    double rest = 0;    // a Spring joint's length (hertz and damping as above)
    // What it pushed with last substep, to start the next from (per substep).
    V3 point;
    double tilt1 = 0, tilt2 = 0, drive = 0, pull = 0, low = 0, high = 0;
    int moving = -1;
};

// --- walkers --------------------------------------------------------------------------
// Someone on their feet: where they stand, how big they are, how high a
// stair they take in their stride and how steep a slope they stand on.
struct Walker {
    V3 at;                 // where their feet are
    double radius = 0.3, height = 1.8;
    double step = 0.3;     // the highest stair taken in a stride
    double slope = 0.8;    // the steepest ground stood on, radians from level
    double mass = 70;      // what they shove with
    double vy = 0;         // falling
    bool grounded = false;
    V3 ground{0, 1, 0};    // which way the ground under them faces
    std::string on;        // what they stand on
    V3 on_x;               // and where that was, and how turned, last step
    M3 on_r;
    double turned = 0;     // how far what they stand on turned them this step (about y)
};

// A direction across `n`, and another across both.
void across_of(V3 n, V3& p1, V3& p2);

// --- the world ------------------------------------------------------------------------
class World {
public:
    // State-derived field data; ordinary gravity is just one analytic source.
    std::vector<field::Source> fields{field::Source::directional("gravity", {0, -9.81, 0})};
    int substeps = 4;
    int iterations = 2;        // per substep, with springs; and as many again without
    double margin = 0.012;     // how far apart two things may be and still count as touching
    double slop = 0.0005;       // how deep a thing may rest in another without being pushed out
    double contact_hertz = 30; // how stiff the springs that push things apart are
    double max_push = 2.0;     // and how fast they may push, m/s
    double sleep_after = 0.5;  // s still before an island sleeps
    // Spatial broadphase: sweep along x for small worlds, BVH for 64 or more
    // bodies. False: exhaustive pairs, to verify identical behavior.
    bool sweep = true;

    std::vector<Body> bodies;

    // --- bodies ----------------------------------------------------------------------
    Body& add(Body b);
    Body* find(const std::string& id);
    const Body* find(const std::string& id) const;
    void remove(const std::string& id);
    void clear();

    // Put a body somewhere, as if it had always been there (not moved
    // through what is between), still.
    void teleport(Body& b, V3 x, const M3& r);
    // A fixed thing has moved: whatever sleeps on it wakes.
    void moved(Body& b, V3 x, const M3& r);
    void wake(Body& b);
    void wake_box(V3 lo, V3 hi);
    void wake_near(const Body& b) { wake_box(b.lo, b.hi); }
    bool any_awake() const;

    // --- joints ----------------------------------------------------------------------
    std::vector<Joint> joints;

    // `a` and `b` (or the room, `b` empty) held together at `at`, each free
    // to turn about it.
    Joint& ball(const std::string& a, const std::string& b, V3 at) { return join(Joint::Ball, a, b, at, {0, 1, 0}); }
    // Held at `at`, and turning only about `axis` - as they stand now is angle 0.
    Joint& hinge(const std::string& a, const std::string& b, V3 at, V3 axis) { return join(Joint::Hinge, a, b, at, normalize(axis)); }
    // The point `pa` of `a` and `pb` of `b` (or of the room) drawn to the
    // length they are apart now, by a spring `hertz` stiff.
    Joint& spring(const std::string& a, const std::string& b, V3 pa, V3 pb, double hertz, double damping = 1.0);
    // How far a hinge has turned from where it was made: `a` against `b`
    // (a door against its frame), radians about the axis.
    double angle(const Joint& j) const;
    // --- driving ---------------------------------------------------------------------
    // `b` moved to `x`, turned `r`, by the end of the next step, at the speed
    // that takes; what sleeps where it goes wakes. (Called each frame it is
    // moved: once stepped, it is itself again - fixed, or free.)
    void drive(Body& b, V3 x, const M3& r);

    // --- sensors ---------------------------------------------------------------------
    // What is inside a sensor now, as (sensor, thing) - and what came in and
    // went out this step.
    using Inside = std::pair<std::string, std::string>;
    const std::set<Inside>& inside() const { return inside_; }
    const std::vector<Inside>& entered() const { return entered_; }
    const std::vector<Inside>& left() const { return left_; }

    // --- casts -----------------------------------------------------------------------
    // The hull `shape`, turned `turn`, carried from `from` to `to` without
    // turning: the first thing it would meet (not `skip`, not a sensor), how
    // far along the way (0..1, in `at`) and which way that thing faces there
    // (towards the shape, in `normal`); none if it meets nothing.
    const Body* cast(const Hull& shape, const M3& turn, V3 from, V3 to, double* at = nullptr, V3* normal = nullptr,
                     const std::string& skip = {}) const;

    // --- walkers ---------------------------------------------------------------------
    // One step of a walker wanting to go `move` (across the ground; its
    // height is the ground's): carried by what they stand on, then across -
    // sliding along what they meet, the bottom `step` of them passing over
    // what is lower (a stair) - then down onto the ground, if it is within
    // a stride below and not too steep, or falling.
    void walk(Walker& w, V3 move, double dt, double time = 0);

    // Whatever holds `id` to anything, let go.
    void unjoin(const std::string& id);

    // --- the hand ----------------------------------------------------------------------
    // Hold `id` by the point `local` of it (in its own frame), pulling that
    // point to `target` with at most `force` newtons. With `upright`, the
    // body is turned to `turn` as well, with at most `torque`.
    struct Grab {
        std::string id;
        V3 local, target;
        double force = 400;
        bool turns = false;
        M3 turn;
        double torque = 30;
        double hertz = 6, damping = 1.0;
        // Solver scratch.
        V3 impulse, spin;
    };
    Grab& grab(const std::string& id, V3 local, V3 target, double force);
    Grab* grabbing(const std::string& id);
    void release(const std::string& id);

    // What a ray meets first, and where along it.
    const Body* ray(V3 o, V3 d, double reach, double* at = nullptr, bool dynamic_only = true, const std::string& skip = {},
                    V3* normal = nullptr) const;

    // Putting something down: where to hold the middle of `id`'s mass, turned
    // `turn`, so that it hangs just clear above the top the eye is on - the
    // floor, a table, a shelf - or false if the eye is on no top within
    // `reach` (a wall, the air). Let go there, it drops the last centimetre
    // and stands - or does not, as it is shaped.
    bool hover(const std::string& id, V3 eye, V3 dir, double reach, const M3& turn, V3& com_at, double clear = 0.012) const;
    // How far from its middle a body reaches: to hold it clear of the eye.
    static double reach_of(const Body& b);

    // --- someone walking -----------------------------------------------------------------
    // A walker is an upright circle at (x, z), `radius` round, from `y0` to
    // `y1`: out of every moving body in the way that stands higher than a
    // step. What they walk into is shoved - at the height of their hips, so
    // a floor lamp walked into tips over - as hard as they walk, the lighter
    // it is the more; the walker is kept out of it all the same.
    V3 walk_into(V3 p, double radius, double y0, double y1, V3 moved, double dt, std::vector<std::string>* shoved = nullptr);

    // --- a step ------------------------------------------------------------------------
    // Time at the beginning of this step, supplied by the state's drive.
    void step(double dt, double time = 0);

private:
    struct Point {
        V3 p, ra, rb;        // at collision time; arms from the centres of mass
        V3 la, lb;           // the same point in each body's frame, to find it again
        double sep = 0;      // at collision time
        double pn = 0, pt1 = 0, pt2 = 0;
        double mn = 0, mt1 = 0, mt2 = 0;
        double vn0 = 0, most = 0;
        uint32_t id = 0;
    };
    struct Manifold {
        std::size_t a = 0, b = 0;
        int ha = 0, hb = 0;
        V3 n, t1, t2;
        std::vector<Point> pts;
        double friction = 0.5, restitution = 0.2;
        bool live = false;
        int moving = -1;  // which of the two moved when its impulses were found (1: a, 2: b)
    };

    // Per body scratch for a step (kept apart from Body's public face).
    std::unordered_map<std::string, std::size_t> index_;
    std::unordered_map<uint64_t, Manifold> manifolds_;

public:
    // What each contact was pushed with last step: to start a step again
    // from where it started once before.
    using Contacts = std::unordered_map<uint64_t, Manifold>;
    Contacts contacts() const { return manifolds_; }
    // The pairs of hulls touching now, by their keys, in order.
    std::vector<uint64_t> touching() const;
    void set_contacts(Contacts c) { manifolds_ = std::move(c); }

private:
    std::vector<Manifold*> live_;
    std::vector<Grab> grabs_;

    static uint64_t pair_key(std::size_t a, std::size_t b, int ha, int hb);

    void collide();

    Joint& join(Joint::Kind kind, const std::string& a, const std::string& b, V3 at, V3 axis);

    // The joints, held: at their points, about their axes, within their
    // limits, driven and sprung - a soft step as the contacts take, stiffer
    // (`joint_hertz`); `springs` false, only what moves now is held (the
    // relaxing pass), as for the contacts. What sleeps does not move.
    double joint_hertz = 60.0;
    struct Held {
        Body* A;
        Body* B;
        V3 ra, rb, pa, pb;
        double ma, mb;
        M3 ia, ib;
    };
    bool held(Joint& j, Held& h);
    void push(Held& h, V3 j);
    void twist(Held& h, V3 t);
    V3 spin_between(const Held& h) const { return (h.mb > 0 ? h.B->w : V3{}) - (h.ma > 0 ? h.A->w : V3{}); }
    V3 speed_between(const Held& h) const;
    static double soft(double hz, double zeta, double h, double& ms, double& is);

    void warm_joints();

    void solve_joints(double hstep, bool springs);

    std::set<Inside> inside_;
    std::vector<Inside> entered_, left_;

    // --- through nothing ---------------------------------------------------------------
    // Where each body was when the step began.
    std::vector<V3> from_x_;
    std::vector<M3> from_r_;
    // Each moving body that went far this step for its size - further than
    // half its narrowest - swept from where it was to where it is, turning
    // as it went, against what does not move near its way; stopped where it
    // first comes within touching of one (conservative advancement: each
    // stride no longer than the gap left, at the fastest any of it moves).
    // What it was already touching at the start does not stop it.
    void sweep_fast();

    // Looked at this step: what moves by itself, and what is driven.
    static bool moving(const Body& b) { return (b.dynamic() && b.awake) || b.driven; }
    // How far past its box a moving body is looked for: the margin, and as
    // far as it can go this step, so nothing is passed through - a gap is
    // closed no faster than it can be (a contact that may not yet be
    // touching), however long the step.
    double stepped_ = 1.0 / 60.0;
    double looked_for(const Body& b) const { return margin + length(b.v) * stepped_; }

    // A moving body `i` and any other `j`: if `i`'s box, grown by its reach,
    // meets `j`'s, they are a pair. (Of two moving bodies, the one met first
    // by number is the one grown.)
    void consider(std::size_t i, std::size_t j);

    // Every moving body against every other.
    void every_pair();

    // Sweep and prune: the bodies in order of where their spans along x
    // begin (a moving one's grown by its reach), kept from step to step and
    // put back in order by insertion - little moves in a step, so that is
    // nearly a pass over them. Going along, those whose spans have not
    // ended yet are open; a body meets only the open ones, and a body at
    // rest only the open ones that move.
    void sweep_pairs();
    void indexed_pairs();
    spatial::Index broadphase_;
    std::vector<std::size_t> order_, open_moving_, open_still_;  // the sweep's scratch
    std::vector<double> span_lo_, span_hi_;

    void pair(std::size_t ia, std::size_t ib, double reach);

    // Anchors, masses, and the velocities things meet with.
    void prepare(double h);

    // What gives when pushed: free, awake, and not being driven.
    static bool moves(const Body& b) { return b.dynamic() && b.awake && !b.driven; }

    void integrate_velocities(double h, double time);
    void sample_fields(double time);
    field::Solver field_solver_;
    std::vector<field::Result> responses_;

    void apply(Body& a, Body& b, V3 ra, V3 rb, V3 j);

    void warm_start();

    // How far apart a point is now, the bodies having moved since it was found.
    double separation(const Manifold& m, const Point& p) const;

    void solve(double h, bool springs);

    void relax(double h) { solve(h, false); }

    void solve_grabs(double h);

    void integrate_positions(double h);

    // Bounces, from how fast each point met, once the step is done.
    void restitution();

    // Islands: everything moving that touches, through anything else that
    // moves. An island still for long enough sleeps whole.
    void sleep(double dt);
};

}  // namespace sg::rigid
