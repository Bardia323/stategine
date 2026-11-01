// Stategine - the loose things of a room, given bodies.
//
// A room does not simulate anything. What in it can be taken up, knocked or
// thrown - a cup, a toothbrush, a book, a note - only says so (params on the
// thing's parts: `loose_thing`, `loose_note`), and a physics of the room's own
// (`Bodies`, a sg::physics::Physics) gives each a body, made from what the
// room is: its walls, floor, ceiling and a plug in every doorway
// (lay_space), every other thing that stands in it a fixed box (by the
// boxes it says it is made of, `hulls`, else by the top of its model, else
// by its box), and a body a loose thing. From then on its bodies move it:
// each of a loose thing's parts is a `part` of the physics, posed from its
// body after every step, and carried back to the room's thing by a functor
// (`<physics>.out.<room>`, which the program's .sg declares with the
// embedding the physics is open in - the room's `loose` portal).
//
//   parts --step--> parts   each part where its body is now (written only
//                           where it moved: asleep, nothing is); a sheet's
//                           parts sagging (`bend`) while it is in a hand;
//                           a hidden body's parts unseen
//   parts --hide--> parts   a body hidden, or seen again
//
// Whoever walks in the room is kept out of its solid bodies and shoves them:
// `walk_among` gives the physics a walker of its own, carried to from the
// room's eye (a kept functor: where it is, whether it walks), and says back
// the way it was kept out (`<room>.pushed`: `push_x push_z push_n`, by a kept
// functor) - which the room takes on its own step (the lab's Realm does).
//
// Written for Memory Leak's house (its src/loose and src/things.cpp), and
// moved here unchanged in what it does: any room's loose things.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Physics.hpp"
#include "sg/domains/Room.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::physics {

class Bodies : public Physics {
public:
    Bodies(sg::Key id, double room_w, double room_d, double room_h);

    // Something that does not move: a box round `centre`, `half` its size,
    // turned `yaw` about up - a support.
    void fixed(const std::string& name, const Vec3d& centre, const Vec3d& half, double yaw = 0.0);
    // A solid loose thing: a box round `centre`, `half` its size, turned
    // (yaw, pitch, roll) as the renderer turns a mesh, `mass` kg, at rest.
    sg::Element& solid(sg::Key id, const Vec3d& centre, const Vec3d& half, double yaw, double pitch, double roll, double mass);
    // A sheet of paper: `w` wide, `h` high, `thick`, its face the way `face`
    // points and its writing's up `up` (both in the room); pinned where it
    // is, or lying.
    sg::Element& sheet(sg::Key id, const Vec3d& centre, double w, double h, double thick, const Vec3d& face, const Vec3d& up, bool pinned);
    // A part of a thing of the room, posed as it is now - a box `lift` high
    // above its foot, turned yaw, pitch, roll about its middle `centre` (as
    // the room's things turn) - that goes where `body` goes from now on.
    sg::Element& part(sg::Key part, sg::Key body, const Vec3d& centre, double lift, double yaw, double pitch, double roll, bool unseen = false,
                      double cast = 1.0);
    // A body hidden from sight (`on`), or seen again: its parts unseen and
    // casting no shadow while it is.
    static sg::Key hide_event() { return sg::Key{"loose.hide"}; }
    static sg::Event hide(sg::Key body, bool on) { return sg::Event{hide_event(), sg::Params{}.set("id", body.str()).set("on", on ? 1.0 : 0.0)}; }

    // How many bodies are on the move (falling, sent, or in a hand).
    int awake() const;

    // Whoever walks in a room whose things are here: an element of its own
    // (`<bodies>.walker.<room>`) - on these bodies' own step, kept out of the
    // solid ones, what it walks into shoved and whoever it walks into told
    // (knock_event); the way it is kept out said back (`push_x`, `push_z`
    // in the room's frame, `push_n` counting them), the room taking it and
    // saying so (`taken`) before another is worked out. Its id.
    sg::Key walker(sg::Key room);

private:
    void pose_parts(double dt);
    void walk(double dt);
    // Each part's body as it was when the part was last posed (by its
    // stamp): derived, so a part whose body has not moved is not visited.
    std::unordered_map<sg::Key, uint64_t> posed_;
};

// What of a room is loose: `thing` (a part of `r`, named without the room's
// names) and every part of it one loose thing, `mass` kg, sounding as
// `sound` says it is made of. A thing as thin as paper (one part a few
// millimetres thin, forty times as wide) is never a rigid block - it would
// fall as wood does: it is made a sheet (loose_note, lying), and said so.
void loose_thing(Room& r, const std::string& thing, double mass, const std::string& sound = "wood");
// A note: paper, its writing's up `up` in its own frame; pinned where it is, or lying.
void loose_note(Room& r, const std::string& thing, const Vec3d& up = {0, 1, 0}, bool pinned = true);
// The mesh parts of `thing` in `r`: `<names><thing>` and `<names><thing>.*`.
std::vector<sg::Element*> parts_of(Room& r, const std::string& thing);

// The physics of `r`: made (`<name>_loose`), and filled from the room as it
// is now - its walls, floor and plugs (lay_space), its fixed boxes and a
// body for each loose thing - with the portal it is embedded in
// (`<room>.loose`). Before the program's .sg declares how it is joined.
Bodies& give_bodies(sg::StateGraph& g, Room& r, const std::string& name);
// A physics made empty, sized as `r`, with the portal it is embedded in: for
// a space of rooms made one, filled when its seams are there to say so.
Bodies& make_bodies(sg::StateGraph& g, Room& r, const std::string& name);
// What stands in `r` and what is loose in it, laid in `ph` where `r` stands
// in its frame (`at`).
void fill_bodies(sg::StateGraph& g, Bodies& ph, Room& r, const sg::Pose& at);
// Where a room stands in its physics' frame (said on the portal the physics
// is embedded in): its own, or where it is in a physics over several.
sg::Pose physics_frame(const Room& r);
void set_physics_frame(Room& r, const sg::Pose& at);
// Another room's own portal for a physics already made (the same room on
// another sheet of a house): open, for as long as the room is.
void lend_bodies(Room& r);
// Its parts carried to the things of each room it is open in, each to the
// thing it is there (`<physics>.out.<room>`, declared by the .sg): after it
// is declared. A part goes to the thing of its own name, or to the thing of
// its name in another sheet of the same room; carried from the physics'
// frame into the room's. A part carried to no thing is said (stderr).
void join_bodies(sg::StateGraph& g, Bodies& ph, const std::vector<Room*>& rooms);
// The physics a room's things are in (the one its `loose` portal is open to), if any.
Bodies* bodies_of(sg::StateGraph& g, sg::Key room);
const Bodies* bodies_of(const sg::StateGraph& g, sg::Key room);
// Whoever walks in `r`, among the loose things of its bodies (if it has
// any): their walker, and the kept functors that carry its eye to it and the
// way it is kept out back (`<room>.pushed`, made here if the room has none).
// False if it has no bodies.
bool walk_among(sg::StateGraph& g, Room& r);

}  // namespace sg::physics
