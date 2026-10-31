// The space a physics is the physics of: one room, or rooms made one.
//
// A physics knows no room; whoever makes it says what its space is. Rooms
// glued by a seam are each their own - their own physics, as their own frame
// and look. Rooms sheafified (sg::sheafify: their doorways say `sheaf`, and
// the two are one kind of space there) are one space, and so one physics:
// each room where it stands in the physics' frame, a thing rolled or thrown
// through a doorway made one going on into the room beyond, as it is drawn
// there. Its floors, ceilings and walls are then its supports, room by
// room - not a box of its own (`shell` 0) - and a doorway that is not made
// one is stopped: what is beyond it is another world.
#pragma once

#include <vector>

#include "sg/domains/Physics.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::physics {

// Each room's floor, ceiling and walls laid in `ph` where the room stands in
// its frame (`rooms`: as sg::sheafify places them, the first in the frame
// the physics is in), and a plug just beyond every doorway walked through
// that is not made one with the room beyond (a room's `doorways`). More than
// one room: the physics has no shell of its own (`shell` 0).
void lay_space(Physics& ph, const std::vector<sg::PlacedRoom>& rooms);

// Where a room's walls and floor reach, in its own frame (its walls', or the
// size it says it is): lo and hi corners; lo.x > hi.x if it says neither.
void room_reach(const sg::Spatial3D& room, sg::Vec3d& lo, sg::Vec3d& hi);

}  // namespace sg::physics
