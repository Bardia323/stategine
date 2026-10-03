// A room on its own: walls laid round any floor plan, cut round its openings,
// closed at every corner; faces and skirting as the room says; what hangs on
// an opening going with it; and nothing anyone may name ever taken away.
#include "sg/domains/Room.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static constexpr double kPi = 3.141592653589793;
static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static int count(const sg::Room& r, const std::string& what, bool alive_only = true) {
    int n = 0;
    for (const sg::Element* e : r.laid(what)) n += !alive_only || e->alive;
    return n;
}

int main() {
    using sg::Key;
    {
        sg::Room r(Key{"box"}, 6, 4, 3);
        r.lay_walls();
        check(count(r, "shell") == 4 && count(r, "face") == 0 && count(r, "skirt") == 0,
              "a bare rectangle: four walls, no faces, no skirting unless asked");
        const sg::Element& n = r.element(Key{"shell_wall_n_0"});
        check(std::fabs(n.params.num(sg::keys::sx) - 6.4) < 1e-9 && std::fabs(n.params.num(sg::keys::sy) - 3.4) < 1e-9,
              "a wall runs on past both corners, under the floor and over the ceiling: no light finds a way between");

        sg::Element* door = r.add_opening("door", 2, 3.0, 0.9, 2.05, 0.0, true);
        check(door && std::fabs(door->params.num(sg::keys::z) - 4.0) < 1e-9 && std::fabs(door->params.num(sg::keys::x) - 3.0) < 1e-9,
              "an opening stands on its wall, where its facts say");
        check(count(r, "shell") == 6, "the wall is cut round it: either side, and over it");
        std::string why;
        check(!r.add_opening("door2", 2, 3.3, 0.9, 2.05, 0.0, true, &why) && why.find("run into") != std::string::npos,
              "two openings do not overlap");
        check(!r.add_opening("high", 0, 3.0, 1.0, 3.0, 0.5, false, &why) && why.find("taller") != std::string::npos,
              "nor does one stand taller than its wall");
        check(r.element(Key{"door.blank"}).alive, "an opening onto nothing is filled in");
        r.opens(Key{"door"}, true);
        check(!r.element(Key{"door.blank"}).alive && r.element(Key{"door"}).params.num("onto") == 1.0,
              "and open once it opens onto something - which it says itself");

        // What hangs on it goes with it.
        sg::Element& kit = r.anchor(Key{"kit"}, {3.0, 0.0, 4.0}, 0.0);
        kit.params.set("hangs_on", std::string("door")).set("hang_yaw", door->params.num(sg::keys::yaw));
        check(r.move_opening(Key{"door"}, 1, 2.0, &why), "an opening moved to another wall");
        check(std::fabs(kit.params.num(sg::keys::x) - 6.0) < 1e-9 && std::fabs(kit.params.num(sg::keys::z) - 2.0) < 1e-9 &&
                  std::fabs(std::remainder(kit.params.num(sg::keys::yaw) - 1.5 * kPi, 2 * kPi)) < 1e-9,
              "what hangs on it is at its foot, turned as it turned");
        check(count(r, "shell") == 6 && count(r, "shell", false) >= 6 && r.find(Key{"shell_wall_s_2"}) &&
                  !r.element(Key{"shell_wall_s_2"}).alive,
              "a piece of wall no longer wanted is out of the way, not taken away");
        r.move_opening(Key{"door"}, 2, 3.0);
        check(r.element(Key{"shell_wall_s_2"}).alive, "and the same element when it is wanted again");
    }
    {
        sg::Room r(Key{"den"}, 5, 5, 2.6, "rect", 6, "den.");
        r.params().set("wall_faces", 1.0).set("skin", 0.001).set("skirting", 0.1);
        r.add_opening("door", 0, 2.5, 0.9, 2.05, 0.0, true);
        check(r.find(Key{"den.wall_n_0"}) && r.find(Key{"den.shell_wall_n_0"}) && r.find(Key{"den.skirt_n1"}),
              "ids start with the room's names");
        check(count(r, "face") == count(r, "shell"), "every piece of wall has its face");
        const sg::Element& left = r.element(Key{"den.wall_n_0"});
        const sg::Element& right = r.element(Key{"den.wall_n_2"});  // (1 is over the door)
        check(std::fabs(left.params.num("face_ox") - 0.0) < 1e-9 && std::fabs(right.params.num("face_ox") - (2.5 + 0.45)) < 1e-9,
              "a face is measured from the end of its wall, so one pattern runs across every piece");
        check(count(r, "skirt") == 5 && !r.find(Key{"den.skirt_n2"}) && std::fabs(r.element(Key{"den.skirt_n0"}).params.num(sg::keys::sx) - (2.05 - 0.08)) < 1e-9,
              "skirting runs to the door's casing, and not across the doorway");
    }
    {
        sg::Room r(Key{"ell"}, 6, 6, 3, "L");
        r.lay_walls();
        // The inside corner of an L: the walls meeting there do not run on into the room.
        const sg::plan::Outline ol = r.outline();
        bool clear = true;
        for (const sg::Element* e : r.laid("shell")) {
            const double x = e->params.num(sg::keys::x), z = e->params.num(sg::keys::z);
            clear &= !(ol.inside({x, z}) && ol.clearance({x, z}) > 0.11);
        }
        check(clear && count(r, "shell") == 6, "an L's six walls stand outside its floor, inside corner and all");
        sg::Room round(Key{"tower"}, 6, 6, 4, "round");
        round.lay_walls();
        check(count(round, "shell") == 96, "a round room's wall follows its curve, piece by piece");
    }
    {
        sg::StateGraph g;
        sg::Room& r = g.add<sg::Room>(Key{"room"}, 4, 4, 3);
        r.params().set("skirting", 0.1);
        r.add_opening("door", 2, 2.0, 0.9, 2.05, 0.0, true);
        const sg::LawReport rep = sg::verify(g);
        check(rep.ok(), "a room keeps the laws: its walls are fixtures, with no arrows of their own");
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
