// Stategine - people put in a place: a being (sg::Being) read from a rigged
// model, fitted to a person's height and stood in a Spatial3D - its body
// carried there by the engine's functor (sg::show), embedded where it stands,
// its spirit on a line of its own. The ways any game casts someone, said once.
//
// Where it stands is an element of the place: a portal, `<host>.<being>_spot`,
// that its skins ride (`show_skins`) and its faces are worn on (sg::Face); it
// is shown there through an embedding `<being>.here`, opened with the place.
//
//   sg::Casting c;
//   c.glb = "bruce.glb", c.height = 1.85, c.at = {2, 0, 3}, c.yaw = 1.2;
//   const sg::Key bruce = sg::cast(g, room, clock, "bruce", c);
#pragma once

#include <string>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

class Being;
class Temporal;

struct Casting {
    std::string glb;      // the rigged model (glTF): its first clip what it does standing
    std::string walk;     // a glTF of the same skeleton's walk (no skin), if it walks
    double height = 1.8;  // metres
    double stride = 0.88; // how fast its walk goes as made (metres a second)
    Vec3d at;             // its feet
    double yaw = 0;
    bool always = false;  // lives whether its place is entered or not (Keeps::Always)
};

// Where `being` stands in `host`.
Key spot_of(const Spatial3D& host, Key being);

// A person: read, fitted, stood at its spot, shown, its spirit driven on
// `clock`, playing its first clip. If it walks (`walk` read beside its own
// clips), standing and walking are blended by pace (`go`), steered at its
// ports (`pace`). Its id (`name`); empty, and why, if it cannot be read.
// (Its files are `Being::files()`.)
Key cast(StateGraph& g, Spatial3D& host, Temporal& clock, Key name, const Casting& how, std::string* why = nullptr);

// Whether it walks; and the events that walk it at `speed` (metres a second),
// its feet as fast as it goes - for its ports. Nearly still, its own standing
// clip played as it is (not the blend at nought, which stands it lower): told
// only as it comes to a stop, as it plays now says.
bool walks(const Being& b);
std::vector<Event> pace(const Being& b, double speed);

// `led` moved by another's motion: `glb`, a glTF of motion alone (no skin),
// read as a being of its own (`<led>_lead`), fitted `height` tall, standing
// unseen at a spot of its own in `host` (embedded `<led>_lead.here`, opened
// with it), its first clip played at `speed` on `clock` (always: a skeleton,
// cheap) - and carried onto `led` by the engine's retarget, as its own motion
// (followed: a ragdoll's knocks still on top). Its id; empty, and why, if it
// cannot be read or shares no joints.
Key lead(StateGraph& g, Spatial3D& host, Temporal& clock, Being& led, const std::string& glb, double height, double speed, std::string* why = nullptr);

// Where a ray from `eye` along `dir` first meets the body of `being` (as its
// ragdoll, `<being>.rag`, has it) within `reach`: the bone met, the point met
// and the ray's way, both in the being's own frame (as its ragdoll's ports
// take them).
struct Touch {
    Key rag;
    std::string bone;
    Vec3d at, dir;
    double t = 0;
};
bool touch(const StateGraph& g, const Spatial3D& host, Key being, const Vec3d& eye, const Vec3d& dir, double reach, Touch& out);
// A point of the place in the being's own frame.
Vec3d in_being(const Spatial3D& host, Key being, const Vec3d& p);

}  // namespace sg
