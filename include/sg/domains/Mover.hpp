// Stategine - a mover: a body's motion made by MotionBricks as it is steered.
//
// A state of its own (kind `mover`), on a line of its own: told where to go
// and how fast (`go`), or to stop (`stand`), it makes its motion a brick at a
// time (sg/domains/Bricks.hpp) - each from the last poses of the one before,
// toward a target placed on the way - and plays it. What it is now is its
// elements: `self` (where it stands on its place's ground, `x z yaw`, and
// what it was told), and a joint of the SOMA skeleton each (`j.<name>`:
// `pq*` its turn in the mover's own frame, ahead +x; the hips' place `px py
// pz` there too). The brick it plays is its params too (`brick`: its context
// and target as text, `t` how far in) - the motion itself is made from them,
// kept by the model for as long as it is asked for (memoised), and asked
// for a little before it is needed, so a step waits on nothing.
//
// It knows no body and no room. Carried onto a body by `move_body` (a kept
// functor: each joint's turn as what leads the body's own, the body's bind
// kept - any rig whose joints are named as SOMA's, MHR's or Mixamo's), and
// onto where the body stands in a place by `move_spot` (a kept functor: its
// ground into the spot's x, z, yaw). With no model to run (no ONNX Runtime,
// no .onnx), it stands as it was put and says why (`faults`).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {

class Being;
class Spatial3D;

class Mover : public State {
public:
    static Key self_id() { return Key{"self"}; }
    // At `x, z` on its place's ground, facing `yaw` (a yaw takes x toward z).
    explicit Mover(Key id, double x = 0.0, double z = 0.0, double yaw = 0.0);
    Key kind() const override { return Key{"mover"}; }

    Key live_event() const { return Key{id().str() + ".live"}; }    // driven: {dt}
    // {x, z: where to, speed (metres a second; 0: stand where it is), face (a
    // yaw to stand facing there; unsaid: the way it came)}
    Key go_event() const { return Key{id().str() + ".go"}; }
    Key stand_event() const { return Key{id().str() + ".stand"}; }  // stop where it is

    std::vector<std::string> faults() const override;
};

// The SOMA joints of the mover each a joint of `b` leads (by name: SOMA's
// own, MHR's or Mixamo's - whichever `b` has most of).
std::vector<std::pair<std::string, std::string>> bricks_joints(const Being& b);
// `m`'s motion carried onto `b`: a kept functor (`<m>.moves.<b>`), each joint
// `b` has led to the mover's turn away from SOMA's reference, put on `b`'s
// own bind; its hips' place as high for its size. `b` follows it as far as
// it gives itself (`lead`: set to 1 here). Returns the functor's name.
Key move_body(StateGraph& g, const Mover& m, Being& b);
// Where `m` stands carried onto `spot` in `host` (its x, z and yaw): a kept
// functor (`<m>.stands.<host>`). Returns its name.
Key move_spot(StateGraph& g, const Mover& m, const Spatial3D& host, Key spot);

}  // namespace sg
