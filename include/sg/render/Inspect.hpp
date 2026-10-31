// Stategine - the inspector: a room measured as it is drawn, without drawing
// it (docs/workflows.md section 14, *The inspector*).
//
// Every thing of a room that an eye sees is taken as the triangles the
// renderer draws it with - a model's own corners, a box, a cylinder, a
// panel - placed by the renderer's own box_transform, in the room's frame.
// No window, no card, no frame: a room of thousands of things is measured
// in a second or two, on the processor, right after it is built. What is
// measured, each to the millimetre and named by thing and place:
//
//   floating   a thing (with all it touches) touching nothing that comes
//              down to the room's shell - a door in mid air, planks over a
//              floor - and how far it is above what is under it.
//   fighting   two faces facing the same way, a hair apart, overlapping,
//              that the depth test cannot tell apart from some distance the
//              eye can stand at - the vertex stage's nudge (the smaller
//              drawn nearer, sg/gl/Shaders.hpp) weighed as it draws them:
//              two that coincide, of one size; or one a millimetre or two
//              behind, drawn nearer by enough to tie with the other a few
//              metres off (z fighting that comes and goes with distance).
//   sunk       a thing passing more than `sink` into another (a lamp
//              through its wall), not one of its own parts.
//
// And it is seen: a sheet for the room of five projections, square on - its
// plan cut below the ceiling and its four walls each seen from the room's
// middle - each thing in a colour of its own, shaded by which way it
// faces, every fault outlined and numbered as it is listed.
//
// A thing is all the parts that share the first two parts of their name
// (`b4.door_leaf.panel` and `b4.door_leaf.knob` are the door_leaf), its group, or hang on it
// (`parent`). The building is what is laid (a room's shell), a wall, or what
// says `building` = 1 (a floor, a ceiling): everything else stands on it,
// hangs from it, or is sunk in it by design. A thing that says `mount` =
// "free" may float; `inspect_ok` = "<reason>" passes it of every check.
#pragma once
#include <string>
#include <vector>

#include "sg/domains/Spatial.hpp"

namespace sg::render {

struct InspectOptions {
    double touch = 0.006;   // as near as this, two things touch (m)
    double fight = 0.006;   // faces nearer than this, facing the same way, are weighed for fighting
    double seen = 12.0;     // ... and fight if the eye can see them tie from as near as this (m)
    double fight_area = 2e-4;  // ... over this much of them (m^2)
    double sink = 0.025;    // one thing this far into another is sunk
    double px = 0.01;       // a pixel of a projection (m)
    int most_px = 1400;     // and no projection wider or higher than this
    std::string out;        // a folder for the sheet (room.png) and the list (room.txt); empty: none
};

struct InspectFault {
    std::string kind;   // floating, fighting, sunk
    std::string thing, other;
    Vec3d at;           // where, in the room
    double amount = 0;  // floating: its gap (m); fighting: overlap (m^2); sunk: how deep (m)
    Vec3d lo, hi;       // the box round what is at fault (for the sheet)
    std::string text;
};

struct InspectReport {
    std::vector<InspectFault> faults;
    std::size_t triangles = 0, things = 0;
    double seconds = 0;
};

InspectReport inspect(const Spatial3D& room, const InspectOptions& options = {});

// The faults alone, each a line as inspect_lines says it - what prepare and
// a project's laws say. Kept on disk (sg::cache) by the room's drawn
// triangles and the inspector's own code: a room unchanged is not measured again.
std::vector<std::string> inspect_faults(const Spatial3D& room, const InspectOptions& options = {});

// A ray against what is drawn - not what stops a walker (a thing's solid box
// can stand above or past its drawn faces): the distance along `dir` to the
// first drawn face from `from` within `reach`, or a negative number. For
// setting a thing down on what is seen (a page on a table's drawn top).
double drawn_hit(const Spatial3D& room, const Vec3d& from, const Vec3d& dir, double reach);
// How far the thing `name` (its parts: `name` and `name.*`, in the room's
// own naming) stands over what is drawn under it: the least gap from any of
// its drawn corners straight down to another drawn face - what to lower it by
// to set it down on what it stands on (a model's foot is not always its
// lowest point). Negative: nothing under it.
double drawn_drop(const Spatial3D& room, const std::string& name);

// Many at once, the room gathered once: each ray's distance, or a negative number.
std::vector<double> drawn_hits(const Spatial3D& room, const std::vector<std::pair<Vec3d, Vec3d>>& rays, double reach);

// Each fault as a line: "<kind> <thing> [<other>] at x,y,z: ...".
std::vector<std::string> inspect_lines(const InspectReport& report);

}  // namespace sg::render
