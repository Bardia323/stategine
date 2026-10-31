// What lies on a thing goes with it.
//
// A thing moved from outside - by the planner, a command, a variant of its
// room - is not moved by the physics, so nothing it holds up feels it go:
// a marker on a whiteboard's tray, a tape on a television would be left in
// the air where it was. The one rule for every place that moves a thing so:
// what lies on it (found by where things are, sg::rigid::World::resting_on)
// is moved as it is, the same way, and stays as it lay.
#pragma once

#include "sg/core/State.hpp"
#include "sg/physics/Rigid.hpp"

namespace sg::physics {

// A host that stood at (`from_x`, `from_r`) and stands now at (`to_x`, `to_r`).
struct Carry {
    sg::rigid::V3 from_x, to_x;
    sg::rigid::M3 from_r, to_r;

    static Carry between(sg::rigid::V3 from_x, const sg::rigid::M3& from_r, sg::rigid::V3 to_x, const sg::rigid::M3& to_r);

    // Whether it has moved at all, past the noise of turning to euler angles and back.
    bool moved() const;

    // A solid rider's place, taken along (its frame, as a body's is).
    void body(sg::rigid::V3& x, sg::rigid::M3& r) const;

    // An element that is a solid body's twin (x, y, z, yaw, pitch, roll), taken along.
    void solid(sg::Element& e) const;

    // A sheet, a print, a tape lying flat: carried along the floor and turned
    // about the vertical as its host is.
    void flat(sg::Element& e) const;
};

}  // namespace sg::physics
