// Stategine - the hand: what a mouse and a few keys mean to the loose things
// of the room one stands in, and to the parts of it that move.
//
// It holds nothing of the world - only what the buttons were, what it has
// hold of and how - and changes nothing itself: it tells the room's physics
// (sg/domains/Loose.hpp, its `phys.*` ports) and the room (its `work` port)
// what it does, by engine.send, next frame; their own arrows do it.
//
//   click            take up the loose thing the eye is on (Shift: by that
//                    point, to drag it, tip it, turn it over)
//   holding          carried in front of the eye, upright as it was taken
//            click   (a solid) to be put down: it hangs just clear over where
//                    the eye is - a top, the floor; a click again, it stands
//                    there; the wheel, back in front
//            Q       tipped a quarter over
//            R held  the mouse turns it every way
//            F       brought up close to look at; the mouse turns it over,
//                    the wheel brings it nearer; a click, done looking
//            wheel   nearer or further
//            click   (paper) pinned to the side of what the eye is on, or
//                    laid face up on a top
//            right   thrown
//   a moving part    (a knob, a lid, a lever, a cabinet's door - what says
//                    `joint`): pressed, held and dragged, the room is told
//                    (`<room>.work {part, phase, dx, dy}`: phase 0 taken
//                    hold of, 1 dragged, 2 let go) and its own arrow moves
//                    the joint; the view stays still while it is held.
//
// Written for Memory Leak (its src/hand.cpp, where books are read, papers
// turned and people shoved besides) and moved here in what is general.
#pragma once

#include <string>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Room.hpp"
#include "sg/physics/Rigid.hpp"

namespace sg {

struct HandButtons {
    bool left = false, right = false;   // held down now
    bool tip = false, look = false;     // Q, F pressed this frame
    bool turn = false, shift = false;   // R, Shift held down now
    double dx = 0, dy = 0, wheel = 0;   // the mouse moved, the wheel turned, this frame
    bool free = true;                   // the hand may act (the mouse captured)
};

class Hand {
public:
    // The event the hand tells `room` a moving part is worked by:
    // {part, phase (0 taken hold of, 1 dragged, 2 let go), dx, dy (the
    // mouse, this frame), held (seconds since it was taken)}.
    static Key work_event(const Room& room) { return Key{room.id().str() + ".work"}; }
    // How far the eye reaches.
    static constexpr double kReach = 2.2;

    // One frame of it in `room` (its camera the eye). True if the mouse is
    // the hand's this frame (turning what it holds, looking at it close,
    // working a part): the view stays where it is.
    bool frame(Engine& engine, const StateGraph& g, const Room& room, const HandButtons& b, double dt);
    // Whether it has anything (a loose thing, or a part it works).
    bool holding() const { return !held_.empty() || !working_.empty(); }
    // The loose thing in hand (its body: `@<thing>`), if any.
    Key held() const { return held_; }
    // The part being worked, if any.
    Key working() const { return working_; }
    bool inspecting() const { return inspecting_; }
    // Let go of whatever it has, where it is, now.
    void let_go(Engine& engine, const StateGraph& g);
    // What `room` must let in for the hand to work in it - its `work` port,
    // its physics' ports - said at start so a program can declare them (its
    // .sg's `port`), never met in play. Empty when all are declared.
    static std::vector<std::string> missing_ports(const StateGraph& g, const Room& room);

private:
    bool was_left_ = false, was_right_ = false;
    Key held_;
    Key held_in_;
    rigid::M3 hold_rel_;
    double hold_dist_ = 0.55, hold_roll_ = 0.0, grab_reach_ = -1.0;
    bool inspecting_ = false, placing_ = false;
    double inspect_yaw_ = 0, inspect_pitch_ = 0, inspect_dist_ = 0.26;
    // A part worked: which, in which room, for how long.
    Key working_, working_in_;
    double worked_for_ = 0.0;
};

}  // namespace sg
