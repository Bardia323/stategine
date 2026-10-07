// Stategine - a ragdoll: a being's body given weight, as a state.
//
// A being (Being.hpp) means a pose - its clips, holds and reaches say where
// every joint is to be (`aq`, `ax ay az`). Its ragdoll is that body made
// physical: a rigid body for each bone, sized and weighed from the skeleton
// as it is bound (a bone to its child is a rounded rod, a bone with several
// children a block round them, a bone with none a short rod on from its
// parent), held to its parent at a ball that bends no further than a cone,
// and turned there by a muscle toward the turn the being means (a soft
// spring, `kp = I w^2`, `kd = 2 z I w` for the inertia it moves). The
// limbs of one body pass through each other; the room's solids stop them.
//
// What is done to it moves it: a hit (`<id>.hit`, an impulse on a bone)
// throws the limb, and what hangs from it and what it hangs from give, each
// as heavy as it is; the bone and its neighbours are weakened a moment and
// come back. A hand may hold a bone and pull (`<id>.grab`, `<id>.let_go`):
// the arm is drawn out against its muscles and, let go, swings on past where
// it was meant to be and settles back. A hit hard enough (`fall_at`) takes
// all its strength, which comes back as it recovers (`recover`).
//
// Its legs and hips go on as the being means them (they keep it standing;
// `full` = 1 gives them weight too, and it falls). While nothing disturbs it
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
// solver (sg::rigid) is a cache, made from its params each step, started
// again from the same contacts when the laws try a step twice.
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
    Key grab_event() const { return Key{id().str() + ".grab"}; }      // {bone, x, y, z (where to, the being's frame), force=500}
    Key let_go_event() const { return Key{id().str() + ".let_go"}; }  // {}

    std::vector<Key> bones() const;
    // A bone's solid, in its own frame (its joint at the origin), as its
    // params say: for whatever else gives the bone a body (another world's).
    static rigid::Hull hull(const Element& bone);
    // One step of `dt` seconds.
    void step(double dt);

private:
    void build(rigid::World& w) const;
    bool disturbed() const;
    // What the solver's contacts were after the last step, and what this
    // step started from (by everything it started from): the same step tried
    // twice starts from the same contacts.
    rigid::World::Contacts last_, memo_;
    uint64_t memo_in_ = 0;
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
