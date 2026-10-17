// Stategine - a door: a state of its own, because it hangs in a doorway
// between two rooms and is in neither.
//
// Its leaf turns about its hinge: `angle` 0 shut, swinging only one way, to
// `lo` or `hi` (one of them 0) - into the room its leaf hangs in. Each side
// holds what it needs of it, carried by kept functors: the leaf's room its
// leaf (the hinge turned: `door_hinge`), both sides their doorway (walked
// through only while it stands open: `door_near` on the leaf's side, which
// also says when it is shut close and nothing is seen past it; `door_far`
// on the other).
//
// Objects:  leaf  angle, vel, lo, hi, shut (the hinge's turn shut), held,
//                 goal, target, friction, rattle
//           way   open (may be walked through), shut (shut close)
// Arrows:   leaf --<id>.step-->    leaf  {dt, time}, its own line: swings on,
//                                        slowing; held, it follows the hand;
//                                        through shut with little speed, it
//                                        latches
//           leaf --<id>.hold-->    leaf  {angle}: follows a hand, springily
//           leaf --<id>.release--> leaf  lets go
//           leaf --<id>.push-->    leaf  {speed}: a shove, positive towards open
//           leaf --<id>.swing-->   leaf  {open} 0..1: swung there, let go there
//           leaf --<id>.rattle-->  leaf  tried, and it does not give: it shakes
//                                        in its frame
//
// Whoever is at it says so in their room (`<room>.<doorway>.hold` ...), and a
// functor carries it here. It knows no room.
//
//   drive time -> hall_door event hall_door.step keeps always
//   functor hall_door.hung : hall_door -> hall {
//       object leaf -> hall.leaf.hinge native door_hinge
//       object way -> hall.to_kitchen native door_near
//   }
//   keep hall_door.hung
//
// (The transports are registered by whoever builds the graph:
// `natives.transport("door_hinge", sg::door_hinge)`.)
#pragma once

#include "sg/core/State.hpp"

namespace sg {

class Door : public State {
public:
    // `open`: how far it opens and which way (radians, signed); `shut`: the
    // hinge's turn, in its room, when the leaf is shut; `ajar`: where it
    // stands at the start (0..1 of the way open).
    Door(Key id, double open, double shut, double ajar = 0.0);

    Key kind() const override { return Key{"door"}; }

    static Key leaf_id() { return Key{"leaf"}; }
    static Key way_id() { return Key{"way"}; }
    Key step_key() const { return Key{id().str() + ".step"}; }
    Key hold_key() const { return Key{id().str() + ".hold"}; }
    Key release_key() const { return Key{id().str() + ".release"}; }
    Key push_key() const { return Key{id().str() + ".push"}; }
    Key swing_key() const { return Key{id().str() + ".swing"}; }
    Key rattle_key() const { return Key{id().str() + ".rattle"}; }

    double angle() const { return element(leaf_id()).params.num("angle"); }
    // Open far enough to walk through.
    bool open() const { return element(way_id()).params.num("open") > 0.5; }
    // How far open past which whoever walks may go through (radians).
    static constexpr double kPassable = 0.5;
    // How long a tried door rattles (seconds of its own time).
    static constexpr double kRattle = 0.35;
};

// A door's leaf to its hinge (a thing of the room it hangs in): shut, turned
// by the angle it stands open, shaking a little while it rattles.
void door_hinge(const Element& leaf, Element& hinge);
// A door's way to the doorway its leaf hangs in: walked through (`leave`)
// only while it stands open; nothing seen past it (`closed`) while shut close.
void door_near(const Element& way, Element& doorway);
// And to the doorway on its far side: walked through only while open.
void door_far(const Element& way, Element& doorway);

}  // namespace sg
