// Stategine - a ragdoll: a being's body given weight, as a state.
//
// A being (Being.hpp) means a pose - its clips, holds and reaches say where
// every joint is to be (`aq`, `ax ay az`). Its ragdoll is that body made
// physical: a rigid body for each bone, sized and weighed from the skeleton
// as it is bound - as big as the parts the being shows on it, if it shows
// any; else a bone to its child is a rounded rod, a bone with several
// children a block round them, a foot a block along the ground, a head a
// ball, and any other end a short rod on from its parent; a bone too small
// to matter (a finger's joints, a toe's or a crown's end) has none, and rides
// its parent as the being moves it (`rides`) - so any skeleton, of any
// number of limbs and fingers, is a ragdoll - held to its parent at a ball that bends no further than a cone,
// and turned there by a muscle toward the turn the being means (a soft
// spring, `kp = I w^2`, `kd = 2 z I w` for the inertia it moves). The
// limbs of one body pass through each other; the room's solids stop them.
//
// What is done to it moves it: a hit (`<id>.hit`, an impulse on a bone)
// moves the whole body by what it carries and kicks the limb hit, and what
// hangs from it and what it hangs from give, each as heavy as it is; its feet
// brace against a push up to what they hold (`hold`, newtons), so leaning on
// it or pulling at it does not drag it; the bone and its neighbours are weakened a moment and
// come back. A hand may hold a bone and pull (`<id>.grab`, `<id>.let_go`):
// the arm is drawn out against its muscles and, let go, swings on past where
// it was meant to be and settles back. A hit hard enough (`fall_at`) takes
// all its strength, which comes back as it recovers (`recover`).
//
// Its root (the hips) goes on as the being means it, swayed by its balance;
// all the rest has weight, legs too - kicked, a leg swings and comes back.
// Standing on two feet it balances: its hips sway over them as an inverted
// pendulum does, pushed by what moves above them and held by the soles as
// far as they reach; when where it would come to rest (the capture point)
// is off its soles, a foot steps there (the leg bent to it), as many times
// as it takes; steady, its feet step home. Too many steps, too long
// stumbling or too far leaning and it gives up: it falls, its strength
// ebbing; lain still a while, it gets up. `full` = 1 lets it fall at once. While nothing disturbs it
// it sleeps - its bones simply where the being means them, costing nothing -
// and wakes when it is hit, held, knocked, or meets a solid. A knock is
// whatever another world - the room's loose things, someone walking into it -
// says struck a bone: an impulse in the being's frame (`kx ky kz`) and how
// many there have been (`knock_n`), carried onto the bone by a functor; each
// new one is taken once. Awake, it leads the being
// (`lead`), so what is seen is the body as it really moves; settled, it
// gives the being back to itself.
//
// Time comes from its drive (`<id>.step`, {dt}), stepped at 120 Hz. Its
// solver (sg::rigid) is made from its params each step and kept nowhere: a
// step is a function of them alone.
#pragma once

#include <cstdint>
#include <vector>

#include "sg/core/State.hpp"
#include "sg/physics/Rigid.hpp"

namespace sg {

class Being;
class StateGraph;
class Temporal;

class Ragdoll : public State {
public:
    // Made from `body` as it is bound, `mass` kg in all.
    Ragdoll(Key id, const Being& body, double mass = 70.0);
    Key kind() const override { return Key{"ragdoll"}; }

    static Key self_id() { return Key{"self"}; }
    Key step_event() const { return Key{id().str() + ".step"}; }      // driven: {dt}
    Key hit_event() const { return Key{id().str() + ".hit"}; }        // {bone, x, y, z (N s, the being's frame), weaken=0.25, for=0.8}
    Key grab_event() const { return Key{id().str() + ".grab"}; }      // {bone, x, y, z (where to, the being's frame), [ax ay az: where on it], force=500}
    Key let_go_event() const { return Key{id().str() + ".let_go"}; }  // {}

    std::vector<Key> bones() const;
    // A bone's solid, in its own frame (its joint at the origin), as its
    // params say: for whatever else gives the bone a body (another world's).
    static rigid::Hull hull(const Element& bone);
    // A bone as a solid of another rigid world - the room's, where loose
    // things and walkers meet the body - from an element that carries the
    // bone's params (the bone, or a copy of it): its hull and weight, of
    // `group` (one body's bones pass through each other).
    static rigid::Body body(const Element& bone, const std::string& id, int group);
    // Where such a bone is in a world in which the being stands at `origin`,
    // turned `yaw`: its joint, and its turn.
    static void pose_in(const Element& bone, const rigid::V3& origin, double yaw, rigid::V3& x, rigid::M3& r);
    // The bone nearest a point of the being's frame: what a hand that touches
    // the body there touches.
    Key nearest(const rigid::V3& p) const;
    // One step of `dt` seconds.
    void step(double dt);

private:
    void build(rigid::World& w) const;
    bool disturbed() const;
    // Its balance, a step of `dt`: the hips' sway over the feet, a foot
    // stepped to catch it or home, its legs aimed there; falling, and getting
    // up again.
    void balance(double dt);
};

// A ragdoll for `body` (`<body>.rag`), on `clock`: the being's meaning
// carried to it and its bones carried back as what leads the being, both by
// kept functors (`<body>.means`, `<body>.rag.leads`). Returns its id.
Key ragdoll(StateGraph& g, Being& body, Temporal& clock, double mass = 70.0);

// What the ragdoll meets: every wall and solid thing of `host` that stands
// on nothing (carried as it is, by a kept functor `<rag>.solids`), and where
// the being stands in it (`anchor`, an element of `host`), so they can be
// seen from the being's own frame. Returns the functor's name.
Key collide_with(StateGraph& g, Ragdoll& rag, Key host, Key anchor);

}  // namespace sg
