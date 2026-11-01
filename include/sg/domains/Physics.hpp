// Physics, as a state of its own: the loose things of a space - their
// bodies, what they lie on and knock into, someone's limbs - knowing no room
// (sg::physics::lay_space says what space it is the physics of).
//
// The room does not simulate anything. Loose things in it - sheets and prints -
// have a twin here, a `body`, and the two are joined by a lens: the room's
// pose of a thing is read into its body when the physics portal opens, and
// every frame after that the body's pose is written back (EmbedSync::Live).
// So what moves a sheet is always an arrow of this state:
//
//   body --step--> body   gravity, drag and flutter, landing on what is below
//   body --grab--> body   into the hand
//   body --hold--> body   where the hand is now
//   body --drop--> body   let go, with some velocity
//   body --pin-->  body   flown to a wall and pinned there
//   body --lay-->  body   flown to a surface and laid on it
//
// Bodies are solid to the room: held or falling, they are pushed out of the
// walls, floor, ceiling and furniture the shortest way, so a sheet carried
// into a wall slides along it instead of through it.
//
// Every arrow is fired by one event that names the body in `id`; the arrows of
// the other bodies see it and do nothing. The furniture arrives from the room
// through a functor as `support`s: a footprint and a top, nothing else.
//
// Solid things - a book (`box`), or a thing of many parts, a mug or a chair
// (`rigid`, its `hulls` listed) - are rigid bodies, stepped together by one
// arrow (rigid.hpp), so they knock into, stack on and tip over each other:
//
//   rigid --step--> rigid   every solid body that is awake, at once
//
// Held, such a body hangs from the hand by a spring: a light one whole and
// square to the eye, a heavy one by the point it was taken hold of (`free`),
// so it drags, tips, or turns over.
//
// Someone's body is here too, a `limb` a bone (a being's ragdoll's, carried
// in by a functor): moved where its owner has it, never by what it meets -
// it pushes loose things aside and stands in a walker's way - and what
// strikes it, or walks into it, is counted for its owner to take (`knock_n`,
// the impulse `kx ky kz` in the owner's frame), carried back by a functor.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sg/domains/PhysicsCarry.hpp"
#include "sg/physics/Rigid.hpp"
#include "sg/physics/Rope.hpp"
#include "sg/core/State.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::physics {

inline constexpr double kPi = 3.14159265358979;
inline constexpr double kFlat = kPi * 0.5;  // pitch of a sheet lying face up

namespace mode {
inline const std::string rest = "rest", pinned = "pinned", fall = "fall", held = "held", send = "send";
}

// A vector turned as the renderer turns a panel - yaw, then pitch, then
// roll - in doubles (local +x is a panel's face, +y its up, +z its width).
sg::Vec3d panel_turn(const sg::Vec3d& v, double yaw, double pitch, double roll);

// The text a body holds under `k` (its mode, its shape), read where it is:
// the physics asks this of every body under every body, every step.
const std::string& text_of(const sg::Element& e, sg::Key k);

// The shortest way round from `a` to `b`.
double angle_to(double a, double b);

class Physics : public sg::State {
public:
    static sg::Key step_event() { return sg::Key{"phys.step"}; }
    static sg::Key grab_event() { return sg::Key{"phys.grab"}; }
    static sg::Key hold_event() { return sg::Key{"phys.hold"}; }
    static sg::Key drop_event() { return sg::Key{"phys.drop"}; }
    static sg::Key pin_event() { return sg::Key{"phys.pin"}; }
    static sg::Key lay_event() { return sg::Key{"phys.lay"}; }
    // The supports moved (the room says so): what rests on them goes along.
    static sg::Key ride_event() { return sg::Key{"phys.ride"}; }
    // Said after a step in which anything it holds moved, for whatever
    // follows where things are.
    static sg::Key moved_event() { return sg::Key{"phys.moved"}; }
    // Said when a hand takes something {id}, for whatever it stood in.
    static sg::Key grabbed_event() { return sg::Key{"phys.grabbed"}; }
    // What a step heard, for whoever listens: something struck {id, hit}
    // (how hard), and something that was falling come to rest {id}.
    static sg::Key hit_event() { return sg::Key{"phys.hit"}; }
    static sg::Key landed_event() { return sg::Key{"phys.landed"}; }
    // Said: what knocked in a step, heard - `sound` hit.<what it is made of>, one a kind a step.
    static sg::Key knocked_event() { return sg::Key{"phys.knocked"}; }
    // Said whenever strand data changes, including the last step before sleep.
    static sg::Key cords_event() { return sg::Key{"phys.cords"}; }
    // Carried along by what holds it - a printer feeding a page - to {x, y,
    // z, yaw}, lying flat (feed); let go there, to lie on what is under it
    // (fed). Only while it is held, and not by a hand (`by`, set by grab).
    static sg::Key feed_event() { return sg::Key{"phys.feed"}; }
    static sg::Key fed_event() { return sg::Key{"phys.fed"}; }
    // Someone walked into a limb {id, x y z: what they carry (on meeting),
    // lx ly lz: their lean (while they lean on)}: the limb's knock, counted.
    static sg::Key knock_event() { return sg::Key{"phys.knock"}; }

    Physics(sg::Key id, double room_w, double room_d, double room_h);

    sg::Key kind() const override { return sg::Key{"physics"}; }


    // A footprint things can rest on. Filled in by the room's `solids` functor.
    sg::Element& add_support(sg::Key id) { return add_element(id, sg::Key{"support"}); }

    // Someone's bone, whose owner stands at `origin` turned `yaw` in the room.
    sg::Element& add_limb(sg::Key id, const sg::Vec3d& origin, double yaw, int group);

    // A loose, thin thing: `drag` is how much the air holds it up (paper
    // floats, a print drops). A `sheet` is w x h with its face along its
    // pitch; a `stick` (a piece of chalk) is w long along its heading, h wide
    // and `thick` high, and never tips over. A `box` (a book) is sized as a
    // stick but turns every way: thrown, it tumbles, bounces, rubs to a stop
    // and comes to rest on one of its faces.
    sg::Element& add_body(sg::Key id, double w, double h, double thick, double drag,
                          const std::string& shape = "sheet");

    // Laid down at {x, y, z, yaw}: on whatever is under it there, not in it.
    static void lay(Physics& self, sg::Element& e, const sg::Event& ev);

    // The highest thing under (x, z) that is no higher than `below`: the floor,
    // a support, or a sheet already lying there. `self` is never its own floor.
    double top_at(double x, double z, double below, sg::Key self = sg::Key{}) const;

    // The highest thing under the whole of `e` as it lies - not just under its
    // middle - that is no higher than `below`: supports under its middle, and
    // any resting thing its footprint overlaps. Two sheets that overlap at all
    // stack; they never lie in one plane.
    double top_under(const sg::Element& e, double below) const;

    // Put a body straight into a pose, as the room is built.
    // Moved from where it is, a little, by a hand that hauls it: the next
    // step drives it there from where it was (`was_*`, kept until then), so
    // it pushes what is in its way and carries what lies on it.
    static void haul(sg::Element& b, const sg::Vec3d& p, double yaw, double pitch, double roll, const std::string& m);
    static void forget_was(sg::Element& e);

    static void place(sg::Element& b, const sg::Vec3d& p, double yaw, double pitch, double roll,
                      const std::string& m);

    // Half its size along x, y and z as it is turned now: a sheet held upright
    // is tall and thin, the same sheet lying down is flat.
    static sg::Vec3d extents(const sg::Element& e);

    // Push a body out of whatever solid it is in: the room's shell, then each
    // piece of furniture, each the shortest way out. Velocity into a surface
    // it was pushed out of is lost.
    // `floors`: false for a body whose own contacts hold it up (a box) -
    // only the walls and the sides of furniture push it here.
    void collide(sg::Element& e, bool floors = true) const;

    // A mesh's turn, as the renderer makes it: yaw about y, then pitch about
    // z, then roll about x (Mat4::rotate_y * rotate_z * rotate_x).
    static sg::Vec3d box_turn(sg::Vec3d v, double yaw, double pitch, double roll);

    // --- solid bodies --------------------------------------------------------------------
    static double solid_top(const sg::rigid::Body& b, double x, double z, double below) { return sg::rigid::surface_at(b, x, z, below); }

    // Heavier than this, a thing is held by where it was taken hold of, not
    // whole: a chair, a plant, a floor lamp are carried; a table is not.
    static constexpr double kCarry = 16.0;
    // Heavier than this, a loose thing stands in the way of others, and is
    // moved about the room as furniture is (not dropped on whatever is there).
    static constexpr double kBig = 4.0;

    static bool solid_shape(const std::string& s) { return s == "box" || s == "rigid"; }

    // A part of a `rigid` body: a box, or a cylinder (a prism of many sides,
    // tapered by `taper`), its middle at `centre` in the body's frame.
    // `friction`: the part's own, where it differs from the body's (a castor
    // rolls); below 0, the body's.
    static void add_hull(sg::Element& e, bool cylinder, sg::Vec3d centre, sg::Vec3d half, double yaw = 0, double pitch = 0,
                         double roll = 0, double taper = 1.0, double friction = -1.0);

    // The hulls of a solid body, in its own frame.
    static std::vector<sg::rigid::Hull> hulls_of(const sg::Element& e);

    // Someone walking (sg::rigid::World::walk_into): kept out of the solid
    // bodies, and what they walk into shoved - awake, and moving as the
    // shove has it.
    // A limb walked into is not written here: it is said in `limbs` (its id,
    // what the walker carries, their lean), for the walker to tell (knock_event).
    struct Shove {
        std::string id;
        sg::rigid::V3 carried, lean;
    };
    sg::rigid::V3 walk_into(sg::rigid::V3 p, double radius, double y0, double y1, sg::rigid::V3 moved, double dt, std::vector<Shove>* limbs = nullptr);

    // The rigid bodies as they stand (for whoever aims at them, or walks into them).
    const sg::rigid::World& solids() const { return world_; }
    sg::rigid::World& solids() { return world_; }

    // Every solid body that is awake, stepped together; the room's shell and
    // its furniture (the supports) are what they stand on and knock into.
    //
    // What a body is doing is all in its element - where it is, how it is
    // turned, how fast it goes and turns, whether it sleeps (`mode` rest)
    // and how long it has been still - and is read from there every step:
    // the solver keeps only its hulls, and what it pushed each contact with
    // last step, kept by what the step started from. The same start, the
    // same step: an arrow of this state, whatever ran it before.
    void step_rigid(double dt, double time = 0);

    // --- what a thing is on ------------------------------------------------------------------
    // Everything that lies on or is stuck to something, taken where that
    // now is - at once, for whoever has just moved the supports.
    void ride_all();
    // Anything lying on something, or pinned to it, goes where that goes:
    // furniture pushed, turned or carried off takes what rests on it and what
    // is stuck to it, as they lie. What a thing is on is found once, when it
    // comes to rest or is pinned (`on`: a support, or `-` for the floor and the
    // walls, which do not move), with where that was then (`on_x`, `on_z`,
    // `on_yaw`, `on_top`); it is forgotten as soon as the thing is picked up,
    // thrown or falls. The one rule for every loose thing, whatever it is.
    void ride(sg::Element& e);
    // The support a thing lies on (its top just under it), or - pinned - the
    // one it is stuck to (its box within a few centimetres of it).
    const sg::Element* support_of(const sg::Element& e, bool pinned) const;

    // --- cables ------------------------------------------------------------------------------
    // (The rope itself - how it hangs, lies, is damped and never passes
    // through anything - is the engine's: sg/physics/Rope.hpp. Here is only
    // what it hangs from, what it lies on, and where its joints are kept.)
    // A cable: links from a point on a support (`anchor`, at `ax, ay, az` from
    // the middle of its base, turned with it) to a point on a loose thing
    // (`body`, at `bx, by, bz` in its own frame), `len` long. It hangs and lies
    // as a cord does - each link's joints fall, and are held to the link's
    // length, to the floor and to the tops of things - and it holds the thing
    // at the end: pulled further than it reaches, the thing is held back.
    // Asleep once neither end has moved for a moment.
    sg::Element& add_cable(sg::Key id, sg::Key anchor, sg::Vec3d a, sg::Key body, sg::Vec3d b, double len, int n);
    static sg::Key strand(sg::Key cable, int i) { return sg::Key{cable.str() + "." + std::to_string(i)}; }
    std::vector<sg::Key> cable_ids() const;

private:
    // Where the hand may pull a thing on a cord to: no further from where the
    // cord is fixed than the cord reaches.
    sg::rigid::V3 within_cords(const std::string& body, sg::rigid::V3 t) const;
    bool cable_ends(const sg::Element& c, sg::Vec3d& a, sg::Vec3d& b) const;
    // What a cord lies on and cannot pass through: the supports within its
    // reach, as blocks, and the loose things lying about, as lumps.
    sg::rope::Ground cable_ground(const sg::Element& c, const sg::Vec3d& a, const sg::Vec3d& b) const;
    // The cord as the rope has it (sg::rope), from its joints' params, and back.
    sg::rope::Rope rope_of(const sg::Element& c) const;
    void keep_rope(const sg::Element& c, const sg::rope::Rope& r);
    // Laid out at its whole length, and let fall.
    void lay_cable(sg::Element& c, bool);
    // Every cable, a step.
    void cables(double dt);
    void settle_cable(sg::Element& c, double dt);

public:

private:
    // The limbs, where their owners have them now (in the room), and whether
    // the room must step for them: one has moved into something at rest.
    struct Limb {
        sg::rigid::Body* body;
        sg::rigid::V3 x;
        sg::rigid::M3 r;
    };
    bool place_limbs(std::vector<Limb>& out);
    // What struck each limb this step, counted for its owner.
    void knock_limbs();
    void knock(sg::Element& limb, sg::rigid::V3 impulse);
    sg::rigid::World world_;
    // The solver's body of an element, by its name: through an index of the
    // solver's list by the names' keys, made again whenever that list is
    // another (`layout`) - derived, and never anything but the list.
    const sg::rigid::Body* solid_of(sg::Key id) const;
    sg::rigid::Body* solid_of(sg::Key id) { return const_cast<sg::rigid::Body*>(std::as_const(*this).solid_of(id)); }
    mutable uint64_t solids_layout_ = 0;
    mutable std::unordered_map<sg::Key, std::size_t> solids_;
    // What each element is to a thing looking for what it lies on - solid
    // or not, in which mode, how far it reaches each way - read from its
    // params and kept by their stamp, by its place in the list while that
    // is the same list (the structure's stamp): the physics asks it of
    // every body under every body, every step.
    struct Lying {
        uint64_t stamp = ~uint64_t{0};
        bool solid = false;
        char mode = 0;  // 'r' at rest, 'h' held, 'f' falling, else 0
        sg::Vec3d ext;
        // Its body in the solver's list, while that is the same list.
        uint64_t layout = ~uint64_t{0};
        std::size_t body = ~std::size_t{0};  // (none)
    };
    mutable std::vector<Lying> lying_;
    mutable uint64_t lying_structure_ = 0;
    const Lying& lying(std::size_t i) const;
    const sg::rigid::Body* solid_at(std::size_t i) const;
    // What each recent step started from (by everything it started from):
    // the contacts it began with. The same start, tried again - in any order,
    // after anything - begins from the same contacts.
    std::vector<std::pair<uint64_t, sg::rigid::World::Contacts>> memo_;
    std::unordered_map<std::string, std::array<double, 6>> support_at_;
    // Each support's parameters as last read (their stamp, by its place
    // among the elements): one that has not changed is not read again -
    // a space of rooms made one has a thousand and more, every step.
    std::vector<uint64_t> support_seen_;
    uint64_t support_structure_ = ~uint64_t{0};
    bool shell_ = false;

    // The shell of the room, and the furniture as it stands now: fixed
    // bodies. A support that has moved wakes whatever sleeps on it.
    // Moved a little since last step: driven there, pushing what is in its
    // way and carrying what lies on it; moved further (reset, carried off to
    // another place), put there.
    static bool nudged(const sg::rigid::Body& b, const sg::rigid::V3& x, const sg::rigid::M3& r);

    // `host` has been put somewhere else from outside, not driven there:
    // whatever lies on it, solid or sheet, is carried the same way (carry.hpp).
    // Called while the host is still where it was.
    void carry(const sg::rigid::Body& host, const Carry& c);

    // The room's shell and its furniture as the solver has them: true if a
    // piece of furniture was driven this step.
    bool statics();

    static bool names(const sg::Event& ev, const sg::Element& e);

    static void aim(sg::Element& e, const sg::Vec3d& p, double yaw, double pitch, double roll);

    // Move towards the aim at `rate` per second; true once there.
    static bool ease(sg::Element& e, double rate, double dt);

    void step(sg::Element& e, double dt);

    // A solid box, free: a rigid body. Its state is where it is, how fast it
    // goes, which way it is turned and how fast it turns (a world angular
    // velocity); its mass is spread evenly through its box. Each step is
    // taken in substeps: gravity, then each of its eight corners that has
    // gone into the floor, a shelf, the tops of the furniture or a wall is
    // pushed back out by an impulse at that corner - restitution along the
    // surface's normal, Coulomb friction across it. The impulse at a corner
    // turns it, so a book landing on a corner tips, rolls and slaps down flat
    // as books do - or stays on its spine if it lands on it square. `hit`
    // says how hard it last struck something, for whoever makes the sound.
    void tumble(sg::Element& e, double dt);
};

}  // namespace sg::physics
