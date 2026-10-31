// Stategine - guardrails: the mistakes a built place was found with, each
// named by what it is and where, so that a place showing one is refused
// before anyone walks it (docs/workflows.md section 14, *Guardrails*).
//
// Each is a check of a space as it stands - its things' boxes, as the walker
// and the renderer read them (an element's position its foot's middle; `sx`
// `sy` `sz` its size; `yaw` `pitch` `roll` its turn; a doorway's pose its
// opening's middle). Every fault names the thing, the other thing and the
// place, in metres. They are cheap - a few milliseconds for a room of
// thousands of things - and run where the world's own faults run: in
// GLWorldView::prepare (every world first drawn), and by `guard_faults` in
// any project's laws pass. What a check cannot know, the thing says:
//
//   mount        "wall" | "floor" | "ceiling" | "free": what it hangs on or
//                stands on. Unsaid, a thing named for what hangs on a wall
//                (a sconce, a switch, a socket) is on a wall, and a thing of
//                furniture's size is on what is under it.
//   in_opening   1: it belongs in a doorway (a leaf, a frame, a curtain, a
//                sill) - a moulding never does.
//   blind        1: a door or a frame that leads nowhere on purpose (a door
//                painted shut, a false front).
//   climb        1: a walker may step up onto it (a stair's tread, a step).
//   bare         1: something used (`use`) that is meant to be nothing seen.
//   taken        1: gone from where it was - nothing of it drawn, cast or
//                felt (better still: not `alive`).
#pragma once
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

class StateGraph;

// 1. What hangs on a wall touches it - not sunk into it, not off it; what
// stands on a floor stands on it (or on what stands on it) - not hovering,
// not sunk into it.
std::vector<std::string> mount_faults(const State& space);

// 2. A doorway is open: nothing drawn runs across its opening - a skirting,
// a dado, a rail, a cornice, a wainscot stops at its jambs - but what says
// it belongs there (`in_opening`, a door's own parts).
std::vector<std::string> opening_faults(const State& space);

// 3. A door leads somewhere: a thing called a door or a door's frame stands
// at a doorway (a portal walked through, a hole) - or says `blind`. And a
// doorway walked through has a room behind it: its seam (given the graph).
std::vector<std::string> door_faults(const State& space, const StateGraph* graph = nullptr);

// 4. No two drawn tops or bottoms lie level with each other (within 2 mm),
// overlapping, of much one size: they fight (coinciding says the same of
// upright faces).
std::vector<std::string> level_faults(const State& space);

// 4, at a seam: a room's tops and bottoms near a doorway glued to another
// room, and the other's carried through the doorway, do not lie level and
// overlapping (two floors run past the threshold into each other) - where
// they are drawn in one depth buffer: rooms the renderer glues as one
// (sg::sheafify as it grows a space: both doorways saying `sheaf`, standing
// in each other by no more than 0.6 m). A room only seen through a walked
// doorway is clipped at its plane, and never fights. (A program that asks
// Quality::sheafify glues more: its lens shows those.)
std::vector<std::string> seam_level_faults(const State& space, const StateGraph& graph);

// 5. No solid thing stands inside another (more than 3 cm each way), but
// what is part of it (its parent, its group, what it says it is `in`).
std::vector<std::string> inside_faults(const State& space);

// 6. Everything used (`use`, `pt_use`, `pad`) that is not drawn has a drawn
// thing within 5 cm of it - a switch is a switch seen.
std::vector<std::string> pad_faults(const State& space);

// 8, 9. What is `taken` (or `hidden`) is gone: not drawn, not casting a
// shadow, not mirrored, not solid.
std::vector<std::string> taken_faults(const State& space);

// 10. A walker climbs only what says it may be climbed: a solid top it can
// step up onto from the floor (or from another such top) - its step the
// camera's `step` (0.36 unless it says, as sg::walk), or `step` given - is a fault, unless it says `climb` or
// `walk`. And a walker that says a step higher than 0.36 m (a riser and more) is one.
std::vector<std::string> climb_faults(const State& space, double step = -1.0);

// All of them, each fault said with the space's name.
std::vector<std::string> guard_faults(const State& space, const StateGraph* graph = nullptr);

}  // namespace sg
