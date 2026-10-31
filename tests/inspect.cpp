// Stategine - the inspector (sg/render/Inspect.hpp) and the walker tried
// (sg::walked_onto): for each mistake a built place was found with, a tiny
// place that shows it - and must be named - and the same place mended,
// which must not be.
//
//   floating  a crate over the floor; a page a centimetre over its table
//   sunk      a lamp's box through its wall
//   fighting  two boards of one size laid in one plane; a paper a hair under
//             a table's top, drawn nearer than it by its size, from a few metres
//   climbed   a table half a metre high a walker is pushed into (its feet a
//             ball, rolled over the edge onto it); a step it may take
#include <cstdio>
#include <string>
#include <vector>

#include "sg/domains/Walk.hpp"
#include "sg/render/Inspect.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
void say(const std::vector<std::string>& fs) {
    for (const std::string& f : fs) std::printf("      %s\n", f.c_str());
}
bool names(const std::vector<std::string>& fs, const std::string& kind, const std::string& what) {
    for (const std::string& f : fs)
        if (f.rfind(kind, 0) == 0 && f.find(what) != std::string::npos) return true;
    return false;
}

// A room 4 by 4: its floor a slab under 0 (the building's), a wall along x = 2 (its face at x 1.9).
struct Place {
    sg::StateGraph g;
    sg::Spatial3D* room;
    Place() {
        room = &g.add<sg::Spatial3D>("room");
        g.set_initial("room");
        room->params().set("enclosed", 0.0);
        room->wall("east", {2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
        box("floor", {0.0, -0.2, 0.0}, 4.0, 0.2, 4.0).params.set("building", 1.0);
        room->camera().params.set("step", 0.34).set("height", 1.65);
    }
    sg::Element& box(const char* id, sg::Vec3d foot, double sx, double sy, double sz, bool solid = true) {
        sg::Element& e = room->fixture(sg::Key{id}, foot.x, foot.y, foot.z);
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz).set("solid", solid ? 1.0 : 0.0);
        return e;
    }
    std::vector<std::string> faults() const { return sg::render::inspect_lines(sg::render::inspect(*room)); }
};
}  // namespace

int main() {
    // 1. Floating: a crate on the floor, and 12 cm over it.
    for (double foot : {0.0, 0.12}) {
        Place p;
        p.box("crate", {-1.0, foot, 0.0}, 0.5, 0.4, 0.5);
        const auto f = p.faults();
        if (foot == 0.0) check(!names(f, "floating", "crate"), "floating: a crate on the floor is let be"), say(f);
        else check(names(f, "floating", "crate") && names(f, "floating", "120 mm"), "floating: a crate 12 cm over the floor is named, with its gap"), say(f);
    }
    {
        // A page a centimetre over its table, and on it.
        for (double over : {0.01, 0.0}) {
            Place p;
            p.box("table", {-1.0, 0.0, 0.0}, 1.0, 0.75, 0.6);
            p.box("page", {-1.0, 0.75 + over, 0.0}, 0.21, 0.001, 0.297, false);
            const auto f = p.faults();
            if (over > 0.0) check(names(f, "floating", "page"), "floating: a page a centimetre over its table is named"), say(f);
            else check(!names(f, "floating", "page"), "floating: a page on its table is let be"), say(f);
        }
    }
    // 2. Sunk: a lamp's box 8 cm into its wall, and with its back on the wall's face.
    for (double into : {0.08, 0.0}) {
        Place p;
        p.box("lamp", {1.9 - 0.06 + into, 1.8, 0.0}, 0.12, 0.2, 0.12).params.set("mount", "wall");
        const auto f = p.faults();
        if (into > 0.0) check(names(f, "sunk", "lamp"), "sunk: a lamp 8 cm into its wall is named"), say(f);
        else check(!names(f, "sunk", "lamp") && !names(f, "floating", "lamp"), "sunk: a lamp with its back on the wall's face is let be"), say(f);
    }
    // 3. Fighting: two boards of one size, one plane - and one stood 5 mm back.
    for (double apart : {0.0, 0.005}) {
        Place p;
        p.box("board_a", {-1.0, 0.0, 0.0}, 1.0, 0.02, 0.3);
        p.box("board_b", {-0.9, apart, 0.05}, 1.0, 0.02, 0.3);
        const auto f = p.faults();
        if (apart == 0.0) check(names(f, "fighting", "board_"), "fighting: two boards of one size in one plane are named"), say(f);
        else check(!names(f, "fighting", "board_"), "fighting: the same, 5 mm apart (the nudge of one size never ties them) is let be"), say(f);
    }
    {
        // A paper half a millimetre under a table's top: drawn nearer than the top by its size, the two tie seen from some metres off.
        Place p;
        p.box("table", {-1.0, 0.0, 0.0}, 1.0, 0.75, 0.6);
        p.box("paper", {-1.0, 0.75 - 0.0005 - 0.0005, 0.0}, 0.21, 0.0005, 0.297, false);
        const auto f = p.faults();
        check(names(f, "fighting", "paper"), "fighting: a paper a hair under a table's top, drawn nearer by its size, is named"), say(f);
    }
    // 4. Climbed: a table half a metre high and a walker stepping 0.34 pushed into it from all round - never on it;
    // a 20 cm step it may take, named unless it says `climb`.
    {
        Place p;
        p.box("table", {-0.6, 0.0, 0.0}, 0.9, 0.53, 0.6);
        const auto f = sg::walked_onto(*p.room, p.room->camera());
        check(f.empty(), "climbed: a walker pushed into a table 53 cm high never stands on it"), say(f);
    }
    for (bool may : {false, true}) {
        Place p;
        sg::Element& s = p.box("crate", {-0.6, 0.0, 0.0}, 0.9, 0.2, 0.6);
        if (may) s.params.set("climb", 1.0);
        const auto f = sg::walked_onto(*p.room, p.room->camera());
        if (!may) check(!f.empty() && f.front().find("crate") != std::string::npos, "climbed: a crate 20 cm high, a step, is climbed - and named"), say(f);
        else check(f.empty(), "climbed: the same crate saying `climb` is let be"), say(f);
    }
    std::printf("%s\n", ok ? "inspect: all hold" : "inspect: FAILS");
    return ok ? 0 : 1;
}
