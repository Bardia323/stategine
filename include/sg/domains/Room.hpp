// Stategine - Room: a room as a state. A floor of some shape, walls round it
// and a ceiling over it, openings in its walls - and everything that follows
// from those worked out from them, the same way for every room there is.
//
// A room's coordinates are its own: x from its box's west side, z from its
// north, y up from its floor. Nothing in it knows of any other room; what an
// opening opens onto is up to whatever glues rooms (seams, sg/domains/Atlas).
//
// What it is, is its params:
//
//   room_w, room_d, room_h  its box, and how high it is
//   shape, sides            its floor plan (plan::outline_of), in that box
//   names                   what every id it makes starts with: "" in a room
//                           that is all there is, "<room>." among many
//   wall_t                  how thick its walls are, outside the floor's edge
//   skin                    how far in front of that edge their faces are
//   opening_gap             how far in from it an opening's plane is
//   wall_faces              1: each piece of wall has a face, a portal a
//                           finish is shown on (whoever dresses the room
//                           embeds one); 0: the walls are their own surface,
//                           wall_r/g/b and wall_surface
//   face_finish, face_seed, face_roughness   what is asked of those finishes
//   skirting                how high its skirting is (0: none), skirt_r/g/b,
//                           skirt_surface, skirt_roughness
//   fill_openings           1: an opening that opens onto nothing is filled
//
// An opening is data on a wall (plan::Opening): a portal with `side` (which
// wall, its index in the outline), `along` (how far along it its middle is),
// `sill`, `w`, `h` and `walk` (1 for a door). Its pose follows from those; on
// a curved wall it spans the chord between its two ends. `onto` = 1 says it
// opens onto something. `head` = "round" or "pointed" arches its top (the
// wall round the arch laid as `<opening>.head`), and `recess` sets what fills
// it that far back into the wall. What hangs on an opening - a frame, a switch beside
// a door, a window's bars - hangs off an anchor that says so: `hangs_on` = the
// opening, `hang_yaw` = the opening's yaw it was built square to. The anchor
// stands at the opening's foot, turned as the opening has turned, wherever
// the opening goes.
//
// lay_walls() makes the walls from all that - cut round the openings, run on
// past the room's corners, its floor and its ceiling so that no light finds a
// way between two of them:
//
//   <names>shell_wall_<w>_<n>   a piece of wall: a `wall` element, solid
//   <names>wall_<w>_<n>         its face, if the room's walls have faces
//   <names>skirt_<w><n>         skirting along its foot, but across a doorway
//   <opening>.blank             what fills an opening onto nothing
//   <opening>.head              the wall round an arched opening's arch
//
// <w> is the wall: n, e, s or w in a rectangle, its index in any other shape.
// What it makes again is the same element by the same name, so whatever was
// shown on a face stays on it; what is no longer wanted is put out of the way
// (not alive), never taken away - an interface may still name it.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "sg/domains/Spatial.hpp"

namespace sg::plan {

// --- a floor plan ------------------------------------------------------------
// Every shape is drawn in the room's own box, x from 0 to w and z from 0 to d.
// Its walls are named lines: straight ones from corner to corner, curved ones
// as many short straight pieces. A place on a wall is how far along that line
// it is (`along`); in a rectangle the lines are laid so that `along` is the
// room's own x on the north and south walls and its z on the east and west.
//
//   rect     four walls: north east south west
//   round    one curved wall, round the box (an ellipse if w and d differ)
//   semi     half of one: a flat north wall, and the curve round the south
//   ngon     `sides` straight walls, a regular polygon stretched to the box
//            (hex and oct are 6 and 8)
//   L        a rectangle with its north-east quarter missing
//   T        a bar along the north, a stem down the middle
//   cross    a plus sign
struct P2 {
    double x = 0, z = 0;
};
inline P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.z + b.z}; }
inline P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.z - b.z}; }
inline P2 operator*(P2 a, double s) { return {a.x * s, a.z * s}; }
inline double dot2(P2 a, P2 b) { return a.x * b.x + a.z * b.z; }
inline double cross2(P2 a, P2 b) { return a.x * b.z - a.z * b.x; }
inline double len2(P2 a) { return std::hypot(a.x, a.z); }

struct WallLine {
    std::string name;
    std::vector<P2> pts;
    bool curved = false;

    double length() const;
    // The point `along` it, and which way it runs there.
    P2 at(double along, P2* dir = nullptr) const;
    // The nearest place on it to `p`: how far along, and how far off.
    double nearest(P2 p, double* off = nullptr) const;
    // The points of it from s0 to s1, the ends included.
    std::vector<P2> between(double s0, double s1) const;
};

struct Outline {
    std::string shape = "rect";
    double w = 1, d = 1;
    int sides = 4;
    std::vector<P2> poly;         // the floor's edge, all the way round
    std::vector<WallLine> walls;  // the same edge, as named walls

    bool inside(P2 p) const;
    // How far `p` is from the nearest wall.
    double clearance(P2 p) const;
    // Which way is into the room from a point on its edge, across the
    // direction `dir` the edge runs there.
    P2 inward(P2 p, P2 dir) const;
    int wall_index(const std::string& name) const;
    // A point well inside: where a lamp hangs, where one arrives.
    P2 middle() const;
    // What a wall is called in ids: n, e, s, w in a rectangle; else its index.
    std::string tag(std::size_t wall) const;
};

const std::vector<std::string>& shape_names();
bool known_shape(const std::string& s);

// The walls of a shape, in a box `w` by `d`.
Outline outline_of(std::string shape, double w, double d, int sides = 6);

// The walls, as a line of text a program can read back:
// `name:x,z x,z ...|name:...`.
std::string walls_text(const Outline& o);
std::vector<WallLine> walls_from_text(const std::string& s);

// --- openings ----------------------------------------------------------------
inline constexpr double kDoorW = 0.9, kDoorH = 2.05;

// An opening's facts, read off its portal.
struct Opening {
    Key id;
    int side = 0;
    double along = 0, sill = 0, w = kDoorW, h = kDoorH;
    bool door = true;  // walked through; a window is only looked through
    // Its top: "" square, "round" (a half circle) or "pointed" (two arcs
    // meeting, each struck from the other's springing) - `h` to its crown.
    std::string head;
    double recess = 0;  // how far back from the wall's face what fills it stands

    double lo() const { return along - w * 0.5; }
    double hi() const { return along + w * 0.5; }
    // Where its head springs: its sides rise straight to here.
    double spring() const;
};

Opening opening_of(const Element& e);
inline bool is_opening(const Element& e) { return e.kind == kinds::portal && e.params.has(Key{"side"}); }

// A piece of wall: from s0 to s1 along it, y0 to y1 up it.
struct WallPiece {
    double s0, s1, y0, y1;
};

// A wall `length` long and `height` high, less its openings: cut across at
// every opening's sides, each span of it whole where nothing opens it, and
// between, under and over the openings in it where they do - so openings may
// stand one over another (an arcade under a clerestory).
std::vector<WallPiece> wall_pieces(double length, double height, std::vector<Opening> holes);

// What fills the head of an opening round its arch, as triangles in the unit
// box a mesh is sized from (sg/domains/Shapes.hpp): the two spandrels
// between the arch and the square it stands in, pushed through the wall.
// Empty for a square head.
std::vector<float> head_shape(const Opening& o);

// Can `o` go where it says, on the walls of `ol`, `height` high, beside
// `others`? The reason if not.
bool opening_fits(const Opening& o, const Outline& ol, double height, const std::vector<Opening>& others,
                  std::string* why = nullptr);

// Where an opening is: the middle of the chord across it, which way is in,
// and the yaw it faces.
void chord(const Outline& ol, const Opening& o, P2& mid, P2& in, double& yaw);

}  // namespace sg::plan

namespace sg {

class Room : public Spatial3D {
public:
    // A room `w` by `d`, `h` high, of `shape`. Its walls are laid when it is
    // dressed as it should be (lay_walls) - the params above first.
    Room(Key id, double w, double d, double h = 3.0, const std::string& shape = "rect", int sides = 6,
         const std::string& names = "");

    double w() const { return params().num("room_w"); }
    double d() const { return params().num("room_d"); }
    double h() const { return params().num("room_h"); }
    std::string shape() const { return params().get_or<std::string>("shape", "rect"); }
    int sides() const { return static_cast<int>(params().num("sides", 6)); }
    plan::Outline outline() const { return plan::outline_of(shape(), w(), d(), sides()); }

    // The id of a part of it, as it names its own.
    Key part(const std::string& name) const { return Key{params().get_or<std::string>("names", "") + name}; }

    // --- openings ------------------------------------------------------------
    std::vector<plan::Opening> openings() const;

    // Whether `o` can be where it says, beside the openings there are.
    bool fits(const plan::Opening& o, std::string* why = nullptr) const;

    // A door (walked through) or a window (looked through) called `name`, on
    // wall `side`, its middle `along` it - if it fits there.
    Element* add_opening(const std::string& name, int side, double along, double ow, double oh, double sill, bool door,
                         std::string* why = nullptr);
    bool move_opening(Key id, int side, double along, std::string* why = nullptr);
    // Another size: `w` wide, `h` high, its bottom `sill` up.
    bool size_opening(Key id, double ow, double oh, double sill, std::string* why = nullptr);
    bool remove_opening(Key id, std::string* why = nullptr);

    // Whether an opening opens onto anything: one that does not is filled in
    // (`fill_openings`).
    void opens(Key id, bool onto_something);

    // Every opening's portal posed from its facts, and what hangs on it.
    void seat_openings();
    void seat(Element& opening);

    // The nearest place on any wall to (x, z): which wall, how far along.
    int nearest_wall(double x, double z, double& along) const;

    // At a doorway, a room is level ground at its floor unless its fields
    // and solids say otherwise (State::overlap).
    Params overlap(Key boundary) const override;

    // --- walls ---------------------------------------------------------------
    void lay_walls();

    // What lay_walls made: "shell", "face", "skirt" or "blank" (`laid`).
    std::vector<const Element*> laid(const std::string& what) const;

private:
    // How far a wall runs on past the room's corner at `p` (coming from `q`):
    // its thickness at an outside corner, nothing at an inside one.
    static double corner_run(const plan::Outline& ol, plan::P2 p, plan::P2 q, double t);
    // Along a curved wall: enough to close the gap where two pieces meet.
    static double bend(const std::vector<plan::P2>& pts, std::size_t i, double t);
};

}  // namespace sg
