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

#include <optional>

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
    double stride = 0;    // how fast its walk goes as made (metres a second); 0: as its feet say (Being::ground_speed)
    bool plant = true;    // its feet set down on the ground as it goes (Being::plant, at its ankles)
    Vec3d at;             // its feet
    double yaw = 0;
    bool always = false;  // lives whether its place is entered or not (Keeps::Always)
    // Its motion made by MotionBricks as it is steered (sg/domains/Mover.hpp):
    // a mover of its own, `<name>.mover`, on the same clock, leading its body
    // and carrying its spot - told where to go at its ports (`go`, `stand`).
    // Its walk clip and its feet's planting are not used.
    bool moved = false;
};

// Where `being` stands in `host`.
Key spot_of(const Spatial3D& host, Key being);
// The mover of a person cast `moved`.
inline Key mover_of(Key being) { return Key{being.str() + ".mover"}; }

// A person: read, fitted, stood at its spot, shown, its spirit driven on
// `clock`, playing its first clip. If it walks (`walk` read beside its own
// clips), standing and walking are blended by pace (`go`), steered at its
// ports (`pace`). Its id (`name`); empty, and why, if it cannot be read.
// (Its files are `Being::files()`.)
Key cast(StateGraph& g, Spatial3D& host, Temporal& clock, Key name, const Casting& how, std::string* why = nullptr);

// Whether it walks; and the events that walk it at `speed` (metres a second)
// ahead, turning at `turn` (radians a second, as its yaw goes), its feet as
// fast as it goes - for its ports: its travel (Being::travel_event, so its
// feet set down stay put), and how it walks. Nearly still, its own standing
// clip played as it is (not the blend at nought, which stands it lower): told
// only as it comes to a stop, as it plays now says. Standing and walking
// change into each other inertialized. `velocity`: any way across its
// ground, in its own frame (ahead is +x).
bool walks(const Being& b);
std::vector<Event> pace(const Being& b, double speed, double turn = 0);
std::vector<Event> pace(const Being& b, const Vec3d& velocity, double turn);
// The same for a velocity across `host` (as the place has it): turned into
// its own frame by how its spot in the place is turned.
std::vector<Event> pace(const Spatial3D& host, const Being& b, const Vec3d& velocity, double turn);

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

// --- a body against what is round it ----------------------------------------------
// What `b` (cast in `host`) is told at its ports to rest against the place as
// a body does - its hand laid on a wall, its head leant on it - worked out
// each time from where its joints stand now and what the place's rays meet
// (sg::ray: what stops a walker), never from points given. Its walk and its
// motion (a mover's, a clip's) carry it; these lay its hand and head over
// that, as a game's own reach does over its animation.

// Its hand (`goal`: a reach of two links ending at `wrist`, its upper arm
// `shoulder`) laid on the nearest surface its arm reaches - the way `toward`
// says (the place's), or, given none, the nearest round it from its side to
// its front, a little below its shoulder, where the arm rests easiest (some
// seven tenths of its reach) - the wrist `palm` off it, the elbow bent down
// and away from it. None if nothing is in reach; `dist`, how far from the
// shoulder the nearest surface is.
// And the hand laid flat on it, if it has its own turn as a goal
// (`<goal>_palm`, palm_goal): its fingers up along the surface, its back
// out from it. Empty if nothing is in reach.
std::vector<Event> brace(const Spatial3D& host, const Being& b, const std::string& goal, const std::string& shoulder, const std::string& wrist,
                         const Vec3d& toward = {}, double palm = 0.035, double* dist = nullptr);
// A hand's own turn as a goal (`name`, looked with, its tip `wrist` alone):
// its forward the way its fingers go (the wrist to `middle`'s root), its up
// the back of the hand (across `index` to `little`, the side away from its
// `thumb`) - read from its bones as they stand now, as it was bound.
Element& palm_goal(Being& b, const std::string& name, const std::string& wrist, const std::string& middle, const std::string& index,
                   const std::string& little, const std::string& thumb);
// Its head (`goal`: looked with, ending at `head`, its neck `neck`) looking
// at `look` (the place's), its crown tipped toward the nearest wall round it
// as far as brings its skull (`radius` round a point `above` over the head
// joint) to meet it - tipped away from one nearer than that; none within
// `near`, upright. Measured from where the skull would be upright on its
// neck, so it comes to rest leant, not hunting.
std::optional<Event> lean(const Spatial3D& host, const Being& b, const std::string& goal, const std::string& neck, const std::string& head, const Vec3d& look,
                          double radius = 0.095, double above = 0.09, double near = 0.45);

}  // namespace sg
