// Stategine - guardrails (sg/domains/Guard.hpp): for each mistake a built
// place was found with, a tiny place that shows it - and must be named - and
// the same place mended, which must not be.
//
//   mount     a sconce sunk in its wall, one off it; a wardrobe hovering, one
//             sunk in the floor
//   opening   a skirting run across a doorway
//   door      a door's frame on a bare wall; a doorway walked into nothing
//   level     two boards laid level over each other
//   inside    a box stood inside another
//   pad       a switch's pad with nothing drawn at it
//   taken     a thing taken still drawn and casting
//   climb     a crate a walker steps onto; a walker's step too high
#include <cstdio>
#include <string>
#include <vector>

#include "sg/domains/Atlas.hpp"
#include "sg/domains/Guard.hpp"
#include "sg/domains/Walk.hpp"
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
bool names(const std::vector<std::string>& fs, const std::string& what) {
    for (const std::string& f : fs)
        if (f.find(what) != std::string::npos) return true;
    return false;
}

// A room 4 by 4, its floor at 0, a wall along x = 2 (its face at x 1.9).
struct Place {
    sg::StateGraph g;
    sg::Spatial3D* room;
    Place() {
        room = &g.add<sg::Spatial3D>("room");
        g.set_initial("room");
        room->wall("east", {2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
    }
    sg::Element& box(const char* id, sg::Vec3d foot, double sx, double sy, double sz, bool solid = true) {
        sg::Element& e = room->fixture(sg::Key{id}, foot.x, foot.y, foot.z);
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz).set("solid", solid ? 1.0 : 0.0);
        return e;
    }
};
}  // namespace

int main() {
    // 1. Mounted: a sconce 10 cm deep, its back at the wall's face (x 1.9).
    for (double back : {1.9, 1.96, 1.8}) {
        Place p;
        p.box("hall_sconce", {back - 0.05, 1.8, 0.0}, 0.1, 0.25, 0.12, false).params.set("mount", "wall");
        const auto f = sg::mount_faults(*p.room);
        if (back == 1.9) check(f.empty(), "mount: a sconce with its back on the wall's face is let be"), say(f);
        if (back == 1.96) check(names(f, "sunk"), "mount: a sconce 6 cm into its wall is named sunk"), say(f);
        if (back == 1.8) check(names(f, "floats") && names(f, "hall_sconce"), "mount: a sconce 10 cm off its wall is named floating"), say(f);
    }
    {
        // Unsaid, but named for a wall: a switch off the wall.
        Place p;
        p.box("light_switch", {1.7, 1.2, 0.0}, 0.02, 0.1, 0.08, false);
        check(names(sg::mount_faults(*p.room), "light_switch"), "mount: a switch (named so, `mount` unsaid) off its wall is named");
    }
    for (double foot : {0.0, 0.12, -0.08}) {
        Place p;
        p.box("wardrobe", {0.0, foot, 0.0}, 1.0, 1.9, 0.6);
        const auto f = sg::mount_faults(*p.room);
        if (foot == 0.0) check(f.empty(), "mount: a wardrobe on the floor is let be"), say(f);
        if (foot > 0.0) check(names(f, "hovers"), "mount: a wardrobe 12 cm over the floor is named hovering"), say(f);
        if (foot < 0.0) check(names(f, "into the floor"), "mount: a wardrobe 8 cm into the floor is named sunk"), say(f);
    }
    {
        // On its legs it is held.
        Place p;
        p.box("cabinet", {0.0, 0.12, 0.0}, 1.0, 0.8, 0.5);
        p.box("cabinet_leg", {0.45, 0.0, 0.2}, 0.04, 0.12, 0.04);
        check(sg::mount_faults(*p.room).empty(), "mount: a cabinet on its legs is let be");
    }
    // 2. A doorway in the east wall (its middle 1.05 up), a skirting along the wall.
    for (bool stopped : {false, true}) {
        Place p;
        p.room->portal("to_hall", {1.9, 1.05, 0.0}, 0.9, 2.1, 3.14159265358979).params.set("walk", 1.0);
        if (!stopped) p.box("skirting_e", {1.88, 0.0, 0.0}, 0.02, 0.12, 4.0, false);
        else {
            p.box("skirting_e1", {1.88, 0.0, -1.225}, 0.02, 0.12, 1.55, false);
            p.box("skirting_e2", {1.88, 0.0, 1.225}, 0.02, 0.12, 1.55, false);
        }
        const auto f = sg::opening_faults(*p.room);
        if (!stopped) check(names(f, "skirting_e") && names(f, "to_hall"), "opening: a skirting run across a doorway is named"), say(f);
        else check(f.empty(), "opening: a skirting stopped at the jambs is let be"), say(f);
    }
    // 3. A door's frame with nothing behind it; the same at a doorway; a doorway glued to nothing.
    for (bool doorway : {false, true}) {
        Place p;
        p.box("study.door_frame", {1.85, 0.0, 0.0}, 0.1, 2.2, 1.0, false);
        if (doorway) p.room->portal("to_study", {1.9, 1.05, 0.0}, 0.9, 2.1, 3.14159265358979);
        const auto f = sg::door_faults(*p.room);
        if (!doorway) check(names(f, "leads nowhere"), "door: a door's frame on a bare wall is named"), say(f);
        else check(f.empty(), "door: a door's frame at a doorway is let be"), say(f);
    }
    {
        Place p;
        p.room->portal("to_void", {1.9, 1.05, 0.0}, 0.9, 2.1, 3.14159265358979).params.set("walk", 1.0);
        check(names(sg::door_faults(*p.room, &p.g), "no seam"), "door: a doorway walked through but glued to nothing is named");
        p.room->element(sg::Key{"to_void"}).params.set("blind", 1.0);
        check(sg::door_faults(*p.room, &p.g).empty(), "door: the same said `blind` is let be");
    }
    // 4. Two boards laid level.
    for (double lift : {0.0, 0.004}) {
        Place p;
        p.box("floor_a", {0.0, -0.02, 0.0}, 2.0, 0.02, 2.0, false);
        p.box("floor_b", {0.8, -0.02 + lift, 0.0}, 2.0, 0.02, 2.0, false).params.set("r", 0.3);
        const auto f = sg::level_faults(*p.room);
        if (lift == 0.0) check(names(f, "level"), "level: two floors laid in one plane, overlapping, are named"), say(f);
        else check(f.empty(), "level: the same, one 4 mm higher, is let be"), say(f);
    }
    // 5. A box inside another.
    for (double x : {0.2, 1.2}) {
        Place p;
        p.box("table", {0.0, 0.0, 0.0}, 1.0, 0.75, 0.6);
        p.box("chest", {x, 0.3, 0.0}, 0.5, 0.4, 0.4);
        const auto f = sg::inside_faults(*p.room);
        if (x < 1.0) check(names(f, "inside"), "inside: a chest stood inside a table is named"), say(f);
        else check(f.empty(), "inside: the chest beside it is let be"), say(f);
    }
    // 6. A switch's pad, unseen, with and without the switch drawn at it.
    for (bool shown : {false, true}) {
        Place p;
        p.box("pad_switch", {1.85, 1.2, 0.0}, 0.1, 0.1, 0.1, false).params.set("unseen", 1.0).set("use", "light");
        if (shown) p.box("switch_plate", {1.89, 1.2, 0.0}, 0.02, 0.12, 0.08, false).params.set("mount", "wall");
        const auto f = sg::pad_faults(*p.room);
        if (!shown) check(names(f, "pad_switch"), "pad: a used pad with nothing drawn at it is named"), say(f);
        else check(f.empty(), "pad: the pad at its drawn switch is let be"), say(f);
    }
    // 8, 9. Taken, still drawn and casting; taken and gone.
    {
        Place p;
        sg::Element& key = p.box("key", {0.0, 0.0, 0.0}, 0.05, 0.01, 0.02, false);
        key.params.set("taken", 1.0);
        check(names(sg::taken_faults(*p.room), "casting"), "taken: a key taken but still drawn and casting is named");
        key.params.set("unseen", 1.0).set("cast", 0.0);
        check(sg::taken_faults(*p.room).empty(), "taken: the key unseen and casting nothing is let be");
    }
    // 10. A crate 20 cm high (a step), a chair seat 45 cm on it; and a walker that steps 40 cm.
    {
        Place p;
        p.box("crate", {0.0, 0.0, 0.0}, 0.5, 0.2, 0.5);
        p.box("chair_seat", {0.6, 0.0, 0.0}, 0.45, 0.42, 0.45);
        const auto f = sg::climb_faults(*p.room, 0.22);
        check(names(f, "crate") && names(f, "chair_seat"), "climb: a crate a step high, and the chair it leads to, are named"), say(f);
        p.room->element(sg::Key{"crate"}).params.set("solid", 0.0);
        check(sg::climb_faults(*p.room, 0.22).empty(), "climb: the crate not solid - the chair out of a step's reach - is let be");
        check(names(sg::climb_faults(*p.room, 0.4), "riser"), "climb: a walker that steps 40 cm is named");
    }
    // All of them through one door.
    {
        Place p;
        p.box("wardrobe", {0.0, 0.12, 0.0}, 1.0, 1.9, 0.6);
        check(names(sg::guard_faults(*p.room), "mount: wardrobe"), "guard_faults: says each with its law's name");
    }
    // 5. A doorway walked through, with a chest stood before it; and moved aside.
    for (double z : {0.0, 1.4}) {
        Place p;
        p.room->portal("to_hall", {1.9, 1.05, 0.0}, 0.9, 2.1, 3.14159265358979).params.set("walk", 1.0);
        p.box("chest", {1.3, 0.0, z}, 0.6, 0.9, 1.0);
        const auto f = sg::doorway_faults(*p.room);
        if (z == 0.0) check(names(f, "cannot be walked to"), "walkway: a chest stood before a doorway is named"), say(f);
        else check(f.empty(), "walkway: moved aside, the doorway is let be"), say(f);
    }
    // 4, at a seam: two rooms glued at a doorway, each floor run 30 cm past it into the other; and ended at it.
    for (double over : {0.3, 0.0}) {
        sg::StateGraph g;
        auto& a = g.add<sg::Spatial3D>("a");
        auto& b = g.add<sg::Spatial3D>("b");
        g.set_initial("a");
        a.portal("a.door", {2.0, 1.1, 0.0}, 1.0, 2.2, 3.14159265358979).params.set("walk", 1.0).set("sheaf", 1.0);
        b.portal("b.door", {-2.0, 1.1, 0.0}, 1.0, 2.2, 0.0).params.set("walk", 1.0).set("sheaf", 1.0);
        a.fixture(sg::Key{"a.floor"}, over * 0.5, -0.02, 0.0).params.set(sg::keys::sx, 4.0 + over).set(sg::keys::sy, 0.02).set(sg::keys::sz, 4.0);
        b.fixture(sg::Key{"b.floor"}, -over * 0.5, -0.02, 0.0).params.set(sg::keys::sx, 4.0 + over).set(sg::keys::sy, 0.02).set(sg::keys::sz, 4.0);
        sg::glue_doorway(g, sg::Key{"ab"}, a.id(), sg::Key{"a.door"}, b.id(), sg::Key{"b.door"});
        const auto f = sg::seam_level_faults(a, g);
        if (over > 0.0) check(names(f, "across the seam"), "seam: two floors run past their doorway into each other are named"), say(f);
        else check(f.empty(), "seam: the floors ended at the doorway are let be"), say(f);
    }
    // What is drawn: rooms not glued into one depth buffer are clipped at the doorway's plane and never fight -
    // one under a sky and one walled (not one space); and two rooms whose walls stand 1 m in each other (more
    // than the renderer glues). Their floors overlapping across the doorway are let be.
    for (int kind : {0, 1, 2, 3}) {
        sg::StateGraph g;
        auto& a = g.add<sg::Spatial3D>("a");
        auto& b = g.add<sg::Spatial3D>("b");
        g.set_initial("a");
        a.portal("a.door", {2.0, 1.1, 0.0}, 1.0, 2.2, 3.14159265358979).params.set("walk", 1.0).set("sheaf", 1.0);
        b.portal("b.door", {-2.0, 1.1, 0.0}, 1.0, 2.2, 0.0).params.set("walk", 1.0).set("sheaf", 1.0);
        a.fixture(sg::Key{"a.floor"}, 0.15, -0.02, 0.0).params.set(sg::keys::sx, 4.3).set(sg::keys::sy, 0.02).set(sg::keys::sz, 4.0);
        b.fixture(sg::Key{"b.floor"}, -0.15, -0.02, 0.0).params.set(sg::keys::sx, 4.3).set(sg::keys::sy, 0.02).set(sg::keys::sz, 4.0);
        if (kind == 0) b.params().set("sky", 1.0);
        else if (kind == 3) a.element(sg::Key{"a.door"}).params.erase(sg::Key{"sheaf"}), b.element(sg::Key{"b.door"}).params.erase(sg::Key{"sheaf"});
        else if (kind == 2) {
            a.wall("a.wall_e", {2.15, 0.0, 0.0}, 0.2, 3.0, 4.0);  // (the rooms 0.5 m in each other: glued, one depth buffer)
            a.wall("a.wall_w", {-2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
            b.wall("b.wall_w", {-2.15, 0.0, 0.0}, 0.2, 3.0, 4.0);
            b.wall("b.wall_e", {2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
        } else {
            a.wall("a.wall_e", {3.0, 0.0, 0.0}, 0.2, 3.0, 4.0);  // (a's walls run 1 m into b)
            a.wall("a.wall_w", {-2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
            b.wall("b.wall_w", {-3.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
            b.wall("b.wall_e", {2.0, 0.0, 0.0}, 0.2, 3.0, 4.0);
        }
        sg::glue_doorway(g, sg::Key{"ab"}, a.id(), sg::Key{"a.door"}, b.id(), sg::Key{"b.door"});
        const auto f = sg::seam_level_faults(a, g);
        if (kind == 2) {
            check(names(f, "across the seam"), "seam: rooms glued (0.5 m in each other, one depth buffer) with floors overlapping are named"), say(f);
            continue;
        }
        check(f.empty(), kind == 0   ? "seam: rooms not one space (sky and walls) - clipped at the doorway - let be"
                         : kind == 3 ? "seam: doorways not saying `sheaf` - each room seen through the other, clipped - let be"
                                     : "seam: rooms standing 1 m in each other - never in one depth buffer - let be"), say(f);
    }
    // Every place is guarded: its guardrails are its faults - validation names them, verify refuses them -
    // unless it says `unguarded` with a reason.
    {
        Place p;
        p.box("wardrobe", {0.0, 0.12, 0.0}, 1.0, 1.9, 0.6);
        const auto said = p.g.validate();
        check(names(said, "state room: mount: wardrobe"), "guarded by default: validation names the hovering wardrobe"), say(said);
        check(!sg::verify(p.g).ok(), "guarded by default: verify refuses it");
        p.room->params().set("unguarded", 1.0);
        check(names(p.g.validate(), "without a reason"), "unguarded with no reason: named");
        p.room->params().set("unguarded", std::string("built before the guardrails"));
        check(!names(p.g.validate(), "mount:"), "unguarded with a reason: let be (prepare still says it)");
        p.room->params().erase(sg::Key{"unguarded"});
        p.room->element(sg::Key{"wardrobe"}).params.set(sg::keys::y, 0.0);
        check(!names(p.g.validate(), "mount:"), "guarded, set on the floor: let be");
    }
    std::printf("%s\n", ok ? "all guardrails hold" : "SOME GUARDRAILS FAIL");
    return ok ? 0 : 1;
}
